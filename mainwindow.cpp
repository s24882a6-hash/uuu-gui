#include "mainwindow.h"
#include "presetdialog.h"
#include "deviceitemwidget.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QDir>

static constexpr int kPresetDescRole = Qt::UserRole + 1;

class PresetDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QString desc = index.data(kPresetDescRole).toString();
        int lines = desc.isEmpty() ? 0 : desc.count('\n') + 1;
        QFont nameFont2 = option.font;
        nameFont2.setPointSize(option.font.pointSize() + 4);
        QFont descFont = option.font;
        descFont.setPointSize(qMax(option.font.pointSize() - 3, 6));
        int nameH = QFontMetrics(nameFont2).height();
        int descH = QFontMetrics(descFont).height();
        int total = 12 + nameH + (lines > 0 ? 3 + descH * lines + 2 * (lines - 1) : 0) + 12;
        return QSize(0, total);
    }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        // Draw background/selection without text (clear it so base doesn't render it)
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear();
        QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &opt, p);

        QString name = index.data(Qt::DisplayRole).toString();
        QString desc = index.data(kPresetDescRole).toString();

        bool selected = option.state & QStyle::State_Selected;
        QRect r = option.rect.adjusted(10, 0, -6, 0);

        QFont nameFont = option.font;
        nameFont.setPointSize(option.font.pointSize() + 4);
        QFont descFont = option.font;
        descFont.setPointSize(qMax(option.font.pointSize() - 3, 6));

        QColor nameColor = option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
        QColor descColor = selected
            ? nameColor.lighter(160)
            : option.palette.color(QPalette::PlaceholderText);

        QFontMetrics nameFm(nameFont);
        QFontMetrics descFm(descFont);
        int nameH = nameFm.height();
        int descLineH = descFm.height();
        QStringList descLines = desc.split('\n');
        int descTotalH = desc.isEmpty() ? 0 : descLineH * descLines.size() + 2 * (descLines.size() - 1);
        int totalH = nameH + (descTotalH > 0 ? 3 + descTotalH : 0);
        int topY = r.top() + (r.height() - totalH) / 2;

        p->save();
        p->setFont(nameFont);
        p->setPen(nameColor);
        p->drawText(QRect(r.left(), topY, r.width(), nameH),
                    Qt::AlignLeft | Qt::AlignVCenter, name);

        if (!desc.isEmpty()) {
            p->setFont(descFont);
            p->setPen(descColor);
            int y = topY + nameH + 3;
            for (const QString& line : descLines) {
                p->drawText(QRect(r.left(), y, r.width(), descLineH),
                            Qt::AlignLeft | Qt::AlignVCenter, line);
                y += descLineH + 2;
            }
        }
        p->restore();
    }
};

// ──────────────────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_monitor(new DeviceMonitor(this))
{
    setWindowTitle(tr("UUU Flash Tool"));
    setMinimumSize(900, 600);

    setupUi();
    loadSettings();
    refreshUuuDropdown();
    refreshPresetList();

    // Prime the monitor with whatever uuu path was restored from settings
    {
        QString p = currentUuuPath();
        if (!p.isEmpty() && QFileInfo::exists(p))
            m_monitor->setUuuPath(p, currentSudoPrefix());
    }

    connect(m_monitor, &DeviceMonitor::deviceConnected,
            this, &MainWindow::onDeviceConnected);
    connect(m_monitor, &DeviceMonitor::deviceDisconnected,
            this, &MainWindow::onDeviceDisconnected);
    connect(m_monitor, &DeviceMonitor::monitoringUnavailable,
            this, &MainWindow::onMonitoringUnavailable);

    m_monitor->start();
}

MainWindow::~MainWindow()
{
    m_monitor->stop();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveSettings();
    event->accept();
}

