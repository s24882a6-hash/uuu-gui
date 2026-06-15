#include "devicemonitor.h"
#include <QMap>
#include <QProcess>
#include <QRegularExpression>

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

QString UsbDevice::uuuDevArg() const
{
    // busId is the exact path from "uuu -lsusb", e.g. "1:12"
    return busId;
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

void DeviceMonitorThread::setUuuPath(const QString& path, const QString& sudoPrefix)
{
    QMutexLocker lk(&m_uuuMutex);
    m_uuuPath    = path;
    m_sudoPrefix = sudoPrefix;
}

bool DeviceMonitorThread::isNxpDevice(quint16 vid)
{
    return vid == VID_NXP || vid == VID_FREESCALE;
}

// Parse "uuu -lsusb" output into a device map.
// Output format (columns are whitespace-separated after initial indent):
//   Path   Chip   Protocol   Vid      Pid      BcdVersion   Serial_no
//   1:12   MX865  SDPS:      0x1FC9   0x0146   0x0002       221DB000727DE5AC
QMap<QString, UsbDevice> DeviceMonitorThread::scanViaUuu()
{
    QMap<QString, UsbDevice> result;

    QString uuuPath, sudoPrefix;
    {
        QMutexLocker lk(&m_uuuMutex);
        uuuPath    = m_uuuPath;
        sudoPrefix = m_sudoPrefix;
    }

    if (uuuPath.isEmpty()) return result;

    QProcess proc;
    QString program;
    QStringList args;
#ifdef Q_OS_WIN
    program = uuuPath;
    args << "-lsusb";
#else
    if (!sudoPrefix.isEmpty()) {
        program = sudoPrefix;
        args << uuuPath << "-lsusb";
    } else {
        program = uuuPath;
        args << "-lsusb";
    }
#endif
    proc.start(program, args);
    if (!proc.waitForFinished(3000)) {
        proc.kill();
        return result;
    }

    const QString output = QString::fromLocal8Bit(
        proc.readAllStandardOutput() + proc.readAllStandardError());

    bool pastHeader = false;
    static const QRegularExpression reSpaces(R"(\s+)");

    for (const QString& rawLine : output.split('\n')) {
        const QString line = rawLine.trimmed();

        // The separator line marks the start of actual device rows
        if (line.startsWith("====")) { pastHeader = true; continue; }
        if (!pastHeader || line.isEmpty()) continue;

        // Each device line: "path chip proto vid pid bcd serial"
        QStringList fields = line.split(reSpaces, Qt::SkipEmptyParts);
        if (fields.size() < 5) continue;

        // Path must look like "N:M"
        const QString& path = fields[0];
        if (!path.contains(':')) continue;

        bool ok;
        quint16 vid = fields[3].toUInt(&ok, 16); if (!ok) continue;
        quint16 pid = fields[4].toUInt(&ok, 16); if (!ok) continue;
        if (!isNxpDevice(vid)) continue;

        QStringList parts = path.split(':');
        if (parts.size() != 2) continue;

        UsbDevice dev;
        dev.busId      = path;
        dev.vendorId   = vid;
        dev.productId  = pid;
        dev.busNumber  = static_cast<quint8>(parts[0].toUInt());
        dev.devAddress = static_cast<quint8>(parts[1].toUInt());
        if (fields.size() > 6) dev.serialNumber = fields[6];

        result[dev.busId] = dev;
    }

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
        dev.busNumber  = bus;
        dev.devAddress = addr;

        libusb_device_handle* handle = nullptr;
        if (m_openAllowed.load() && libusb_open(list[i], &handle) == 0) {
            unsigned char buf[256] = {};
            if (desc.iManufacturer &&
                libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, buf, sizeof(buf)) > 0)
                dev.manufacturer = QString::fromLatin1(reinterpret_cast<char*>(buf));
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
    bool uuuModeActive = false;

    while (!m_stop) {
        QString uuuPath;
        { QMutexLocker lk(&m_uuuMutex); uuuPath = m_uuuPath; }

        QMap<QString, UsbDevice> current;

        if (!uuuPath.isEmpty()) {
            // Skip scanning while flashing — uuu has exclusive device access
            if (!m_openAllowed.load()) {
                msleep(500);
                continue;
            }
            current = scanViaUuu();
            if (!uuuModeActive) {
                uuuModeActive = true;
                // Clear known so we don't emit spurious disconnects when switching modes
                known.clear();
            }
        } else {
#ifdef HAVE_LIBUSB
            current = scanViaLibusb();
#else
            if (!uuuModeActive) {
                emit monitoringUnavailable(
                    "Set a uuu binary path to enable device detection.\n"
                    "Alternatively, install libusb and rebuild:\n"
                    "  macOS:   brew install libusb\n"
                    "  Linux:   sudo apt install libusb-1.0-0-dev\n"
                    "  Windows: vcpkg install libusb");
                uuuModeActive = true; // suppress repeated emissions
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
        m_thread->wait(2000);
    }
}

void DeviceMonitor::setOpenAllowed(bool allowed)
{
    m_thread->setOpenAllowed(allowed);
}

void DeviceMonitor::setUuuPath(const QString& path, const QString& sudoPrefix)
{
    m_thread->setUuuPath(path, sudoPrefix);
}
