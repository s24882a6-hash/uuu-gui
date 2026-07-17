#include "mainwindow.h"
#include "presetdialog.h"
#include "deviceitemwidget.h"
#include "settingsdialog.h"
#include "apppaths.h"

#include <QApplication>
#include <QEvent>
#include <QStyle>
#include <QStyleHints>
#include <QGroupBox>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QEventLoop>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QTextEdit>

#include <QStatusBar>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

// Best-effort: overwrite this copy's characters before releasing the buffer.
// Implicitly-shared copies elsewhere are not covered — QString cannot
// guarantee full erasure.
static void wipePassword(QString& s)
{
    s.fill(QChar('\0'));
    s.clear();
}

// ──────────────────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_monitor(new DeviceMonitor(this))
{
    setWindowTitle(tr("UUU Flash Tool") + " v" + QCoreApplication::applicationVersion());
    setMinimumSize(900, 600);

    setupUi();
    loadSettings();
    refreshPresetList();

    m_helperPath = AppPaths::helper();

    connect(m_monitor, &DeviceMonitor::deviceConnected,
            this, &MainWindow::onDeviceConnected);
    connect(m_monitor, &DeviceMonitor::deviceDisconnected,
            this, &MainWindow::onDeviceDisconnected);
    connect(m_monitor, &DeviceMonitor::monitoringUnavailable,
            this, &MainWindow::onMonitoringUnavailable);

    applySettings();
    m_monitor->start();
}

MainWindow::~MainWindow()
{
    m_monitor->stop();
    wipePassword(m_sessionPassword);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveSettings();
    event->accept();
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QMainWindow::changeEvent(event);
}

void MainWindow::retranslateUi()
{
    setWindowTitle(tr("UUU Flash Tool") + " v" + QCoreApplication::applicationVersion());
    if (m_groupPresets) m_groupPresets->setTitle(tr("Firmware Presets"));
    if (m_groupDevices) m_groupDevices->setTitle(tr("Connected Devices"));
    m_btnAdd->setText(tr("Add"));
    m_btnEdit->setText(tr("Edit"));
    m_btnDelete->setText(tr("Delete"));
    m_noDevicesLbl->setText(tr("No NXP devices detected.\nConnect a device in recovery / SDP mode."));
    m_btnSettings->setText(tr("Settings"));
    m_autoFlash->setText(tr("Auto-flash on connect"));
    m_chkRebootAfter->setText(tr("Reboot after flash"));
    m_btnFlashSel->setText(tr("Flash Checked Devices"));
}

void MainWindow::applyLanguage(const QString& lang)
{
    qApp->removeTranslator(&m_translator);
    if (lang == "ru" && m_translator.load(":/i18n/UUUFlashTool_ru.qm"))
        qApp->installTranslator(&m_translator);
    // Qt sends LanguageChange to all widgets automatically
}

// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::applyTheme(const QString& theme)
{
#ifdef Q_OS_LINUX
    // QTBUG-129917: on Linux/GNOME the platform plugin doesn't implement
    // requestColorScheme(), so setColorScheme() is silently ignored.
    // Workaround: switch to Fusion (always available, no platform-theme
    // interference) and set the palette explicitly.
    static const QString defaultStyle   = QApplication::style()->name();
    static const QPalette defaultPalette = QApplication::palette();

    if (theme == "system") {
        QApplication::setStyle(defaultStyle);
        QApplication::setPalette(defaultPalette);
        return;
    }

    QApplication::setStyle("Fusion");

    if (theme == "dark") {
        QPalette p;
        const QColor base(35, 35, 35);
        const QColor window(53, 53, 53);
        const QColor text(Qt::white);
        const QColor disabled(127, 127, 127);
        const QColor highlight(42, 130, 218);
        p.setColor(QPalette::Window,          window);
        p.setColor(QPalette::WindowText,      text);
        p.setColor(QPalette::Base,            base);
        p.setColor(QPalette::AlternateBase,   window);
        p.setColor(QPalette::ToolTipBase,     QColor(25, 25, 25));
        p.setColor(QPalette::ToolTipText,     text);
        p.setColor(QPalette::Text,            text);
        p.setColor(QPalette::Button,          window);
        p.setColor(QPalette::ButtonText,      text);
        p.setColor(QPalette::BrightText,      Qt::red);
        p.setColor(QPalette::Link,            highlight);
        p.setColor(QPalette::Highlight,       highlight);
        p.setColor(QPalette::HighlightedText, Qt::black);
        p.setColor(QPalette::Disabled, QPalette::WindowText,      disabled);
        p.setColor(QPalette::Disabled, QPalette::Text,            disabled);
        p.setColor(QPalette::Disabled, QPalette::ButtonText,      disabled);
        p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);
        p.setColor(QPalette::Disabled, QPalette::Base,            window);
        p.setColor(QPalette::Disabled, QPalette::Button,          window);
        QApplication::setPalette(p);
    } else {
        QPalette p;
        const QColor window(239, 239, 239);
        const QColor base(Qt::white);
        const QColor text(Qt::black);
        const QColor disabled(160, 160, 160);
        const QColor highlight(48, 140, 198);
        p.setColor(QPalette::Window,          window);
        p.setColor(QPalette::WindowText,      text);
        p.setColor(QPalette::Base,            base);
        p.setColor(QPalette::AlternateBase,   QColor(247, 247, 247));
        p.setColor(QPalette::ToolTipBase,     QColor(255, 255, 220));
        p.setColor(QPalette::ToolTipText,     text);
        p.setColor(QPalette::Text,            text);
        p.setColor(QPalette::Button,          window);
        p.setColor(QPalette::ButtonText,      text);
        p.setColor(QPalette::BrightText,      Qt::red);
        p.setColor(QPalette::Link,            QColor(0, 0, 255));
        p.setColor(QPalette::Highlight,       highlight);
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Disabled, QPalette::WindowText,      disabled);
        p.setColor(QPalette::Disabled, QPalette::Text,            disabled);
        p.setColor(QPalette::Disabled, QPalette::ButtonText,      disabled);
        p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);
        p.setColor(QPalette::Disabled, QPalette::Base,            window);
        p.setColor(QPalette::Disabled, QPalette::Button,          window);
        QApplication::setPalette(p);
    }
#elif QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (theme == "light")
        qApp->styleHints()->setColorScheme(Qt::ColorScheme::Light);
    else if (theme == "dark")
        qApp->styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    else
        qApp->styleHints()->setColorScheme(Qt::ColorScheme::Unknown);
#else
    Q_UNUSED(theme)
#endif
}

void MainWindow::setupUi()
{
    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* root = new QVBoxLayout(central);
    root->setSpacing(6);
    root->setContentsMargins(8, 8, 8, 8);

    m_splitter = new QSplitter(Qt::Horizontal, central);
    m_splitter->addWidget(makePresetsPanel());
    m_splitter->addWidget(makeDevicesPanel());
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({220, 680});
    root->addWidget(m_splitter, 1);

    root->addWidget(makeBottomBar());

    m_statusBar = new QLabel(this);
    statusBar()->addWidget(m_statusBar, 1);
}

