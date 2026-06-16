#pragma once
#include "devicemonitor.h"
#include "firmwarepreset.h"
#include <QWidget>
#include <QFile>
#include <QTextStream>

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class FlashWorker;
class LogDialog;

class DeviceItemWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DeviceItemWidget(const UsbDevice& device, QWidget* parent = nullptr);
    ~DeviceItemWidget();

    const UsbDevice& device() const { return m_device; }
    bool isChecked() const;
    bool isFlashing() const;

    void flash(const QString& uuuPath,
               const FirmwarePreset& preset,
               const QString& sudoPrefix,
               bool rebootAfter = false);
    void reboot(const QString& uuuPath, const QString& sudoPrefix);
    void cancelFlash();

signals:
    void checkStateChanged();
    void flashRequested();
    void flashDone(bool success);

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onProgress(int pct);
    void onPhaseChanged(int current, int total);
    void onLogLine(const QString& line);
    void onFlashFinished(bool success, const QString& err);
    void showLog();
#ifdef Q_OS_LINUX
    void onPermissionError();
#endif

private:
    void setFlashingState(bool active);
    void retranslateUi();

    UsbDevice       m_device;
    FlashWorker*    m_worker    = nullptr;
    LogDialog*      m_logDialog = nullptr;

    QFile           m_logFile;
    QTextStream     m_logStream;

    QString        m_lastUuuPath;
    FirmwarePreset m_lastPreset;
    QString        m_lastSudoPrefix;
    bool           m_lastRebootAfter = false;

    QCheckBox*   m_check     = nullptr;
    QLabel*      m_lblName   = nullptr;
    QLabel*      m_lblStatus = nullptr;
    QLabel*      m_lblPhase  = nullptr;
    QProgressBar* m_bar      = nullptr;
    QLabel*      m_lblPct    = nullptr;
    QPushButton* m_btnFlash  = nullptr;
    QPushButton* m_btnLog    = nullptr;
};