// ──────────────────────────────────────────────────────────────────────────────
// UI construction
// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::setupUi()
{
    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* root = new QVBoxLayout(central);
    root->setSpacing(6);
    root->setContentsMargins(8, 8, 8, 8);

    root->addWidget(makeUuuBar());

    auto* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->addWidget(makePresetsPanel());
    splitter->addWidget(makeDevicesPanel());
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    root->addWidget(splitter, 1);

    root->addWidget(makeBottomBar());

    m_statusBar = new QLabel(this);
    statusBar()->addWidget(m_statusBar, 1);
}

QWidget* MainWindow::makeUuuBar()
{
    auto* bar    = new QGroupBox(tr("UUU Binary"), this);
    auto* layout = new QHBoxLayout(bar);

    layout->addWidget(new QLabel(tr("Path:"), bar));

    m_uuuCombo = new QComboBox(bar);
    m_uuuCombo->setEditable(true);
    m_uuuCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(m_uuuCombo, 2);

    m_uuuBrowse = new QPushButton(tr("Browse…"), bar);
    m_uuuBrowse->setFixedWidth(80);
    layout->addWidget(m_uuuBrowse);

    layout->addWidget(new QLabel(tr("  Privilege:"), bar));
    m_sudoCombo = new QComboBox(bar);
    m_sudoCombo->addItem(tr("None (run as-is)"), "");
#ifdef Q_OS_LINUX
    m_sudoCombo->addItem("sudo",   "sudo");
    m_sudoCombo->addItem("pkexec", "pkexec");
#elif defined(Q_OS_MAC)
    m_sudoCombo->addItem("sudo", "sudo");
#endif
    layout->addWidget(m_sudoCombo);

    connect(m_uuuBrowse, &QPushButton::clicked,        this, &MainWindow::browseUuu);
    connect(m_uuuCombo,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onUuuChanged);
    connect(m_uuuCombo,  &QComboBox::editTextChanged,
            this, [this](const QString&){ onUuuChanged(0); });
    connect(m_sudoCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ onUuuChanged(0); });

    return bar;
}

QWidget* MainWindow::makePresetsPanel()
{
    auto* group  = new QGroupBox(tr("Firmware Presets"), this);
    auto* layout = new QVBoxLayout(group);

    m_presetList = new QListWidget(group);
    m_presetList->setItemDelegate(new PresetDelegate(m_presetList));
    layout->addWidget(m_presetList);

    auto* btnRow = new QHBoxLayout;
    m_btnAdd    = new QPushButton(tr("+  Add"),  group);
    m_btnEdit   = new QPushButton(tr("Edit"),    group);
    m_btnDelete = new QPushButton(tr("Delete"),  group);
    m_btnEdit->setEnabled(false);
    m_btnDelete->setEnabled(false);
    btnRow->addWidget(m_btnAdd);
    btnRow->addWidget(m_btnEdit);
    btnRow->addWidget(m_btnDelete);
    layout->addLayout(btnRow);

    connect(m_btnAdd,    &QPushButton::clicked, this, &MainWindow::addPreset);
    connect(m_btnEdit,   &QPushButton::clicked, this, &MainWindow::editPreset);
    connect(m_btnDelete, &QPushButton::clicked, this, &MainWindow::deletePreset);
    connect(m_presetList, &QListWidget::itemSelectionChanged,
            this, &MainWindow::onPresetSelectionChanged);
    connect(m_presetList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) { editPreset(); });

    return group;
}

QWidget* MainWindow::makeDevicesPanel()
{
    auto* group  = new QGroupBox(tr("Connected Devices"), this);
    auto* layout = new QVBoxLayout(group);

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

    m_autoFlash = new QCheckBox(tr("Auto-flash on connect:"), bar);
    layout->addWidget(m_autoFlash);

    m_autoPreset = new QComboBox(bar);
    m_autoPreset->setEnabled(false);
    m_autoPreset->setMinimumWidth(200);
    layout->addWidget(m_autoPreset);

    layout->addSpacing(16);

    m_chkRebootAfter = new QCheckBox(tr("Reboot after flash"), bar);
    layout->addWidget(m_chkRebootAfter);

    layout->addStretch();

    m_btnFlashSel = new QPushButton(tr("Flash Checked Devices"), bar);
    m_btnFlashSel->setFixedWidth(180);
    layout->addWidget(m_btnFlashSel);

    connect(m_autoFlash,   &QCheckBox::toggled,   this, &MainWindow::onAutoFlashToggled);
    connect(m_btnFlashSel, &QPushButton::clicked, this, &MainWindow::flashSelected);

    return bar;
}

