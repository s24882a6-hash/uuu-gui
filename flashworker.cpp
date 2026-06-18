#include "flashworker.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

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
    // -S: read the password from stdin; -p "": suppress the prompt text.
    if (m_elevated)
        cmd << "sudo" << "-S" << "-p" << "";
#endif
    cmd << m_helperPath << "phase";

    // Prefer serial — it survives the SDP→fastboot re-enumeration, so the helper
    // (via libuuu) re-locates the board itself without us rescanning bus paths.
    if (!m_device.serialNumber.isEmpty())
        cmd << "--serial" << m_device.serialNumber;
    else if (!m_device.busId.isEmpty())
        cmd << "--path" << m_device.busId;

    cmd << m_phases[phaseIndex];
    return cmd;
}

void FlashWorker::ensureProcess()
{
    if (m_process) return;

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &FlashWorker::onReadyRead);
    connect(m_process, &QProcess::errorOccurred, this, &FlashWorker::onErrorOccurred);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &FlashWorker::onFinished);

    m_phaseTimer = new QTimer(this);
    m_phaseTimer->setSingleShot(true);
    connect(m_phaseTimer, &QTimer::timeout, this, &FlashWorker::launchCurrentPhase);
}

void FlashWorker::resetRunState()
{
    m_currentPhase    = 0;
    m_active          = true;
    m_permissionError = false;
    m_sudoAuthFailed  = false;
    m_lastError.clear();
    m_lineBuffer.clear();
}

void FlashWorker::start()
{
    if (isRunning()) return;
    ensureProcess();

    m_phases = m_preset.buildHelperPhases();
    if (m_rebootAfterFlash)
        m_phases << QStringList{"--cmd", "FB: reboot", "--besteffort"};
    resetRunState();

    launchCurrentPhase();
}

void FlashWorker::launchCurrentPhase()
{
    QStringList fullCmd = buildPhaseCommand(m_currentPhase);
    QString program = fullCmd.takeFirst();

    emit phaseChanged(m_currentPhase + 1, m_phases.size());

    if (m_phases.size() > 1)
        emit logLine(QString("--- Phase %1/%2 ---").arg(m_currentPhase + 1).arg(m_phases.size()));

    emit logLine(QString("$ %1 %2").arg(program, fullCmd.join(' ')));

    m_lineBuffer.clear();
    m_process->start(program, fullCmd);

    // Feed the password to `sudo -S`. Queued until the process starts; the helper
    // itself never reads stdin, so closing the channel afterwards is safe.
    if (m_elevated) {
        m_process->write((m_password + "\n").toUtf8());
        m_process->closeWriteChannel();
    }
    // errorOccurred fires if the process fails to start — no blocking wait needed
}

void FlashWorker::cancel()
{
    if (m_phaseTimer) m_phaseTimer->stop();
    if (!m_process || !isRunning()) return;
    m_process->terminate();
    // Kill forcefully after 2s if still running
    QTimer::singleShot(2000, m_process, [this]() {
        if (m_process && isRunning())
            m_process->kill();
    });
}

void FlashWorker::onErrorOccurred(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        m_active = false;
        emit finished(false, QString("Failed to start helper: %1").arg(m_process->errorString()));
    }
}

void FlashWorker::processLineBuffer()
{
    int pos;
    while ((pos = m_lineBuffer.indexOf('\n')) != -1) {
        QByteArray line = m_lineBuffer.left(pos);
        m_lineBuffer.remove(0, pos + 1);
        line = line.trimmed();
        if (!line.isEmpty())
            handleEvent(line);
    }
}

void FlashWorker::handleEvent(const QByteArray& jsonLine)
{
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(jsonLine, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        // Not a structured event — likely a sudo message (merged into stdout) or
        // raw helper text. Detect a failed sudo authentication so the caller can
        // re-prompt for the password.
        const QString text = QString::fromUtf8(jsonLine);
        if (m_elevated) {
            const QString low = text.toLower();
            if (low.contains("try again") || low.contains("incorrect password") ||
                low.contains("authentication failure") || low.startsWith("sorry"))
                m_sudoAuthFailed = true;
        }
        emit logLine(text);
        return;
    }

    const QJsonObject obj = doc.object();
    const QString event = obj.value("event").toString();

    if (event == "progress") {
        emit progressChanged(qBound(0, obj.value("pct").toInt(), 100));
    } else if (event == "log") {
        emit logLine(obj.value("msg").toString());
    } else if (event == "done") {
        if (!obj.value("success").toBool()) {
            m_lastError = obj.value("error").toString();
            if (obj.value("permission").toBool())
                m_permissionError = true;
        }
    }
    // "phase_start" / others: nothing to do — phase UI is driven by phaseChanged.
}

void FlashWorker::onReadyRead()
{
    m_lineBuffer += m_process->readAll();
    processLineBuffer();
}

void FlashWorker::onFinished(int exitCode, QProcess::ExitStatus status)
{
    // Flush any trailing partial line.
    if (!m_lineBuffer.isEmpty()) {
        m_lineBuffer.append('\n');
        processLineBuffer();
    }

    bool phaseOk = (status == QProcess::NormalExit && exitCode == 0);

    if (!phaseOk && m_elevated && m_sudoAuthFailed) {
        m_active = false;
        emit authFailed();
        return;
    }

    if (!phaseOk && (m_permissionError || exitCode == 2)) {
        m_active = false;
        emit permissionError();
        return;
    }

    if (!phaseOk) {
        m_active = false;
        QString errMsg = !m_lastError.isEmpty()
            ? m_lastError
            : (status == QProcess::CrashExit
                ? QStringLiteral("uuu-helper crashed")
                : QString("uuu-helper exited with code %1").arg(exitCode));
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

    // Delay before next phase (device needs time to re-enumerate).
    bool nextIsReboot = m_phases[m_currentPhase].contains("--cmd");
    int delayMs = (nextIsReboot ? qMax(m_preset.phaseDelay, 3) : m_preset.phaseDelay) * 1000;

    if (m_phases.size() > 1 && m_currentPhase == 1 &&
        m_preset.type == FirmwarePreset::Type::EmmcAll4G)
    {
        emit logLine("Hint: if stuck here, reconnect USB cable to put device in SDP mode");
    }

    emit logLine(QString("Waiting %1s before next phase…").arg(delayMs / 1000));
    m_phaseTimer->start(delayMs);
}
