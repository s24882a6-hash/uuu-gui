#pragma once
#include "firmwarepreset.h"
#include "devicemonitor.h"
#include <QObject>
#include <QProcess>
#include <QTimer>

class FlashWorker : public QObject
{
    Q_OBJECT
public:
    explicit FlashWorker(QObject* parent = nullptr);

    void setUuuPath(const QString& path)       { m_uuuPath = path; }
    void setPreset(const FirmwarePreset& p)    { m_preset  = p;    }
    void setDevice(const UsbDevice& dev)       { m_device  = dev;  }
    void setPrivilegePrefix(const QString& p)  { m_sudoPrefix = p; }
    void setRebootAfterFlash(bool reboot)      { m_rebootAfterFlash = reboot; }

    void start();
    void startReboot();
    void cancel();

    bool     isRunning() const;
    bool     isActive()  const { return m_active; }
    QString  deviceBusId() const { return m_device.busId; }

signals:
    void progressChanged(int percent);
    void logLine(QString line);
    void finished(bool success, QString errorMsg);
    void permissionError();

private slots:
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void startCurrentPhase();

private:
    QStringList buildPhaseCommand(int phaseIndex) const;
    void parseLine(const QString& line);
    void emitScaledProgress(int phasePct);

    QString            m_uuuPath;
    FirmwarePreset     m_preset;
    UsbDevice          m_device;
    QString            m_sudoPrefix;

    QProcess*          m_process           = nullptr;
    QTimer*            m_phaseTimer        = nullptr;
    QList<QStringList> m_phases;
    int                m_currentPhase      = 0;
    bool               m_active            = false;
    bool               m_rebootAfterFlash  = false;
    bool               m_permissionError   = false;
    bool               m_holdsSharedLock   = false;
    int                m_stepsDone         = 0;
    int                m_stepsTotal        = 0;
    bool               m_lastWasCmd        = false;
};
