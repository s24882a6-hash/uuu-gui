#include "deviceitemwidget.h"
#include "flashworker.h"
#include "logdialog.h"
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QFrame>
#include <QSettings>

DeviceItemWidget::DeviceItemWidget(const UsbDevice& device, QWidget* parent)
    : QWidget(parent)
    , m_device(device)
{
    setAutoFillBackground(true);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(8);

    m_check = new QCheckBox(this);
    m_check->setChecked(true);
    layout->addWidget(m_check);

    // Device icon placeholder + name
    m_lblName = new QLabel(device.displayName(), this);
    m_lblName->setMinimumWidth(0);
    m_lblName->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_lblName, 1);

    m_lblStatus = new QLabel(tr("Idle"), this);
    m_lblStatus->setFixedWidth(80);
    layout->addWidget(m_lblStatus);

    m_lblPhase = new QLabel(this);
    m_lblPhase->setFixedWidth(30);
    m_lblPhase->setAlignment(Qt::AlignCenter);
    m_lblPhase->setStyleSheet("color: gray; font-size: 10px;");
    m_lblPhase->setVisible(false);
    layout->addWidget(m_lblPhase);

    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 100);
    m_bar->setValue(0);
    m_bar->setTextVisible(false);
    m_bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_bar->setMinimumWidth(100);
    layout->addWidget(m_bar);

    m_lblPct = new QLabel("0%", this);
    m_lblPct->setFixedWidth(38);
    m_lblPct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(m_lblPct);

    m_btnFlash = new QPushButton(tr("Flash"), this);
    m_btnFlash->setFixedWidth(72);
    layout->addWidget(m_btnFlash);

    m_btnLog = new QPushButton(tr("Logs"), this);
    m_btnLog->setFixedWidth(60);
    layout->addWidget(m_btnLog);

    setObjectName("deviceItem");
    setStyleSheet("#deviceItem { border-bottom: 1px solid palette(mid); }");

    connect(m_btnLog,   &QPushButton::clicked,         this, &DeviceItemWidget::showLog);
    connect(m_btnFlash, &QPushButton::clicked, this, [this]() {
        if (isFlashing())
            cancelFlash();
        else
            emit flashRequested();
    });
}

DeviceItemWidget::~DeviceItemWidget()
{
    if (m_logFile.isOpen()) {
        m_logStream.flush();
        m_logFile.close();
    }
}

bool DeviceItemWidget::isChecked() const
{
    return m_check->isChecked();
}

bool DeviceItemWidget::isFlashing() const
{
    return m_worker && m_worker->isActive();
}

void DeviceItemWidget::ensureWorker()
{
    if (m_worker) return;
    m_worker = new FlashWorker(this);
    connect(m_worker, &FlashWorker::progressChanged, this, &DeviceItemWidget::onProgress);
    connect(m_worker, &FlashWorker::phaseChanged,    this, &DeviceItemWidget::onPhaseChanged);
    connect(m_worker, &FlashWorker::logLine,         this, &DeviceItemWidget::onLogLine);
    connect(m_worker, &FlashWorker::finished,        this, &DeviceItemWidget::onFlashFinished);
    // Surface elevation needs to the main window, which owns the session password.
    connect(m_worker, &FlashWorker::permissionError, this, &DeviceItemWidget::permissionDenied);
    connect(m_worker, &FlashWorker::authFailed,      this, &DeviceItemWidget::authFailed);
}

void DeviceItemWidget::flash(const QString& helperPath,
                              const FirmwarePreset& preset,
                              bool rebootAfter,
                              const QString& password)
{
    if (isFlashing()) return;

    ensureWorker();
    m_helperPath = helperPath;
    m_worker->setHelperPath(helperPath);
    m_worker->setPreset(preset);
    m_worker->setDevice(m_device);
    m_worker->setRebootAfterFlash(rebootAfter);
    m_worker->setElevation(!password.isEmpty(), password);

    if (!m_logDialog)
        m_logDialog = new LogDialog(m_device.displayName(), this);
    else
        m_logDialog->setStatus(tr("Flashing…"));

    // Open log file if enabled in settings
    if (m_logFile.isOpen()) {
        m_logStream.flush();
        m_logFile.close();
    }
    QSettings s;
    if (s.value("saveLogs", false).toBool()) {
        QString dir = s.value("logDir").toString();
        if (!dir.isEmpty() && QDir().mkpath(dir)) {
            QString deviceId = m_device.serialNumber.isEmpty()
                ? m_device.busId.replace(':', '-')
                : m_device.serialNumber;
            QString ts   = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
            QString name = QString("flash_%1_%2.log").arg(deviceId, ts);
            m_logFile.setFileName(QDir(dir).filePath(name));
            if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                m_logStream.setDevice(&m_logFile);
                m_logStream.setEncoding(QStringConverter::Utf8);
                m_logStream << "=== Flash log: " << m_device.displayName()
                            << "  " << QDateTime::currentDateTime().toString(Qt::ISODate)
                            << " ===\n";
            }
        }
    }

    setFlashingState(true);
    m_worker->start();
}

