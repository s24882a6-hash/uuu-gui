#pragma once
#include "devicemonitor.h"
#include "firmwarepreset.h"
#include <QWidget>
#include <QElapsedTimer>
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
    // True shortly after a flash ended — the device is likely re-enumerating.
    bool recentlyFinishedFlash() const;

    void flash(const QString& helperPath,
               const FirmwarePreset& preset,
               bool rebootAfter = false,
               const QString& password = {});   // empty = run unprivileged
    void cancelFlash();

    // Re-run the current flash; a non-empty `password` runs it via sudo,
    // an empty one retries unprivileged (e.g. after installing a udev rule).
    void retryFlash(const QString& password);
    bool isElevated() const;
    // Abort with an error message (e.g. when the user cancels the password prompt).
    void abortFlash(const QString& message);

signals:
    void flashRequested();
    void flashDone(bool success);
    void permissionDenied();   // device access denied — caller may elevate
    void authFailed();         // sudo password was wrong

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void onProgress(int pct);
    void onPhaseChanged(int current, int total);
    void onLogLine(const QString& line);
    void onFlashFinished(bool success, const QString& err);
    void showLog();

private:
    void setFlashingState(bool active);
    void retranslateUi();
    void ensureWorker();

    UsbDevice       m_device;
    QString         m_helperPath;
    FlashWorker*    m_worker    = nullptr;
    LogDialog*      m_logDialog = nullptr;

    QFile           m_logFile;
    QTextStream     m_logStream;
    QElapsedTimer   m_finishTimer;   // started when a flash ends (see recentlyFinishedFlash)

    QCheckBox*   m_check     = nullptr;
    QLabel*      m_lblName   = nullptr;
    QLabel*      m_lblStatus = nullptr;
    QLabel*      m_lblPhase  = nullptr;
    QProgressBar* m_bar      = nullptr;
    QLabel*      m_lblPct    = nullptr;
    QPushButton* m_btnFlash  = nullptr;
    QPushButton* m_btnLog    = nullptr;
};