// ──────────────────────────────────────────────────────────────────────────────
// UUU binary handling
// ──────────────────────────────────────────────────────────────────────────────

QStringList MainWindow::findUuuBinaries()
{
    QStringList candidates;

    // QStandardPaths::findExecutable searches PATH cross-platform (no "which" on Windows)
    QString inPath = QStandardPaths::findExecutable("uuu");
    if (!inPath.isEmpty()) candidates << inPath;

#ifdef Q_OS_WIN
    QStringList winPaths = {
        "C:/Program Files/uuu/uuu.exe",
        "C:/uuu/uuu.exe"
    };
    for (const auto& wp : winPaths)
        if (QFileInfo::exists(wp)) candidates << wp;
#elif defined(Q_OS_MAC)
    QStringList macPaths = {
        "/usr/local/bin/uuu",
        "/opt/homebrew/bin/uuu"
    };
    for (const auto& mp : macPaths)
        if (QFileInfo::exists(mp) && !candidates.contains(mp)) candidates << mp;
#else
    QStringList linuxPaths = {
        "/usr/local/bin/uuu",
        "/usr/bin/uuu"
    };
    for (const auto& lp : linuxPaths)
        if (QFileInfo::exists(lp) && !candidates.contains(lp)) candidates << lp;
#endif

    return candidates;
}

void MainWindow::refreshUuuDropdown()
{
    QStringList found = findUuuBinaries();
    QString current   = m_uuuCombo->currentText();

    m_uuuCombo->blockSignals(true);
    m_uuuCombo->clear();
    for (const auto& p : found)
        m_uuuCombo->addItem(p);

    if (!current.isEmpty()) {
        int idx = m_uuuCombo->findText(current);
        if (idx >= 0) m_uuuCombo->setCurrentIndex(idx);
        else          m_uuuCombo->setEditText(current);
    }
    m_uuuCombo->blockSignals(false);

    if (found.isEmpty()) {
        m_statusBar->setText(tr("uuu not found in PATH. Browse to set the path manually."));
        m_statusBar->setStyleSheet("color: orange;");
    }
}

void MainWindow::browseUuu()
{
#ifdef Q_OS_WIN
    QString filter = tr("Executables (*.exe);;All files (*)");
#else
    QString filter = tr("All files (*)");
#endif
    QString path = QFileDialog::getOpenFileName(this, tr("Select uuu binary"), {}, filter);
    if (path.isEmpty()) return;

    int idx = m_uuuCombo->findText(path);
    if (idx < 0) {
        m_uuuCombo->addItem(path);
        idx = m_uuuCombo->count() - 1;
    }
    m_uuuCombo->setCurrentIndex(idx);
}

void MainWindow::onUuuChanged(int)
{
    QString path = currentUuuPath();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        m_statusBar->setText(path.isEmpty() ? "" : tr("uuu binary not found at: ") + path);
        m_statusBar->setStyleSheet("color: red;");
        m_monitor->setUuuPath({}, {});
    } else {
        m_statusBar->setText("uuu: " + path);
        m_statusBar->setStyleSheet("color: green;");
        m_monitor->setUuuPath(path, currentSudoPrefix());
    }
}

QString MainWindow::currentUuuPath() const
{
    return m_uuuCombo->currentText().trimmed();
}

QString MainWindow::currentSudoPrefix() const
{
    return m_sudoCombo->currentData().toString();
}

