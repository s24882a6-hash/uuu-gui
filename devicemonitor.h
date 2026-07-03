#pragma once
#include <QObject>
#include <QThread>
#include <QMap>
#include <QMutex>
#include <QSet>
#include <atomic>

struct UsbDevice {
    QString  busId;          // USB path e.g. "1:12" — exact value reported by libuuu
    quint16  vendorId  = 0;
    quint16  productId = 0;
    QString  product;
    QString  serialNumber;

    QString displayName() const;
};

// Detects NXP devices via libuuu ("uuu-helper list", exact path strings libuuu
// reports). When built with libusb, a cheap passive enumeration (no device
// opens, no process spawn) runs every 500 ms and the helper is only invoked
// when the NXP device set actually changes (plus a periodic reconciliation).
// Falls back to pure libusb scanning if the helper path is not configured.
class DeviceMonitorThread : public QThread
{
    Q_OBJECT
public:
    explicit DeviceMonitorThread(QObject* parent = nullptr);
    void stop();
    void setHelperPath(const QString& path);

    // Parse "uuu-helper list" JSON-line output into NXP device rows.
    static QList<UsbDevice> parseHelperList(const QByteArray& output);

signals:
    void deviceConnected(UsbDevice device);
    void deviceDisconnected(QString busId);
    void monitoringUnavailable(QString reason);
    void helperDiagnostic(QString text);

protected:
    void run() override;

private:
    QMap<QString, UsbDevice> scanViaHelper();
    QMap<QString, UsbDevice> scanViaLibusb();
    // Passive libusb enumeration: "bus:addr" keys of NXP devices, no opens.
    static QSet<QString> passiveNxpSet();
    static bool isNxpDevice(quint16 vid);

    std::atomic<bool> m_stop {false};

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
    void setHelperPath(const QString& path);

signals:
    void deviceConnected(UsbDevice device);
    void deviceDisconnected(QString busId);
    void monitoringUnavailable(QString reason);
    void helperDiagnostic(QString text);

private:
    DeviceMonitorThread* m_thread = nullptr;
};
