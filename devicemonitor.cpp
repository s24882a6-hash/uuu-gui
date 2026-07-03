#include "devicemonitor.h"
#include <QMap>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>

#ifdef HAVE_LIBUSB
#  include <libusb.h>
#endif

// NXP / Freescale USB VIDs used in SDP / fastboot recovery mode
static constexpr quint16 VID_NXP       = 0x1fc9;
static constexpr quint16 VID_FREESCALE = 0x15a2;

// ──────────────────────────────────────────────────────────────────────────────
// UsbDevice helpers
// ──────────────────────────────────────────────────────────────────────────────

QString UsbDevice::displayName() const
{
    QString name;
    if (!product.isEmpty())
        name = product;
    else
        name = QString("NXP %1:%2").arg(vendorId, 4, 16, QLatin1Char('0'))
                                    .arg(productId, 4, 16, QLatin1Char('0'));

    if (!serialNumber.isEmpty())
        name += QString(" [%1]").arg(serialNumber);
    else
        name += QString(" [%1]").arg(busId);

    return name;
}

// ──────────────────────────────────────────────────────────────────────────────
// DeviceMonitorThread
// ──────────────────────────────────────────────────────────────────────────────

DeviceMonitorThread::DeviceMonitorThread(QObject* parent)
    : QThread(parent)
{}

void DeviceMonitorThread::stop()
{
    m_stop = true;
}

void DeviceMonitorThread::setHelperPath(const QString& path)
{
    QMutexLocker lk(&m_helperMutex);
    m_helperPath = path;
}

bool DeviceMonitorThread::isNxpDevice(quint16 vid)
{
    return vid == VID_NXP || vid == VID_FREESCALE;
}

// Parse "uuu-helper list" output: one JSON object per line, e.g.
//   {"event":"device","path":"1:12","chip":"MX865","pro":"SDPS:",
//    "vid":8137,"pid":326,"bcd":2,"serial":"221DB000727DE5AC"}
QList<UsbDevice> DeviceMonitorThread::parseHelperList(const QByteArray& output)
{
    QList<UsbDevice> devices;

    for (const QByteArray& rawLine : output.split('\n')) {
        const QByteArray line = rawLine.trimmed();
        if (line.isEmpty()) continue;

        QJsonParseError err{};
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;

        const QJsonObject obj = doc.object();
        if (obj.value("event").toString() != "device") continue;

        const QString path = obj.value("path").toString();
        if (path.split(':').size() != 2) continue;   // expect "bus:addr"

        quint16 vid = static_cast<quint16>(obj.value("vid").toInt());
        if (!isNxpDevice(vid)) continue;

        UsbDevice dev;
        dev.busId        = path;
        dev.vendorId     = vid;
        dev.productId    = static_cast<quint16>(obj.value("pid").toInt());
        dev.serialNumber = obj.value("serial").toString();

        devices << dev;
    }

    return devices;
}

QMap<QString, UsbDevice> DeviceMonitorThread::scanViaHelper()
{
    QString helperPath;
    {
        QMutexLocker lk(&m_helperMutex);
        helperPath = m_helperPath;
    }

    if (helperPath.isEmpty()) return {};

    QProcess proc;
    // Always list without elevation — enumerating devices doesn't need root, and
    // sudo without a terminal would block waiting for a password.
    proc.setProgram(helperPath);
    proc.setArguments({"list"});
    proc.start();
    if (!proc.waitForFinished(3000)) {
        proc.kill();
        return {};
    }

    QMap<QString, UsbDevice> result;
    const QByteArray output = proc.readAllStandardOutput();
    for (const UsbDevice& dev : parseHelperList(output))
        result[dev.busId] = dev;
    return result;
}

QMap<QString, UsbDevice> DeviceMonitorThread::scanViaLibusb()
{
    QMap<QString, UsbDevice> result;
#ifndef HAVE_LIBUSB
    return result;
#else
    libusb_context* ctx = nullptr;
    if (libusb_init(&ctx) < 0) return result;

    libusb_device** list = nullptr;
    ssize_t count = libusb_get_device_list(ctx, &list);

    for (ssize_t i = 0; i < count; ++i) {
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(list[i], &desc) != 0) continue;
        if (!isNxpDevice(desc.idVendor)) continue;

        quint8 bus  = libusb_get_bus_number(list[i]);
        quint8 addr = libusb_get_device_address(list[i]);
        QString id  = QString("%1:%2").arg(bus).arg(addr);

        UsbDevice dev;
        dev.busId      = id;
        dev.vendorId   = desc.idVendor;
        dev.productId  = desc.idProduct;

        libusb_device_handle* handle = nullptr;
        if (libusb_open(list[i], &handle) == 0) {
            unsigned char buf[256] = {};
            if (desc.iProduct &&
                libusb_get_string_descriptor_ascii(handle, desc.iProduct, buf, sizeof(buf)) > 0)
                dev.product = QString::fromLatin1(reinterpret_cast<char*>(buf));
            if (desc.iSerialNumber &&
                libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, buf, sizeof(buf)) > 0)
                dev.serialNumber = QString::fromLatin1(reinterpret_cast<char*>(buf));
            libusb_close(handle);
        }

        result[id] = dev;
    }

    if (list) libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return result;