// ──────────────────────────────────────────────────────────────────────────────
// Preset management
// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::refreshPresetList()
{
    QString selectedId;
    if (!m_presetList->selectedItems().isEmpty())
        selectedId = m_presetList->selectedItems().first()->data(Qt::UserRole).toString();

    m_presetList->clear();
    for (const auto& p : m_presets) {
        auto* item = new QListWidgetItem(m_presetList);
        item->setText(p.name);
        item->setToolTip(p.description());
        item->setData(Qt::UserRole, p.id);

        QStringList descLines;
        switch (p.type) {
        case FirmwarePreset::Type::SimpleBin:
            descLines << "bin: " + QFileInfo(p.binPath).fileName();
            break;
        case FirmwarePreset::Type::EmmcAll:
            descLines << "bootloader: " + QFileInfo(p.binPath).fileName();
            descLines << "wic: "        + QFileInfo(p.wicPath).fileName();
            break;
        case FirmwarePreset::Type::EmmcAll4G:
            descLines << "bin: "        + QFileInfo(p.bin4gPath).fileName();
            descLines << "bootloader: " + QFileInfo(p.binPath).fileName();
            descLines << "wic: "        + QFileInfo(p.wicPath).fileName();
            break;
        }
        item->setData(kPresetDescRole, descLines.join("\n"));
    }

    // Restore or auto-select first item
    bool restored = false;
    if (!selectedId.isEmpty()) {
        for (int i = 0; i < m_presetList->count(); ++i) {
            if (m_presetList->item(i)->data(Qt::UserRole).toString() == selectedId) {
                m_presetList->setCurrentRow(i);
                restored = true;
                break;
            }
        }
    }
    if (!restored && m_presetList->count() > 0)
        m_presetList->setCurrentRow(0);

    // Refresh auto-preset combo
    QString autoId = m_autoPreset->currentData().toString();
    m_autoPreset->clear();
    for (const auto& p : m_presets)
        m_autoPreset->addItem(p.name, p.id);
    int idx = m_autoPreset->findData(autoId);
    if (idx >= 0) m_autoPreset->setCurrentIndex(idx);
}

void MainWindow::addPreset()
{
    PresetDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    m_presets << dlg.preset();
    saveSettings();
    refreshPresetList();
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

void MainWindow::onPresetSelectionChanged()
{
    bool sel = !m_presetList->selectedItems().isEmpty();
    m_btnEdit->setEnabled(sel);
    m_btnDelete->setEnabled(sel);
}

FirmwarePreset* MainWindow::selectedPreset()
{
    auto items = m_presetList->selectedItems();
    if (items.isEmpty()) return nullptr;
    QString id = items.first()->data(Qt::UserRole).toString();
    for (auto& p : m_presets)
        if (p.id == id) return &p;
    return nullptr;
}

// ──────────────────────────────────────────────────────────────────────────────
// Device handling
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
        flashDevice(w);
    });

    connect(w, &DeviceItemWidget::flashDone, this, [this](bool){
        if (--m_activeFlashCount <= 0) {
            m_activeFlashCount = 0;
            m_monitor->setOpenAllowed(true);
        }
    });
}

void MainWindow::onDeviceConnected(UsbDevice dev)
{
    auto* existing = m_deviceWidgets.value(dev.busId);
    if (existing) {
        if (existing->isFlashing()) return;
        // Stale widget from a previous flash — replace it
        m_deviceWidgets.remove(dev.busId);
        m_devicesLayout->removeWidget(existing);
        existing->deleteLater();
    }
    addDeviceWidget(dev);
    m_statusBar->setText(tr("Device connected: %1").arg(dev.displayName()));

    if (m_autoFlash->isChecked()) {
        QString autoId = m_autoPreset->currentData().toString();
        for (auto& p : m_presets) {
            if (p.id == autoId) {
                auto* w = m_deviceWidgets.value(dev.busId);
                if (w) w->flash(currentUuuPath(), p, currentSudoPrefix(), m_chkRebootAfter->isChecked());
                break;
            }
        }
    }
}

