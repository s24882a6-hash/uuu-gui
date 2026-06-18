#pragma once
#include "firmwarepreset.h"
#include "devicemonitor.h"
#include <QObject>
#include <QProcess>
#include <QTimer>


// Drives a flash by spawning the bundled `uuu-helper` (one process per phase).
// The helper links libuuu and emits structured JSON progress on stdout, so this
// class no longer scrapes human-readable text or tracks USB re-enumeration —
// libuuu re-finds the device by serial on its own.
class FlashWorker : public QObject
{
    Q_OBJECT
public:
    explicit FlashWorker(QObject* parent = nullptr);

    void setHelperPath(const QString& path)    { m_helperPath = path; }
    void setPreset(const FirmwarePreset& p)    { m_preset  = p;    }
    void setDevice(const UsbDevice& dev)       { m_device  = dev;  }
    void setRebootAfterFlash(bool reboot)      { m_rebootAfterFlash = reboot; }

    // Run the helper under `sudo -S`, feeding `password` to sudo's stdin.
    void setElevation(bool useSudo, const QString& password)
    { m_elevated = useSudo; m_password = password; }
    bool isElevated() const { return m_elevated; }

    void start();
    void cancel();

    bool     isRunning() const;
    bool     isActive()  const { return m_active; }

signals:
    void progressChanged(int percent);
    void phaseChanged(int current, int total);
    void logLine(QString line);
    void finished(bool success, QString errorMsg);
    void permissionError();   // device access denied (needs / not helped by sudo)
    void authFailed();        // sudo could not authenticate (wrong password)

private slots:
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onErrorOccurred(QProcess::ProcessError error);
    void launchCurrentPhase();

private:
    void        ensureProcess();
    void        resetRunState();
    QStringList buildPhaseCommand(int phaseIndex) const;
    void        processLineBuffer();
    void        handleEvent(const QByteArray& jsonLine);

    QString            m_helperPath;
    FirmwarePreset     m_preset;
    UsbDevice          m_device;

    bool               m_elevated          = false;
    QString            m_password;

    QProcess*          m_process           = nullptr;
    QTimer*            m_phaseTimer        = nullptr;
    QList<QStringList> m_phases;
    int                m_currentPhase      = 0;
    bool               m_active            = false;
    bool               m_rebootAfterFlash  = false;
    bool               m_permissionError   = false;
    bool               m_sudoAuthFailed    = false;
    QString            m_lastError;
    QByteArray         m_lineBuffer;
};
