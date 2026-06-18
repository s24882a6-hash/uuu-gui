#pragma once
#include <QObject>
#include <QThread>
#include <QMap>
#include <QMutex>
#include <atomic>

struct UsbDevice {
    QString  busId;          // USB path e.g. "1:12" — exact value reported by libuuu
    quint16  vendorId  = 0;
    quint16  productId = 0;
    QString  product;
    QString  serialNumber;

    QString displayName() const;
};

// Polls "uuu-helper list" every 500 ms to detect NXP devices via libuuu.
// Uses the exact path strings libuuu reports. Falls back to libusb if the
// helper path is not configured.
class DeviceMonitorThread : public QThread
{
    Q_OBJECT
public:
    explicit DeviceMonitorThread(QObject* parent = nullptr);
    void stop();
    void setOpenAllowed(bool allowed) { m_openAllowed.store(allowed); }
    void setHelperPath(const QString& path);

    // Parse "uuu-helper list" JSON-line output into NXP device rows.
    static QList<UsbDevice> parseHelperList(const QByteArray& output);

signals:
    void deviceConnected(UsbDevice device);
    void deviceDisconnected(QString busId);
    void monitoringUnavailable(QString reason);

protected:
    void run() override;

private:
    QMap<QString, UsbDevice> scanViaHelper();
    QMap<QString, UsbDevice> scanViaLibusb();
    static bool isNxpDevice(quint16 vid);

    std::atomic<bool> m_stop        {false};
    std::atomic<bool> m_openAllowed {true};

    QMutex  m_helperMutex;
    QString m_helperPath;
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
    void setHelperPath(const QString& path);

signals:
    void deviceConnected(UsbDevice device);
    void deviceDisconnected(QString busId);
    void monitoringUnavailable(QString reason);

private:
    DeviceMonitorThread* m_thread = nullptr;
};