QWidget* MainWindow::makePresetsPanel()
{
    m_groupPresets = new QGroupBox(tr("Firmware Presets"), this);
    m_groupPresets->setMinimumWidth(0);
    auto* group  = m_groupPresets;
    auto* layout = new QVBoxLayout(group);

    auto* btnRow = new QHBoxLayout;
    m_btnAdd    = new QPushButton(tr("Add"),    group);
    m_btnEdit   = new QPushButton(tr("Edit"),   group);
    m_btnDelete = new QPushButton(tr("Delete"), group);
    m_btnAdd->setMinimumWidth(0);
    m_btnEdit->setMinimumWidth(0);
    m_btnDelete->setMinimumWidth(0);
    btnRow->addWidget(m_btnAdd);
    btnRow->addWidget(m_btnEdit);
    btnRow->addWidget(m_btnDelete);
    layout->addLayout(btnRow);

    m_presetCombo = new QComboBox(group);
    layout->addWidget(m_presetCombo);

    m_presetFilesLbl = new QTextEdit(group);
    m_presetFilesLbl->setReadOnly(true);
    m_presetFilesLbl->setFrameShape(QFrame::NoFrame);
    m_presetFilesLbl->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_presetFilesLbl->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_presetFilesLbl->setWordWrapMode(QTextOption::WrapAnywhere);
    m_presetFilesLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_presetFilesLbl->setStyleSheet("background: transparent; color: gray;");
    {
        QFont f = m_presetFilesLbl->font();
        f.setPointSize(qMax(f.pointSize() - 2, 7));
        m_presetFilesLbl->setFont(f);
        m_presetFilesLbl->setFixedHeight(QFontMetrics(f).lineSpacing() * 6 + 10);
    }
    layout->addWidget(m_presetFilesLbl);

    layout->addStretch();

    m_autoFlash      = new QCheckBox(tr("Auto-flash on connect"), group);
    m_chkRebootAfter = new QCheckBox(tr("Reboot after flash"), group);
    layout->addWidget(m_autoFlash);
    layout->addWidget(m_chkRebootAfter);

    connect(m_btnAdd,    &QPushButton::clicked, this, &MainWindow::addPreset);
    connect(m_btnEdit,   &QPushButton::clicked, this, &MainWindow::editPreset);
    connect(m_btnDelete, &QPushButton::clicked, this, &MainWindow::deletePreset);
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        FirmwarePreset* p = selectedPreset();
        bool has = p != nullptr;
        m_btnEdit->setEnabled(has);
        m_btnDelete->setEnabled(has);
        updatePresetFilesLabel();
    });

    return group;
}

QWidget* MainWindow::makeDevicesPanel()
{
    m_groupDevices = new QGroupBox(tr("Connected Devices"), this);
    auto* group    = m_groupDevices;
    auto* layout   = new QVBoxLayout(group);

    auto* scroll     = new QScrollArea(group);
    auto* container  = new QWidget(scroll);
    m_devicesLayout  = new QVBoxLayout(container);
    m_devicesLayout->setAlignment(Qt::AlignTop);
    m_devicesLayout->setSpacing(2);

    m_noDevicesLbl = new QLabel(tr("No NXP devices detected.\nConnect a device in recovery / SDP mode."), container);
    m_noDevicesLbl->setAlignment(Qt::AlignCenter);
    m_noDevicesLbl->setStyleSheet("color: gray; font-style: italic; padding: 20px;");
    m_devicesLayout->addWidget(m_noDevicesLbl);

    container->setLayout(m_devicesLayout);
    scroll->setWidget(container);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(scroll);

    return group;
}

QWidget* MainWindow::makeBottomBar()
{
    auto* bar    = new QWidget(this);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);

    m_btnSettings = new QPushButton(tr("Settings"), bar);
    m_btnSettings->setFixedWidth(90);
    layout->addWidget(m_btnSettings);

    layout->addStretch();

    m_btnFlashSel = new QPushButton(tr("Flash Checked Devices"), bar);
    m_btnFlashSel->setFixedWidth(180);
    layout->addWidget(m_btnFlashSel);

    connect(m_btnSettings, &QPushButton::clicked,  this, &MainWindow::openSettings);
    connect(m_btnFlashSel, &QPushButton::clicked,  this, &MainWindow::flashSelected);

    return bar;
}

// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::openSettings()
{
    SettingsDialog dlg(this);
    connect(&dlg, &SettingsDialog::languageChanged, this, &MainWindow::applyLanguage);
    connect(&dlg, &SettingsDialog::themeChanged,   this, &MainWindow::applyTheme);
    connect(&dlg, &SettingsDialog::settingsSaved, this, [this]() {
        applySettings();
    });
    dlg.exec();
}

void MainWindow::applySettings()
{
    if (m_helperPath.isEmpty() || !QFileInfo::exists(m_helperPath)) {
        // No bundled helper — DeviceMonitor will fall back to libusb if available.
        m_statusBar->setText(tr("Built-in uuu-helper not found — limited functionality."));
        m_statusBar->setStyleSheet("color: red;");
        m_monitor->setHelperPath({});
    } else {
        m_statusBar->setText(tr("Using built-in libuuu engine"));
        m_statusBar->setStyleSheet("color: green;");
        m_monitor->setHelperPath(m_helperPath);
    }
}

// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::refreshPresetList(const QString& selectId)
{
    QString selectedId = selectId.isEmpty() ? m_presetCombo->currentData().toString()
                                            : selectId;

    {
        QSignalBlocker blocker(m_presetCombo);
        m_presetCombo->clear();
        for (const auto& p : m_presets)
            m_presetCombo->addItem(p.name, p.id);
    }

    int idx = m_presetCombo->findData(selectedId);
    m_presetCombo->setCurrentIndex(idx >= 0 ? idx : (m_presetCombo->count() > 0 ? 0 : -1));

    bool has = selectedPreset() != nullptr;
    m_btnEdit->setEnabled(has);
    m_btnDelete->setEnabled(has);
    updatePresetFilesLabel();
}

void MainWindow::updatePresetFilesLabel()
{
    FirmwarePreset* p = selectedPreset();
    if (!p) {
        m_presetFilesLbl->setPlainText({});
        return;
    }
    QStringList lines;
    switch (p->type) {
    case FirmwarePreset::Type::SimpleBin:
        lines << "bin: " + QFileInfo(p->binPath).fileName();
        break;
    case FirmwarePreset::Type::EmmcAll:
        lines << "bootloader: " + QFileInfo(p->binPath).fileName();
        lines << "wic: "        + QFileInfo(p->wicPath).fileName();
        break;
    case FirmwarePreset::Type::EmmcAll4G:
        lines << "bin: "        + QFileInfo(p->bin4gPath).fileName();
        lines << "bootloader: " + QFileInfo(p->binPath).fileName();
        lines << "wic: "        + QFileInfo(p->wicPath).fileName();
        break;
    }
    m_presetFilesLbl->setPlainText(lines.join("\n"));
}

void MainWindow::addPreset()
{
    PresetDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    // preset() mints a fresh id on every call, so keep the one we store.
    const FirmwarePreset p = dlg.preset();
    m_presets << p;
    saveSettings();
    refreshPresetList(p.id);
}

void MainWindow::editPreset()
{
    FirmwarePreset* p = selectedPreset();
    if (!p) return;

    PresetDialog dlg(*p, this);
    if (dlg.exec() != QDialog::Accepted) return;
    *p = dlg.preset();
    saveSettings();
    refreshPresetList();
}

void MainWindow::deletePreset()
{
    FirmwarePreset* p = selectedPreset();
    if (!p) return;

    if (QMessageBox::question(this, tr("Delete preset"),
            tr("Delete preset \"%1\"?").arg(p->name)) != QMessageBox::Yes)
        return;

    m_presets.removeIf([&](const FirmwarePreset& x){ return x.id == p->id; });
    saveSettings();
    refreshPresetList();
}

FirmwarePreset* MainWindow::selectedPreset()
{
    QString id = m_presetCombo->currentData().toString();
    for (auto& p : m_presets)
        if (p.id == id) return &p;
    return nullptr;
}

// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::addDeviceWidget(const UsbDevice& dev)
{
    m_noDevicesLbl->setVisible(false);

    auto* w = new DeviceItemWidget(dev, this);
    m_deviceWidgets[dev.busId] = w;
    m_devicesLayout->addWidget(w);

    connect(w, &DeviceItemWidget::flashRequested, this, [this, w](){
        if (w->isFlashing()) return;
        FirmwarePreset* p = selectedPreset();
        if (!p) {
            QMessageBox::information(this, tr("No preset selected"),
                tr("Select a firmware preset from the list before flashing."));
            return;
        }
        flashDevice(w, *p);
    });

    connect(w, &DeviceItemWidget::permissionDenied, this, [this, w](){ onDevicePermissionError(w); });
    connect(w, &DeviceItemWidget::authFailed,       this, [this, w](){ onDeviceAuthFailed(w); });
}

void MainWindow::onDeviceConnected(UsbDevice dev)
{
    // A flashing board re-enumerates (SDP → fastboot) under a new bus address.
    // libuuu re-finds it by serial inside the helper — don't add a duplicate row.
    if (!dev.serialNumber.isEmpty()) {
        for (auto* w : std::as_const(m_deviceWidgets)) {
            if (w->device().serialNumber == dev.serialNumber &&
                (w->isFlashing() || w->recentlyFinishedFlash()))
                return;
        }
    }

    auto* existing = m_deviceWidgets.value(dev.busId);
    if (existing) {
        if (existing->isFlashing()) return;
        m_deviceWidgets.remove(dev.busId);
        m_devicesLayout->removeWidget(existing);
        existing->deleteLater();
    }
    addDeviceWidget(dev);
    m_statusBar->setText(tr("Device connected: %1").arg(dev.displayName()));

    if (m_autoFlash->isChecked()) {
        FirmwarePreset* p = selectedPreset();
        if (p) {
            auto* w = m_deviceWidgets.value(dev.busId);
            if (w) flashDevice(w, *p);
        }
    }
}

void MainWindow::onDeviceDisconnected(QString busId)
{
    auto* w = m_deviceWidgets.value(busId);
    if (!w) return;

    // Keep the widget while ITS flash is active — the device is re-enumerating
    // between phases. isFlashing() alone is not enough: the worker finishes
    // slightly before the USB disconnect event arrives on the main thread,
    // hence the short post-flash grace period.
    if (w->isFlashing() || w->recentlyFinishedFlash()) {
        m_statusBar->setText(tr("Device rebooting: %1").arg(w->device().displayName()));
        return;
    }

    m_deviceWidgets.remove(busId);
    m_statusBar->setText(tr("Device disconnected: %1").arg(w->device().displayName()));
    m_devicesLayout->removeWidget(w);
    w->deleteLater();

    if (m_deviceWidgets.isEmpty())
        m_noDevicesLbl->setVisible(true);
}

void MainWindow::onMonitoringUnavailable(QString reason)
{
    m_noDevicesLbl->setText(tr("USB monitoring unavailable.\n%1").arg(reason));
    m_noDevicesLbl->setVisible(true);
    m_statusBar->setText(tr("USB monitoring unavailable — install libusb and rebuild."));
    m_statusBar->setStyleSheet("color: orange;");
}

// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::flashDevice(DeviceItemWidget* widget, const FirmwarePreset& preset)
{
    if (m_helperPath.isEmpty() || !QFileInfo::exists(m_helperPath)) {
        QMessageBox::warning(this, tr("Helper not found"),
            tr("The bundled uuu-helper executable is missing."));
        return;
    }

    if (!preset.isValid()) {
        QMessageBox::warning(this, tr("Invalid preset"),
            tr("The selected preset has missing or invalid files."));
        return;
    }

    // Reuse the session password if a previous device already needed elevation.
    QString password = m_useElevation ? m_sessionPassword : QString();
    widget->flash(m_helperPath, preset, m_chkRebootAfter->isChecked(), password);
}