void DeviceItemWidget::cancelFlash()
{
    if (m_worker) m_worker->cancel();
}

void DeviceItemWidget::retryElevated(const QString& password)
{
    if (!m_worker) return;
    if (m_logDialog) m_logDialog->appendLine(tr("Retrying with administrator privileges…"));
    m_worker->setElevation(true, password);
    setFlashingState(true);
    m_worker->start();
}

bool DeviceItemWidget::isElevated() const
{
    return m_worker && m_worker->isElevated();
}

void DeviceItemWidget::abortFlash(const QString& message)
{
    if (m_logFile.isOpen()) {
        m_logStream << "\n=== FAILED: " << message << " ===\n";
        m_logStream.flush();
        m_logFile.close();
    }
    setFlashingState(false);
    m_lblStatus->setText(tr("Error"));
    m_lblStatus->setStyleSheet("color: red; font-weight: bold;");
    if (m_logDialog) {
        m_logDialog->appendLine(QString("\n[ERROR] %1").arg(message));
        m_logDialog->setStatus(tr("Error"));
    }
    emit flashDone(false);
}

void DeviceItemWidget::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

void DeviceItemWidget::retranslateUi()
{
    m_btnLog->setText(tr("Logs"));
    // Re-apply flashing state so buttons/labels get correct translated text
    setFlashingState(isFlashing());
}

void DeviceItemWidget::onProgress(int pct)
{
    m_bar->setValue(pct);
    m_lblPct->setText(QString("%1%").arg(pct));
}

void DeviceItemWidget::onPhaseChanged(int current, int total)
{
    m_bar->setValue(0);
    m_lblPct->setText("0%");
    if (total > 1) {
        m_lblPhase->setText(QString("%1/%2").arg(current).arg(total));
        m_lblPhase->setVisible(true);
    } else {
        m_lblPhase->setVisible(false);
    }
}

void DeviceItemWidget::onLogLine(const QString& line)
{
    if (m_logDialog)
        m_logDialog->appendLine(line);
    if (m_logFile.isOpen())
        m_logStream << line << '\n';
}

void DeviceItemWidget::onFlashFinished(bool success, const QString& err)
{
    if (m_logFile.isOpen()) {
        m_logStream << (success ? "\n=== DONE ===" : QString("\n=== FAILED: %1 ===").arg(err)) << '\n';
        m_logStream.flush();
        m_logFile.close();
    }

    setFlashingState(false);
    emit flashDone(success);

    if (success) {
        m_lblStatus->setText(tr("Done"));
        m_lblStatus->setStyleSheet("color: green; font-weight: bold;");
        m_bar->setValue(100);
        m_lblPct->setText("100%");
        if (m_logDialog) m_logDialog->setStatus(tr("Done"));
    } else {
        m_lblStatus->setText(tr("Error"));
        m_lblStatus->setStyleSheet("color: red; font-weight: bold;");
        if (m_logDialog) {
            m_logDialog->appendLine(QString("\n[ERROR] %1").arg(err));
            m_logDialog->setStatus(tr("Error"));
        }
    }
}

void DeviceItemWidget::showLog()
{
    if (!m_logDialog)
        m_logDialog = new LogDialog(m_device.displayName(), this);
    m_logDialog->show();
    m_logDialog->raise();
    m_logDialog->activateWindow();
}

void DeviceItemWidget::setFlashingState(bool active)
{
    m_lblStatus->setText(active ? tr("Flashing…") : tr("Idle"));
    m_lblStatus->setStyleSheet(active ? "color: orange; font-weight: bold;" : "");
    m_btnFlash->setText(active ? tr("Cancel") : tr("Flash"));
    if (!active) { m_bar->setValue(0); m_lblPct->setText("0%"); m_lblPhase->setVisible(false); }
}
