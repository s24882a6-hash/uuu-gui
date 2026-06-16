#include "flashworker.h"
#include <QRegularExpression>
#include <QProcessEnvironment>
#include <QProcess>

static int estimateSteps(const FirmwarePreset& p)
{
    switch (p.type) {
    case FirmwarePreset::Type::SimpleBin:  return 4;
    case FirmwarePreset::Type::EmmcAll:    return 20;
    case FirmwarePreset::Type::EmmcAll4G:  return 24;
    }
    return 10;
}

FlashWorker::FlashWorker(QObject* parent)
    : QObject(parent)
{}

bool FlashWorker::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

QStringList FlashWorker::buildPhaseCommand(int phaseIndex) const
{
    QStringList cmd;
#ifndef Q_OS_WIN
    if (!m_sudoPrefix.isEmpty())
        cmd << m_sudoPrefix;
#endif
    cmd << m_uuuPath;
    if (!m_device.busId.isEmpty())
        cmd << "-m" << m_device.uuuDevArg();
    cmd << m_phases[phaseIndex];
    return cmd;
}

void FlashWorker::start()
{
    if (isRunning()) return;

    if (!m_process) {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::readyReadStandardOutput, this, &FlashWorker::onReadyRead);
        connect(m_process, &QProcess::readyReadStandardError,  this, &FlashWorker::onReadyRead);
        connect(m_process,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &FlashWorker::onFinished);
    }

    if (!m_phaseTimer) {
        m_phaseTimer = new QTimer(this);
        m_phaseTimer->setSingleShot(true);
        connect(m_phaseTimer, &QTimer::timeout, this, &FlashWorker::startCurrentPhase);
    }

    m_phases          = m_preset.buildAllPhases();
    if (m_rebootAfterFlash)
        m_phases << QStringList{"FB:", "reboot"};
    m_currentPhase    = 0;
    m_stepsDone       = 0;
    m_stepsTotal      = estimateSteps(m_preset);
    m_active            = true;
    m_lastWasCmd        = false;
    m_permissionError   = false;
    m_busIdScanRetries  = 0;

    startCurrentPhase();
}

// Scan uuu -lsusb and return the bus path of our device in Fastboot mode.
// Returns empty string if not found yet (device still transitioning).
QString FlashWorker::scanForFastbootBusId() const
{
    if (m_uuuPath.isEmpty() || m_device.serialNumber.isEmpty())
        return {};

    QProcess proc;
    proc.setProgram(m_uuuPath);
    proc.setArguments({"-lsusb"});
    proc.start();
    if (!proc.waitForFinished(3000)) {
        proc.kill();
        return {};
    }

    const QString output = QString::fromLocal8Bit(
        proc.readAllStandardOutput() + proc.readAllStandardError());

    static const QRegularExpression reSpaces(R"(\s+)");
    bool pastHeader = false;

    for (const QString& rawLine : output.split('\n')) {
        const QString line = rawLine.trimmed();
        if (line.startsWith("====")) { pastHeader = true; continue; }
        if (!pastHeader || line.isEmpty()) continue;
        if (!line.contains("FB:")) continue;  // only fastboot-mode devices

        const QStringList fields = line.split(reSpaces, Qt::SkipEmptyParts);
        if (fields.size() < 2 || !fields[0].contains(':')) continue;

        if (fields.contains(m_device.serialNumber))
            return fields[0];
    }
    return {};
}

void FlashWorker::startCurrentPhase()
{
    // For phase 2+, device re-enumerates after SDP boot and may get a new bus path.
    // Scan lsusb to find the new path by serial number before launching uuu.
    if (m_currentPhase > 0 && !m_device.serialNumber.isEmpty()) {
        QString newBusId = scanForFastbootBusId();
        if (newBusId.isEmpty()) {
            // Device not in fastboot yet — retry in 1 second
            if (m_busIdScanRetries < 15) {
                ++m_busIdScanRetries;
                m_phaseTimer->start(1000);
                return;
            }
            // Gave up — proceed with original bus ID; uuu will wait for it
            emit logLine(tr("Warning: device not found in fastboot mode after re-enumeration"));
        } else {
            if (newBusId != m_device.busId) {
                emit logLine(QString("Device re-enumerated: %1 → %2").arg(m_device.busId, newBusId));
                m_device.busId = newBusId;
            }
        }
        m_busIdScanRetries = 0;
    }

    QStringList fullCmd = buildPhaseCommand(m_currentPhase);
    QString program = fullCmd.takeFirst();

    emit phaseChanged(m_currentPhase + 1, m_phases.size());

    if (m_phases.size() > 1)
        emit logLine(QString("--- Phase %1/%2 ---").arg(m_currentPhase + 1).arg(m_phases.size()));

    emit logLine(QString("$ %1 %2").arg(program, fullCmd.join(' ')));

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("TERM", "dumb");
    m_process->setProcessEnvironment(env);
    m_process->start(program, fullCmd);

    if (!m_process->waitForStarted(3000)) {
        m_active = false;
        emit finished(false, QString("Failed to start: %1").arg(m_process->errorString()));
    }
}