bool MainWindow::promptPassword(bool incorrect)
{
    bool ok = false;
    QString note = incorrect
        ? tr("Incorrect password — please try again.")
        : tr("Enter your password to flash with administrator privileges.");
    QString pw = QInputDialog::getText(this, tr("Administrator password"),
                                       note, QLineEdit::Password, QString(), &ok);
    if (!ok) return false;
    m_sessionPassword = pw;
    m_useElevation    = true;
    return true;
}

void MainWindow::onDevicePermissionError(DeviceItemWidget* widget)
{
#ifdef Q_OS_WIN
    widget->abortFlash(tr("Cannot access the device. Install the WinUSB driver (e.g. via Zadig)."));
#else
    // Already ran as root and still denied — a password won't help.
    if (widget->isElevated()) {
        widget->abortFlash(tr("Device access denied even with administrator privileges."));
        return;
    }
    requestElevation(widget, /*passwordWasWrong=*/false);
#endif
}

void MainWindow::onDeviceAuthFailed(DeviceItemWidget* widget)
{
    requestElevation(widget, /*passwordWasWrong=*/true);
}

#ifdef Q_OS_LINUX
bool MainWindow::installUdevRule()
{
    // VIDs covered by `uuu -udev`: NXP/Freescale recovery mode plus the
    // fastboot gadget IDs boards re-enumerate as during flashing.
    static const char kRules[] =
        "SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"1fc9\", MODE=\"0666\"\n"
        "SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"15a2\", MODE=\"0666\"\n"
        "SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"0525\", MODE=\"0666\"\n"
        "SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"0483\", MODE=\"0666\"\n"
        "SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"18d1\", MODE=\"0666\"\n";

    const QString script = QStringLiteral(
        "set -e\n"
        "cat > /etc/udev/rules.d/70-uuu-gui.rules <<'EOF'\n"
        "%1"
        "EOF\n"
        "udevadm control --reload\n"
        "udevadm trigger --subsystem-match=usb\n").arg(QString::fromLatin1(kRules));

    // pkexec shows polkit's own authentication dialog, so the password never
    // passes through this process.
    QProcess proc;
    proc.start(QStringLiteral("pkexec"), {QStringLiteral("sh"), QStringLiteral("-c"), script});
    if (!proc.waitForStarted(3000)) {
        QMessageBox::warning(this, tr("udev rule"),
            tr("pkexec is not available — falling back to sudo."));
        return false;
    }

    // Wait without blocking the event loop (the auth dialog may stay open a while).
    QEventLoop loop;
    connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            &loop, &QEventLoop::quit);
    if (proc.state() != QProcess::NotRunning)
        loop.exec();

    if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0) {
        m_statusBar->setText(tr("udev rule installed — flashing no longer needs a password."));
        return true;
    }
    QMessageBox::warning(this, tr("udev rule"),
        tr("Failed to install the udev rule — falling back to sudo.\n%1")
            .arg(QString::fromUtf8(proc.readAllStandardError()).trimmed()));
    return false;
}
#endif

MainWindow::ElevationOutcome MainWindow::resolveElevation(bool passwordWasWrong)
{
#ifdef Q_OS_LINUX
    // Offer the permanent fix first: a udev rule installed via pkexec.
    if (!passwordWasWrong && !m_udevOfferDeclined) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(tr("Device access denied"));
        box.setText(tr("Flashing needs access to the USB device."));
        box.setInformativeText(tr(
            "Installing a udev rule (recommended) grants permanent access to NXP "
            "devices — no password will be needed again.\n\n"
            "Alternatively, this flash can run via sudo with your password."));
        QPushButton* installBtn = box.addButton(tr("Install udev rule"), QMessageBox::AcceptRole);
        QPushButton* sudoBtn    = box.addButton(tr("Use sudo"),          QMessageBox::ActionRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();

        if (box.clickedButton() == installBtn) {
            if (installUdevRule())
                return ElevationOutcome::Unprivileged;
            // Installation failed — fall through to the sudo prompt.
        } else if (box.clickedButton() == sudoBtn) {
            m_udevOfferDeclined = true; // don't re-ask for every device this session
        } else {
            return ElevationOutcome::Aborted;
        }
    }
#endif
    return promptPassword(passwordWasWrong) ? ElevationOutcome::Elevated
                                            : ElevationOutcome::Aborted;
}

