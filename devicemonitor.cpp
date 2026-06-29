#include "devicemonitor.h"
#include <QMap>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QTextStream>

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

static void appendHelperLog(const QString& text)
{
    QString logPath = QCoreApplication::applicationDirPath() + "/uuu-helper-debug.log";
    QFile f(logPath);
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;
    QTextStream out(&f);
    out << "[" << QDateTime::currentDateTime().toString(Qt::ISODate) << "] " << text << "\n";
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
    proc.setProgram(helperPath);
    proc.setArguments({"list"});
    proc.start();

    const bool finished = proc.waitForFinished(3000);

    const QByteArray errBytes = proc.readAllStandardError();
    if (!errBytes.isEmpty()) {
        QString errText = QString::fromLocal8Bit(errBytes).trimmed();
        appendHelperLog(errText);
        emit helperDiagnostic(errText);
    }

    if (!finished) {
        QString msg = QString("uuu-helper timed out or failed to start (state=%1 error=%2)")
                          .arg(proc.state()).arg(proc.errorString());
        appendHelperLog(msg);
        emit helperDiagnostic(msg);
        proc.kill();
        return {};
    }

    QMap<QString, UsbDevice> result;
    const QByteArray output = proc.readAllStandardOutput();

    if (!output.isEmpty()) {
        appendHelperLog("stdout: " + QString::fromUtf8(output).trimmed());
    } else {
        appendHelperLog("stdout: (empty)");
    }

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
        if (m_openAllowed.load() && libusb_open(list[i], &handle) == 0) {
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

void DeviceMonitorThread::run()
{
    QMap<QString, UsbDevice> known;
    bool helperModeActive  = false;
    bool noLibusbWarned    = false;

    while (!m_stop) {
        QString helperPath;
        { QMutexLocker lk(&m_helperMutex); helperPath = m_helperPath; }

        QMap<QString, UsbDevice> current;

        if (!helperPath.isEmpty()) {
            // Skip scanning while flashing — the helper has exclusive device access
            if (!m_openAllowed.load()) {
                msleep(500);
                continue;
            }
            current = scanViaHelper();
            if (!helperModeActive) {
                helperModeActive = true;
                noLibusbWarned   = false;
                // Clear known so we don't emit spurious disconnects when switching modes
                known.clear();
            }
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
    connect(m_thread, &DeviceMonitorThread::helperDiagnostic,
            this,     &DeviceMonitor::helperDiagnostic);
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

void DeviceMonitor::setOpenAllowed(bool allowed)
{
    m_thread->setOpenAllowed(allowed);
}

void DeviceMonitor::setHelperPath(const QString& path)
{
    m_thread->setHelperPath(path);
}