#endif
}

QSet<QString> DeviceMonitorThread::passiveNxpSet()
{
    QSet<QString> result;
#ifdef HAVE_LIBUSB
    libusb_context* ctx = nullptr;
    if (libusb_init(&ctx) < 0) return result;

    libusb_device** list = nullptr;
    ssize_t count = libusb_get_device_list(ctx, &list);

    for (ssize_t i = 0; i < count; ++i) {
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(list[i], &desc) != 0) continue;
        if (!isNxpDevice(desc.idVendor)) continue;
        result << QString("%1:%2").arg(libusb_get_bus_number(list[i]))
                                  .arg(libusb_get_device_address(list[i]));
    }

    if (list) libusb_free_device_list(list, 1);
    libusb_exit(ctx);
#endif
    return result;
}

void DeviceMonitorThread::run()
{
    QMap<QString, UsbDevice> known;
    bool helperModeActive  = false;
    bool noLibusbWarned    = false;
#ifdef HAVE_LIBUSB
    QSet<QString> lastPassive;
    bool passivePrimed = false;
#endif
    int cyclesSinceHelperScan = 0;

    while (!m_stop) {
        QString helperPath;
        { QMutexLocker lk(&m_helperMutex); helperPath = m_helperPath; }

        QMap<QString, UsbDevice> current;

        if (!helperPath.isEmpty()) {
            if (!helperModeActive) {
                helperModeActive = true;
                noLibusbWarned   = false;
                // Clear known so we don't emit spurious disconnects when switching modes
                known.clear();
#ifdef HAVE_LIBUSB
                passivePrimed = false;
#endif
            }
#ifdef HAVE_LIBUSB
            // Cheap passive pre-check: only spawn the helper when the set of
            // NXP devices changed, or every ~5 s as a reconciliation pass
            // (serials/paths can change without the passive set noticing).
            const QSet<QString> passive = passiveNxpSet();
            const bool changed = !passivePrimed || passive != lastPassive;
            lastPassive   = passive;
            passivePrimed = true;
            if (!changed && ++cyclesSinceHelperScan < 10) {
                msleep(500);
                continue;
            }
#endif
            cyclesSinceHelperScan = 0;
            current = scanViaHelper();
        } else {
            if (helperModeActive) {
                // Switching away from helper mode — let known devices emit disconnects below
                helperModeActive = false;
                known.clear();
            }
#ifdef HAVE_LIBUSB
            current = scanViaLibusb();
#else
            if (!noLibusbWarned) {
                emit monitoringUnavailable(
                    "The bundled uuu-helper was not found.\n"
                    "Alternatively, install libusb and rebuild:\n"
                    "  macOS:   brew install libusb\n"
                    "  Linux:   sudo apt install libusb-1.0-0-dev\n"
                    "  Windows: vcpkg install libusb");
                noLibusbWarned = true;
            }
            msleep(500);
            continue;
#endif
        }

        // Emit added
        for (auto it = current.cbegin(); it != current.cend(); ++it)
            if (!known.contains(it.key()))
                emit deviceConnected(it.value());

        // Emit removed
        for (auto it = known.cbegin(); it != known.cend(); ++it)
            if (!current.contains(it.key()))
                emit deviceDisconnected(it.key());

        known = current;
        msleep(500);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// DeviceMonitor (facade on the main thread)
// ──────────────────────────────────────────────────────────────────────────────

DeviceMonitor::DeviceMonitor(QObject* parent)
    : QObject(parent)
    , m_thread(new DeviceMonitorThread(this))
{
    connect(m_thread, &DeviceMonitorThread::deviceConnected,
            this,     &DeviceMonitor::deviceConnected);
    connect(m_thread, &DeviceMonitorThread::deviceDisconnected,
            this,     &DeviceMonitor::deviceDisconnected);
    connect(m_thread, &DeviceMonitorThread::monitoringUnavailable,
            this,     &DeviceMonitor::monitoringUnavailable);
}

DeviceMonitor::~DeviceMonitor()
{
    stop();
}

void DeviceMonitor::start()
{
    if (!m_thread->isRunning())
        m_thread->start();
}

void DeviceMonitor::stop()
{
    if (m_thread->isRunning()) {
        m_thread->stop();
        m_thread->wait(4000); // > 3000ms proc.waitForFinished in scanViaHelper
    }
}

void DeviceMonitor::setHelperPath(const QString& path)
{
    m_thread->setHelperPath(path);
}