void MainWindow::requestElevation(DeviceItemWidget* widget, bool passwordWasWrong)
{
    // Reuse a known-good password without prompting.
    if (!passwordWasWrong && m_useElevation && !m_sessionPassword.isEmpty()) {
        widget->retryFlash(m_sessionPassword);
        return;
    }

    // A prompt is already open (its modal loop delivers other devices'
    // errors re-entrantly). Queue this device instead of stacking dialogs.
    if (m_promptInProgress) {
        if (!m_pendingElevation.contains(widget))
            m_pendingElevation << widget;
        return;
    }

    if (passwordWasWrong)
        wipePassword(m_sessionPassword);

    m_promptInProgress = true;
    const ElevationOutcome outcome = resolveElevation(passwordWasWrong);
    m_promptInProgress = false;

    // Devices that piled up while the dialog was open.
    QList<DeviceItemWidget*> pending = m_pendingElevation;
    m_pendingElevation.clear();

    switch (outcome) {
    case ElevationOutcome::Aborted: {
        const QString abortMsg = tr("Administrator privileges are required to flash this device.");
        widget->abortFlash(abortMsg);
        for (auto* w : pending) w->abortFlash(abortMsg);
        break;
    }
    case ElevationOutcome::Unprivileged:
        widget->retryFlash({});
        for (auto* w : pending) w->retryFlash({});
        break;
    case ElevationOutcome::Elevated:
        widget->retryFlash(m_sessionPassword);
        for (auto* w : pending) w->retryFlash(m_sessionPassword);
        break;
    }
}

void MainWindow::flashSelected()
{
    FirmwarePreset* p = selectedPreset();
    if (!p) {
        QMessageBox::information(this, tr("No preset"),
            tr("Select a firmware preset first."));
        return;
    }

    if (m_helperPath.isEmpty() || !QFileInfo::exists(m_helperPath)) {
        QMessageBox::warning(this, tr("Helper not found"),
            tr("The bundled uuu-helper executable is missing."));
        return;
    }

    bool any = false;
    for (int i = 0; i < m_devicesLayout->count(); ++i) {
        auto* item = m_devicesLayout->itemAt(i);
        if (!item || !item->widget()) continue;
        auto* w = qobject_cast<DeviceItemWidget*>(item->widget());
        if (!w || !w->isChecked() || w->isFlashing()) continue;
        flashDevice(w, *p);
        any = true;
    }

    if (!any)
        QMessageBox::information(this, tr("Nothing to flash"),
            tr("Check at least one device in the list."));
}

// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::loadSettings()
{
    QSettings s;

    applyLanguage(s.value("language", "en").toString());

    m_autoFlash->setChecked(s.value("autoFlash", false).toBool());
    m_chkRebootAfter->setChecked(s.value("rebootAfterFlash", false).toBool());
    if (s.contains("splitterState"))
        m_splitter->restoreState(s.value("splitterState").toByteArray());

    QByteArray presetsJson = s.value("presets").toByteArray();
    if (!presetsJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(presetsJson);
        if (doc.isArray()) {
            for (const auto& val : doc.array())
                m_presets << FirmwarePreset::fromJson(val.toObject());
        }
    }
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue("autoFlash",        m_autoFlash->isChecked());
    s.setValue("rebootAfterFlash", m_chkRebootAfter->isChecked());
    s.setValue("splitterState",    m_splitter->saveState());

    QJsonArray arr;
    for (const auto& p : m_presets)
        arr << p.toJson();
    s.setValue("presets", QJsonDocument(arr).toJson(QJsonDocument::Compact));
}