void MainWindow::onDeviceDisconnected(QString busId)
{
    auto* w = m_deviceWidgets.value(busId);
    if (!w) return;

    if (w->isFlashing()) {
        // Device rebooted mid-flash (normal after Phase 1 of multi-phase preset).
        // Keep the widget alive so FlashWorker can finish Phase 2.
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
// Flash
// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::flashDevice(DeviceItemWidget* widget)
{
    FirmwarePreset* p = selectedPreset();
    if (!p) return;

    QString uuu = currentUuuPath();
    if (uuu.isEmpty() || !QFileInfo::exists(uuu)) {
        QMessageBox::warning(this, tr("uuu not found"), tr("Set a valid uuu binary path first."));
        return;
    }

    if (!p->isValid()) {
        QMessageBox::warning(this, tr("Invalid preset"),
            tr("The selected preset has missing or invalid files."));
        return;
    }

    ++m_activeFlashCount;
    m_monitor->setOpenAllowed(false);
    widget->flash(uuu, *p, currentSudoPrefix(), m_chkRebootAfter->isChecked());
}

void MainWindow::flashSelected()
{
    FirmwarePreset* p = selectedPreset();
    if (!p) {
        QMessageBox::information(this, tr("No preset"),
            tr("Select a firmware preset first."));
        return;
    }

    QString uuu = currentUuuPath();
    if (uuu.isEmpty() || !QFileInfo::exists(uuu)) {
        QMessageBox::warning(this, tr("uuu not found"), tr("Set a valid uuu binary path first."));
        return;
    }

    bool any = false;
    bool reboot = m_chkRebootAfter->isChecked();
    for (int i = 0; i < m_devicesLayout->count(); ++i) {
        auto* item = m_devicesLayout->itemAt(i);
        if (!item || !item->widget()) continue;
        auto* w = qobject_cast<DeviceItemWidget*>(item->widget());
        if (!w || !w->isChecked() || w->isFlashing()) continue;
        w->flash(uuu, *p, currentSudoPrefix(), reboot);
        any = true;
    }

    if (!any)
        QMessageBox::information(this, tr("Nothing to flash"),
            tr("Check at least one device in the list."));
}


void MainWindow::onAutoFlashToggled(bool enabled)
{
    m_autoPreset->setEnabled(enabled);
}

// ──────────────────────────────────────────────────────────────────────────────
// Persistence (QSettings, JSON)
// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::loadSettings()
{
    QSettings s("uuuapp", "UUUFlashTool");

    // UUU path
    QString savedUuu = s.value("uuuPath").toString();
    if (!savedUuu.isEmpty()) {
        if (m_uuuCombo->findText(savedUuu) < 0)
            m_uuuCombo->addItem(savedUuu);
        m_uuuCombo->setCurrentText(savedUuu);
    }

    // Sudo prefix
    QString savedPrefix = s.value("sudoPrefix").toString();
    int pidx = m_sudoCombo->findData(savedPrefix);
    if (pidx >= 0) m_sudoCombo->setCurrentIndex(pidx);

    // Auto-flash
    m_autoFlash->setChecked(s.value("autoFlash", false).toBool());
    m_chkRebootAfter->setChecked(s.value("rebootAfterFlash", false).toBool());

    // Presets
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
    QSettings s("uuuapp", "UUUFlashTool");
    s.setValue("uuuPath",    currentUuuPath());
    s.setValue("sudoPrefix", currentSudoPrefix());
    s.setValue("autoFlash",        m_autoFlash->isChecked());
    s.setValue("rebootAfterFlash", m_chkRebootAfter->isChecked());

    QJsonArray arr;
    for (const auto& p : m_presets)
        arr << p.toJson();
    s.setValue("presets", QJsonDocument(arr).toJson(QJsonDocument::Compact));
}
