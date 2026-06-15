#include "deviceitemwidget.h"
#include "flashworker.h"
#include "logdialog.h"
#include <QHBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QFrame>

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

    m_lblStatus = new QLabel("Idle", this);
    m_lblStatus->setFixedWidth(80);
    layout->addWidget(m_lblStatus);

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

    m_btnFlash = new QPushButton("Flash", this);
    m_btnFlash->setFixedWidth(72);
    layout->addWidget(m_btnFlash);

    m_btnLog = new QPushButton("Logs", this);
    m_btnLog->setFixedWidth(60);
    layout->addWidget(m_btnLog);

    setObjectName("deviceItem");
    setStyleSheet("#deviceItem { border-bottom: 1px solid palette(mid); }");

    connect(m_check,    &QCheckBox::stateChanged, this, [this](int) { emit checkStateChanged(); });
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
    delete m_logDialog;
}

bool DeviceItemWidget::isChecked() const
{
    return m_check->isChecked();
}

bool DeviceItemWidget::isFlashing() const
{
    return m_worker && m_worker->isActive();
}

void DeviceItemWidget::flash(const QString& uuuPath,
                              const FirmwarePreset& preset,
                              const QString& sudoPrefix,
                              bool rebootAfter)
{
    if (isFlashing()) return;

    m_lastUuuPath     = uuuPath;
    m_lastPreset      = preset;
    m_lastSudoPrefix  = sudoPrefix;
    m_lastRebootAfter = rebootAfter;

    if (!m_worker) {
        m_worker = new FlashWorker(this);
        connect(m_worker, &FlashWorker::progressChanged, this, &DeviceItemWidget::onProgress);
        connect(m_worker, &FlashWorker::logLine,         this, &DeviceItemWidget::onLogLine);
        connect(m_worker, &FlashWorker::finished,        this, &DeviceItemWidget::onFlashFinished);
#ifdef Q_OS_LINUX
        connect(m_worker, &FlashWorker::permissionError, this, &DeviceItemWidget::onPermissionError);
#endif
    }

    m_worker->setUuuPath(uuuPath);
    m_worker->setPreset(preset);
    m_worker->setDevice(m_device);
    m_worker->setPrivilegePrefix(sudoPrefix);
    m_worker->setRebootAfterFlash(rebootAfter);

    if (!m_logDialog)
        m_logDialog = new LogDialog(m_device.displayName(), this);
    else
        m_logDialog->setStatus("Flashing…");

    setFlashingState(true);
    m_worker->start();
}

void DeviceItemWidget::reboot(const QString& uuuPath, const QString& sudoPrefix)
{
    if (isFlashing()) return;

    if (!m_worker) {
        m_worker = new FlashWorker(this);
        connect(m_worker, &FlashWorker::progressChanged, this, &DeviceItemWidget::onProgress);
        connect(m_worker, &FlashWorker::logLine,         this, &DeviceItemWidget::onLogLine);
        connect(m_worker, &FlashWorker::finished,        this, &DeviceItemWidget::onFlashFinished);
    }

    m_worker->setUuuPath(uuuPath);
    m_worker->setDevice(m_device);
    m_worker->setPrivilegePrefix(sudoPrefix);

    if (!m_logDialog)
        m_logDialog = new LogDialog(m_device.displayName(), this);
    else
        m_logDialog->setStatus("Rebooting…");

    setFlashingState(true);
    m_lblStatus->setText("Rebooting…");
    m_worker->startReboot();
}

void DeviceItemWidget::cancelFlash()
{
    if (m_worker) m_worker->cancel();
}

void DeviceItemWidget::onProgress(int pct)
{
    m_bar->setValue(pct);
    m_lblPct->setText(QString("%1%").arg(pct));
}

void DeviceItemWidget::onLogLine(const QString& line)
{
    if (m_logDialog)
        m_logDialog->appendLine(line);
}

void DeviceItemWidget::onFlashFinished(bool success, const QString& err)
{
    setFlashingState(false);
    emit flashDone(success);

    if (success) {
        m_lblStatus->setText("Done");
        m_lblStatus->setStyleSheet("color: green; font-weight: bold;");
        m_bar->setValue(100);
        m_lblPct->setText("100%");
        if (m_logDialog) m_logDialog->setStatus("Done");
    } else {
        m_lblStatus->setText("Error");
        m_lblStatus->setStyleSheet("color: red; font-weight: bold;");
        if (m_logDialog) {
            m_logDialog->appendLine(QString("\n[ERROR] %1").arg(err));
            m_logDialog->setStatus("Error");
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
    m_lblStatus->setText(active ? "Flashing…" : "Idle");
    m_lblStatus->setStyleSheet(active ? "color: orange; font-weight: bold;" : "");
    m_btnFlash->setText(active ? "Cancel" : "Flash");
    if (!active) { m_bar->setValue(0); m_lblPct->setText("0%"); }
}

#ifdef Q_OS_LINUX
void DeviceItemWidget::onPermissionError()
{
    setFlashingState(false);

    if (m_logDialog)
        m_logDialog->appendLine("\n[ERROR] Permission denied accessing USB device.");

    auto reply = QMessageBox::question(
        this,
        "Permission denied",
        "Cannot open USB device — permission denied.\n\n"
        "Retry with pkexec (a password dialog will appear)?",
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        flash(m_lastUuuPath, m_lastPreset, "pkexec", m_lastRebootAfter);
    } else {
        m_lblStatus->setText("Error");
        m_lblStatus->setStyleSheet("color: red; font-weight: bold;");
        emit flashDone(false);
    }
}
#endif