void FlashWorker::startReboot()
{
    if (isRunning()) return;
    m_phases        = {{"FB:", "reboot"}};
    m_currentPhase  = 0;
    m_stepsDone     = 0;
    m_stepsTotal    = 1;
    m_active        = true;
    m_lastWasCmd    = false;

    if (!m_process) {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::readyReadStandardOutput, this, &FlashWorker::onReadyRead);
        connect(m_process, &QProcess::readyReadStandardError,  this, &FlashWorker::onReadyRead);
        connect(m_process,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &FlashWorker::onFinished);
    }

    QStringList cmd;
#ifndef Q_OS_WIN
    if (!m_sudoPrefix.isEmpty()) cmd << m_sudoPrefix;
#endif
    cmd << m_uuuPath;
    if (!m_device.busId.isEmpty())
        cmd << "-m" << m_device.uuuDevArg();
    cmd << "FB:" << "reboot";
    QString program = cmd.takeFirst();
    emit logLine(QString("$ %1 %2").arg(program, cmd.join(' ')));

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("TERM", "dumb");
    m_process->setProcessEnvironment(env);
    m_process->start(program, cmd);

    if (!m_process->waitForStarted(3000)) {
        m_active = false;
        emit finished(false, QString("Failed to start: %1").arg(m_process->errorString()));
    }
}

void FlashWorker::cancel()
{
    if (m_phaseTimer) m_phaseTimer->stop();
    if (m_process && isRunning()) {
        m_process->terminate();
        if (!m_process->waitForFinished(2000))
            m_process->kill();
    }
}

void FlashWorker::onReadyRead()
{
    QByteArray data = m_process->readAllStandardOutput()
                    + m_process->readAllStandardError();

    static const QRegularExpression reAnsi(R"(\x1B\[[0-9;]*[A-Za-z]|\x1B\[\?[0-9]+[lh])");
    static const QRegularExpression reEscape(R"(^\[[\d;]*[A-Za-z]$)");

    for (const QByteArray& part : data.split('\n')) {
        for (const QByteArray& sub : part.split('\r')) {
            QString line = QString::fromLocal8Bit(sub).trimmed();
            line.remove(reAnsi);
            if (line.isEmpty() || reEscape.match(line).hasMatch()) continue;
            emit logLine(line);
            parseLine(line);
        }
    }
}

void FlashWorker::parseLine(const QString& line)
{
    if (line.contains("Failure open usb device", Qt::CaseInsensitive) ||
        line.contains("Try sudo", Qt::CaseInsensitive))
    {
        m_permissionError = true;
        return;
    }

    // VT mode (Linux/macOS): "0:12  1/ 2 [  48%  ] SDPS: ..."
    static const QRegularExpression rePercent(R"(\[[^\]]*?(\d+)%[^\]]*?\])");
    QRegularExpressionMatch m = rePercent.match(line);
    if (m.hasMatch()) {
        emit progressChanged(qMin(99, m.captured(1).toInt()));
        return;
    }

    // Verbose mode (Windows): bare "27%" line
    static const QRegularExpression rePercentVerbose(R"(^(\d+)%$)");
    QRegularExpressionMatch m2 = rePercentVerbose.match(line);
    if (m2.hasMatch()) {
        emit progressChanged(qMin(99, m2.captured(1).toInt()));
        return;
    }

    if (line.contains("[Done", Qt::CaseInsensitive)) {
        emit progressChanged(99);
        return;
    }
}

void FlashWorker::onFinished(int exitCode, QProcess::ExitStatus status)
{
    bool isRebootPhase = m_rebootAfterFlash && (m_currentPhase == m_phases.size() - 1);
    bool phaseOk = (status == QProcess::NormalExit && exitCode == 0)
                || (isRebootPhase); // reboot cmd exits non-zero — treat as ok

    if (!phaseOk && m_permissionError) {
        m_active = false;
        emit permissionError();
        return;
    }

    if (!phaseOk) {
        m_active = false;
        QString errMsg = (status == QProcess::CrashExit)
            ? "uuu process crashed"
            : QString("uuu exited with code %1").arg(exitCode);
        emit finished(false, errMsg);
        return;
    }

    m_currentPhase++;
    if (m_currentPhase >= m_phases.size()) {
        m_active = false;
        emit progressChanged(100);
        emit finished(true, {});
        return;
    }

    // Delay before next phase (device needs time to re-enumerate)
    bool nextIsReboot = m_rebootAfterFlash && (m_currentPhase == m_phases.size() - 1);
    int delayMs = (nextIsReboot ? qMax(m_preset.phaseDelay, 3) : m_preset.phaseDelay) * 1000;

    if (m_phases.size() > 1 && m_currentPhase == 1 &&
        m_preset.type == FirmwarePreset::Type::EmmcAll4G)
    {
        emit logLine("Hint: if stuck here, reconnect USB cable to put device in SDP mode");
    }

    emit logLine(QString("Waiting %1s before next phase…").arg(delayMs / 1000));
    m_phaseTimer->start(delayMs);
}
