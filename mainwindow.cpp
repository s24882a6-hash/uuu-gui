#include "mainwindow.h"
#include "presetdialog.h"
#include "deviceitemwidget.h"
#include "settingsdialog.h"

#include <QApplication>
#include <QEvent>
#include <QGroupBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCheckBox>
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
#include <QStandardPaths>

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
    refreshPresetList();

    if (!m_uuuPath.isEmpty() && QFileInfo::exists(m_uuuPath))
        m_monitor->setUuuPath(m_uuuPath, m_sudoPrefix);

    connect(m_monitor, &DeviceMonitor::deviceConnected,
            this, &MainWindow::onDeviceConnected);
    connect(m_monitor, &DeviceMonitor::deviceDisconnected,
            this, &MainWindow::onDeviceDisconnected);
    connect(m_monitor, &DeviceMonitor::monitoringUnavailable,
            this, &MainWindow::onMonitoringUnavailable);

    m_monitor->start();

    // Reflect loaded uuu path in status bar
    if (!m_uuuPath.isEmpty())
        applyUuuSettings(m_uuuPath, m_sudoPrefix);
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

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QMainWindow::changeEvent(event);
}

void MainWindow::retranslateUi()
{
    setWindowTitle(tr("UUU Flash Tool"));
    if (m_groupPresets) m_groupPresets->setTitle(tr("Firmware Presets"));
    if (m_groupDevices) m_groupDevices->setTitle(tr("Connected Devices"));
    m_btnAdd->setText(tr("+  Add"));
    m_btnEdit->setText(tr("Edit"));
    m_btnDelete->setText(tr("Delete"));
    m_noDevicesLbl->setText(tr("No NXP devices detected.\nConnect a device in recovery / SDP mode."));
    m_btnSettings->setText(tr("Settings"));
    m_autoFlash->setText(tr("Auto-flash on connect:"));
    m_chkRebootAfter->setText(tr("Reboot after flash"));
    m_btnFlashSel->setText(tr("Flash Checked Devices"));
}

void MainWindow::applyLanguage(const QString& lang)
{
    qApp->removeTranslator(&m_translator);
    if (lang == "ru" && m_translator.load(":/i18n/uuuapp_ru.qm"))
        qApp->installTranslator(&m_translator);
    // Qt sends LanguageChange to all widgets automatically
}

// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::setupUi()
{
    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* root = new QVBoxLayout(central);
    root->setSpacing(6);
    root->setContentsMargins(8, 8, 8, 8);

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

QWidget* MainWindow::makePresetsPanel()
{
    m_groupPresets = new QGroupBox(tr("Firmware Presets"), this);
    m_groupPresets->setMinimumWidth(0);
    auto* group    = m_groupPresets;
    auto* layout   = new QVBoxLayout(group);

    m_presetList = new QListWidget(group);
    m_presetList->setItemDelegate(new PresetDelegate(m_presetList));
    layout->addWidget(m_presetList);

    auto* btnRow = new QHBoxLayout;
    m_btnAdd    = new QPushButton(tr("+  Add"),  group);
    m_btnEdit   = new QPushButton(tr("Edit"),    group);
    m_btnDelete = new QPushButton(tr("Delete"),  group);
    m_btnAdd->setMinimumWidth(0);
    m_btnEdit->setMinimumWidth(0);
    m_btnDelete->setMinimumWidth(0);
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

    layout->addSpacing(12);

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

    connect(m_btnSettings, &QPushButton::clicked,  this, &MainWindow::openSettings);
    connect(m_autoFlash,   &QCheckBox::toggled,    this, &MainWindow::onAutoFlashToggled);
    connect(m_btnFlashSel, &QPushButton::clicked,  this, &MainWindow::flashSelected);

    return bar;
}

// ──────────────────────────────────────────────────────────────────────────────

void MainWindow::openSettings()
{
    SettingsDialog dlg(this);
    connect(&dlg, &SettingsDialog::languageChanged, this, &MainWindow::applyLanguage);
    connect(&dlg, &SettingsDialog::settingsSaved, this, [this]() {
        QSettings s("uuuapp", "UUUFlashTool");
        applyUuuSettings(s.value("uuuPath").toString(), s.value("sudoPrefix").toString());
    });
    dlg.exec();
}

void MainWindow::applyUuuSettings(const QString& uuuPath, const QString& sudoPrefix)
{
    m_uuuPath    = uuuPath;
    m_sudoPrefix = sudoPrefix;

    if (uuuPath.isEmpty() || !QFileInfo::exists(uuuPath)) {
        m_statusBar->setText(uuuPath.isEmpty() ? "" : tr("uuu binary not found at: ") + uuuPath);
        m_statusBar->setStyleSheet("color: red;");
        m_monitor->setUuuPath({}, {});
    } else {
        m_statusBar->setText("uuu: " + uuuPath);
        m_statusBar->setStyleSheet("color: green;");
        m_monitor->setUuuPath(uuuPath, sudoPrefix);
    }
}

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
                if (w) w->flash(m_uuuPath, p, m_sudoPrefix, m_chkRebootAfter->isChecked());
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

void MainWindow::flashDevice(DeviceItemWidget* widget)
{
    FirmwarePreset* p = selectedPreset();
    if (!p) return;

    if (m_uuuPath.isEmpty() || !QFileInfo::exists(m_uuuPath)) {
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
    widget->flash(m_uuuPath, *p, m_sudoPrefix, m_chkRebootAfter->isChecked());
}

void MainWindow::flashSelected()
{
    FirmwarePreset* p = selectedPreset();
    if (!p) {
        QMessageBox::information(this, tr("No preset"),
            tr("Select a firmware preset first."));
        return;
    }

    if (m_uuuPath.isEmpty() || !QFileInfo::exists(m_uuuPath)) {
        QMessageBox::warning(this, tr("uuu not found"), tr("Set a valid uuu binary path first."));
        return;
    }

    bool any = false;
    for (int i = 0; i < m_devicesLayout->count(); ++i) {
        auto* item = m_devicesLayout->itemAt(i);
        if (!item || !item->widget()) continue;
        auto* w = qobject_cast<DeviceItemWidget*>(item->widget());
        if (!w || !w->isChecked() || w->isFlashing()) continue;
        flashDevice(w);
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

void MainWindow::loadSettings()
{
    QSettings s("uuuapp", "UUUFlashTool");

    m_uuuPath    = s.value("uuuPath").toString();
    m_sudoPrefix = s.value("sudoPrefix").toString();

    m_autoFlash->setChecked(s.value("autoFlash", false).toBool());
    m_chkRebootAfter->setChecked(s.value("rebootAfterFlash", false).toBool());

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
    s.setValue("uuuPath",          m_uuuPath);
    s.setValue("sudoPrefix",       m_sudoPrefix);
    s.setValue("autoFlash",        m_autoFlash->isChecked());
    s.setValue("rebootAfterFlash", m_chkRebootAfter->isChecked());

    QJsonArray arr;
    for (const auto& p : m_presets)
        arr << p.toJson();
    s.setValue("presets", QJsonDocument(arr).toJson(QJsonDocument::Compact));
}
