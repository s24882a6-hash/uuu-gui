#pragma once
#include <QObject>
#include <QThread>
#include <QMap>
#include <QMutex>
#include <atomic>

struct UsbDevice {
    QString  busId;          // uuu path e.g. "1:12" — exact value from uuu -lsusb
    quint16  vendorId  = 0;
    quint16  productId = 0;
    quint8   busNumber = 0;
    quint8   devAddress = 0;
    QString  manufacturer;
    QString  product;
    QString  serialNumber;

    QString displayName() const;
    QString uuuDevArg()   const;  // returns busId — used as -m argument for uuu
};

// Polls "uuu -lsusb" every 500 ms to detect NXP devices.
// Uses the exact path strings uuu reports, so -m flags always match.
// Falls back to libusb if uuu path is not configured.
class DeviceMonitorThread : public QThread
{
    Q_OBJECT
public:
    explicit DeviceMonitorThread(QObject* parent = nullptr);
    void stop();
    void setOpenAllowed(bool allowed) { m_openAllowed.store(allowed); }
    void setUuuPath(const QString& path, const QString& sudoPrefix);

signals:
    void deviceConnected(UsbDevice device);
    void deviceDisconnected(QString busId);
    void monitoringUnavailable(QString reason);

protected:
    void run() override;

private:
    QMap<QString, UsbDevice> scanViaUuu();
    QMap<QString, UsbDevice> scanViaLibusb();
    static bool isNxpDevice(quint16 vid);

    std::atomic<bool> m_stop        {false};
    std::atomic<bool> m_openAllowed {true};

    QMutex  m_uuuMutex;
    QString m_uuuPath;
    QString m_sudoPrefix;
};

class DeviceMonitor : public QObject
{
    Q_OBJECT
public:
    explicit DeviceMonitor(QObject* parent = nullptr);
    ~DeviceMonitor();

    void start();
    void stop();
    void setOpenAllowed(bool allowed);
    void setUuuPath(const QString& path, const QString& sudoPrefix);

signals:
    void deviceConnected(UsbDevice device);
    void deviceDisconnected(QString busId);
    void monitoringUnavailable(QString reason);

private:
    DeviceMonitorThread* m_thread = nullptr;
};
