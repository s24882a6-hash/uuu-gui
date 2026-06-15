#include "flashworker.h"
#include <QRegularExpression>
#include <QProcessEnvironment>

static int estimateSteps(const FirmwarePreset& p)
{
    switch (p.type) {
    case FirmwarePreset::Type::SimpleBin:  return 4;
    case FirmwarePreset::Type::EmmcAll:    return 20;
    case FirmwarePreset::Type::EmmcAll4G:  return 24;
    }
    return 10;
}

// Shell-quote a single argument (single-quote wrapping, escape embedded single quotes)
static QString shQuote(const QString& s)
{
    return '\'' + QString(s).replace('\'', "'\\''") + '\'';
}

FlashWorker::FlashWorker(QObject* parent)
    : QObject(parent)
{}

bool FlashWorker::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

QStringList FlashWorker::buildCommand() const
{
    QStringList cmd;
#ifdef Q_OS_WIN
    cmd << m_uuuPath;
#else
    if (!m_sudoPrefix.isEmpty())
        cmd << m_sudoPrefix;
    cmd << m_uuuPath;
#endif
    if (!m_device.busId.isEmpty())
        cmd << "-m" << m_device.uuuDevArg();
    cmd << m_phases[0];
    return cmd;
}

QString FlashWorker::buildShellScript() const
{
    // Each uuu invocation gets the same privilege prefix as direct calls.
    // The outer bash process itself runs without elevation.
    QStringList lines;
    lines << "#!/bin/bash";
    lines << "set -e";

    for (int i = 0; i < m_phases.size(); ++i) {
        lines << QString("echo '--- Phase %1/%2 ---'").arg(i + 1).arg(m_phases.size());

        // After Phase 1 of a multi-phase preset the device re-enumerates.
        // If it doesn't appear automatically, reconnecting USB puts it in SDP mode
        // which the next phase (emmc_all) also handles.
        if (i == 1 && m_preset.type == FirmwarePreset::Type::EmmcAll4G)
            lines << "echo 'Hint: if stuck here, reconnect USB cable to put device in SDP mode'";

        QStringList cmd;
#ifndef Q_OS_WIN
        if (!m_sudoPrefix.isEmpty())
            cmd << m_sudoPrefix;
#endif
        cmd << shQuote(m_uuuPath);
        // Target the specific device on Phase 1. Phase 2+ of EmmcAll4G are skipped
        // because the device re-enumerates at a new address after Phase 1.
        if (i == 0 && !m_device.busId.isEmpty())
            cmd << "-m" << shQuote(m_device.uuuDevArg());
        for (const QString& arg : m_phases[i])
            cmd << shQuote(arg);

        // Reboot phase: device drops USB before fastboot ACKs — uuu always exits non-zero.
        // Append || true so set -e doesn't abort the script on expected disconnect.
        bool isReboot = m_rebootAfterFlash && (i == m_phases.size() - 1);
        lines << cmd.join(' ') + (isReboot ? " || true" : "");

        if (i < m_phases.size() - 1) {
            // Give extra time before reboot phase so USB buffers from previous phase drain.
            bool nextIsReboot = m_rebootAfterFlash && (i == m_phases.size() - 2);
            int delay = nextIsReboot ? qMax(m_preset.phaseDelay, 3) : m_preset.phaseDelay;
            lines << QString("sleep %1").arg(delay);
        }
    }

    return lines.join('\n') + '\n';
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

    m_phases          = m_preset.buildAllPhases();
    if (m_rebootAfterFlash)
        m_phases << QStringList{"FB:", "reboot"};
    m_stepsDone       = 0;
    m_stepsTotal      = estimateSteps(m_preset);
    m_active          = true;
    m_lastWasCmd      = false;
    m_permissionError = false;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("TERM", "dumb");
    m_process->setProcessEnvironment(env);

    if (m_phases.size() > 1) {
        // Multi-phase: write a temp shell script and run it under sudo bash.
        // This exactly replicates the manual two-command workflow.
        m_scriptFile.reset(new QTemporaryFile(this));
        m_scriptFile->setFileTemplate(
            QDir::tempPath() + "/uuuflash_XXXXXX.sh");

        if (!m_scriptFile->open()) {
            m_active = false;
            emit finished(false, "Failed to create temp script file");
            return;
        }
        m_scriptFile->write(buildShellScript().toUtf8());
        m_scriptFile->setPermissions(
            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        m_scriptFile->close();

        QString scriptPath = m_scriptFile->fileName();
        // Run bash directly (no outer sudo) — sudo is embedded per-command in the script
        QStringList args = { scriptPath };
        emit logLine(QString("$ bash %1").arg(scriptPath));
        m_process->start("bash", args);
    } else {
        QStringList fullCmd = buildCommand();
        QString program = fullCmd.takeFirst();
        emit logLine(QString("$ %1 %2").arg(program, fullCmd.join(' ')));
        m_process->start(program, fullCmd);
    }

    if (!m_process->waitForStarted(3000)) {
        m_active = false;
        emit finished(false, QString("Failed to start: %1").arg(m_process->errorString()));
    }
}

void FlashWorker::startReboot()
{
    if (isRunning()) return;
    m_phases     = {{"FB:", "reboot"}};
    m_stepsDone  = 0;
    m_stepsTotal = 1;
    m_active     = true;
    m_lastWasCmd = false;

    if (!m_process) {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::readyReadStandardOutput, this, &FlashWorker::onReadyRead);
        connect(m_process, &QProcess::readyReadStandardError,  this, &FlashWorker::onReadyRead);
        connect(m_process,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &FlashWorker::onFinished);
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("TERM", "dumb");
    m_process->setProcessEnvironment(env);

    // uuu FB: reboot — sends fastboot reboot to the device in U-Boot fastboot mode
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
    m_process->start(program, cmd);

    if (!m_process->waitForStarted(3000)) {
        m_active = false;
        emit finished(false, QString("Failed to start: %1").arg(m_process->errorString()));
    }
}

void FlashWorker::cancel()
{
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

    // Split by both \n and \r to handle TUI-style carriage-return progress lines
    QList<QByteArray> rawLines;
    for (const QByteArray& part : data.split('\n'))
        for (const QByteArray& sub : part.split('\r'))
            rawLines << sub;

    for (const QByteArray& rawLine : rawLines) {
        QString line = QString::fromLocal8Bit(rawLine).trimmed();
        line.remove(reAnsi);
        if (line.isEmpty() || reEscape.match(line).hasMatch()) continue;
        emit logLine(line);
        parseLine(line);
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
    // Progress from uuu: "0:12-xxx 1/ 2 [  48%  ] SDPS: ..."
    // Use [^\]]* to match any content inside brackets regardless of fill chars
    static const QRegularExpression rePercent(R"(\[[^\]]*?(\d+)%[^\]]*?\])");
    QRegularExpressionMatch m = rePercent.match(line);
    if (m.hasMatch()) {
        emit progressChanged(qMin(99, m.captured(1).toInt()));
        return;
    }

    if (line.contains("[Done", Qt::CaseInsensitive)) {
        emit progressChanged(99);
        return;
    }
}

void FlashWorker::onFinished(int exitCode, QProcess::ExitStatus status)
{
    m_active = false;
    m_scriptFile.reset();

    bool success = (status == QProcess::NormalExit && exitCode == 0);
    if (success) {
        emit progressChanged(100);
        emit finished(true, {});
    } else if (m_permissionError) {
        emit permissionError();
    } else {
        QString errMsg = (status == QProcess::CrashExit)
            ? "uuu process crashed"
            : QString("uuu exited with code %1").arg(exitCode);
        emit finished(false, errMsg);
    }
}
