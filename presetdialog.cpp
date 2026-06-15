#include "presetdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QRadioButton>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QFileInfo>

PresetDialog::PresetDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("New Firmware Preset");
    setupUi();
    onTypeChanged();
    validate();
}

PresetDialog::PresetDialog(const FirmwarePreset& preset, QWidget* parent)
    : QDialog(parent)
    , m_editId(preset.id)
{
    setWindowTitle("Edit Firmware Preset");
    setupUi();
    populate(preset);
    onTypeChanged();
    validate();
}

void PresetDialog::setupUi()
{
    setMinimumWidth(480);
    auto* root = new QVBoxLayout(this);

    // --- Name ---
    auto* formLayout = new QFormLayout;
    m_name = new QLineEdit(this);
    m_name->setPlaceholderText("My Preset");
    formLayout->addRow("Preset name:", m_name);
    root->addLayout(formLayout);

    // --- Type ---
    auto* typeGroup = new QGroupBox("Flash type", this);
    auto* typeVBox  = new QVBoxLayout(typeGroup);
    m_rdSimple  = new QRadioButton("Simple boot      (uuu <file.bin>)", typeGroup);
    m_rdEmmc    = new QRadioButton("Full eMMC        (uuu -b emmc_all <bootloader> <image.wic>)", typeGroup);
    m_rdEmmc4g  = new QRadioButton("Full eMMC + 4G   (uuu <4g.bin> → uuu -b emmc_all <bootloader> <image.wic>)", typeGroup);
    m_rdSimple->setChecked(true);
    typeVBox->addWidget(m_rdSimple);
    typeVBox->addWidget(m_rdEmmc);
    typeVBox->addWidget(m_rdEmmc4g);
    root->addWidget(typeGroup);

    // --- Paths ---
    auto* pathGroup  = new QGroupBox("Files", this);
    auto* pathLayout = new QFormLayout(pathGroup);

    m_lblBin4g = new QLabel("4G init file (.bin):", pathGroup);
    auto* bin4gRow = new QHBoxLayout;
    m_bin4gPath    = new QLineEdit(pathGroup);
    m_bin4gPath->setPlaceholderText("path/to/imx-boot_4G.bin");
    m_btnBin4g     = new QPushButton("Browse…", pathGroup);
    bin4gRow->addWidget(m_bin4gPath);
    bin4gRow->addWidget(m_btnBin4g);
    pathLayout->addRow(m_lblBin4g, bin4gRow);

    m_lblBin = new QLabel("Boot file (.bin / .imx):", pathGroup);
    auto* binRow   = new QHBoxLayout;
    m_binPath      = new QLineEdit(pathGroup);
    m_binPath->setPlaceholderText("path/to/imx-boot.bin");
    m_btnBin       = new QPushButton("Browse…", pathGroup);
    binRow->addWidget(m_binPath);
    binRow->addWidget(m_btnBin);
    pathLayout->addRow(m_lblBin, binRow);

    m_lblWic = new QLabel("Image file (.wic):", pathGroup);
    auto* wicRow   = new QHBoxLayout;
    m_wicPath      = new QLineEdit(pathGroup);
    m_wicPath->setPlaceholderText("path/to/image.wic");
    m_btnWic       = new QPushButton("Browse…", pathGroup);
    wicRow->addWidget(m_wicPath);
    wicRow->addWidget(m_btnWic);
    pathLayout->addRow(m_lblWic, wicRow);

    m_lblDelay = new QLabel("Delay between phases (s):", pathGroup);
    m_spinDelay = new QSpinBox(pathGroup);
    m_spinDelay->setRange(1, 30);
    m_spinDelay->setValue(2);
    m_spinDelay->setToolTip("Seconds to wait after Phase 1 before starting Phase 2.\n"
                             "Increase to 5–10 if device doesn't re-enumerate in time.");
    pathLayout->addRow(m_lblDelay, m_spinDelay);

    root->addWidget(pathGroup);

    // --- Buttons ---
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_btnOk     = buttons->button(QDialogButtonBox::Ok);
    m_btnCancel = buttons->button(QDialogButtonBox::Cancel);
    root->addWidget(buttons);

    // --- Connections ---
    connect(m_rdSimple,  &QRadioButton::toggled, this, &PresetDialog::onTypeChanged);
    connect(m_rdEmmc,    &QRadioButton::toggled, this, &PresetDialog::onTypeChanged);
    connect(m_rdEmmc4g,  &QRadioButton::toggled, this, &PresetDialog::onTypeChanged);
    connect(m_btnBin4g,  &QPushButton::clicked,  this, &PresetDialog::browseBin4g);
    connect(m_btnBin,    &QPushButton::clicked,  this, &PresetDialog::browseBin);
    connect(m_btnWic,    &QPushButton::clicked,  this, &PresetDialog::browseWic);
    connect(m_name,      &QLineEdit::textChanged, this, &PresetDialog::validate);
    connect(m_bin4gPath, &QLineEdit::textChanged, this, &PresetDialog::validate);
    connect(m_binPath,   &QLineEdit::textChanged, this, &PresetDialog::validate);
    connect(m_wicPath,   &QLineEdit::textChanged, this, &PresetDialog::validate);
    connect(buttons,    &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons,    &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void PresetDialog::populate(const FirmwarePreset& p)
{
    m_name->setText(p.name);
    m_bin4gPath->setText(p.bin4gPath);
    m_binPath->setText(p.binPath);
    m_wicPath->setText(p.wicPath);
    m_spinDelay->setValue(p.phaseDelay);
    if (p.type == FirmwarePreset::Type::EmmcAll4G)
        m_rdEmmc4g->setChecked(true);
    else if (p.type == FirmwarePreset::Type::EmmcAll)
        m_rdEmmc->setChecked(true);
    else
        m_rdSimple->setChecked(true);
}

void PresetDialog::onTypeChanged()
{
    bool emmc   = m_rdEmmc->isChecked();
    bool emmc4g = m_rdEmmc4g->isChecked();
    bool needWic    = emmc || emmc4g;
    bool needBin4g  = emmc4g;

    m_lblBin4g->setVisible(needBin4g);
    m_bin4gPath->setVisible(needBin4g);
    m_btnBin4g->setVisible(needBin4g);

    m_lblWic->setVisible(needWic);
    m_wicPath->setVisible(needWic);
    m_btnWic->setVisible(needWic);

    m_lblDelay->setVisible(needBin4g);
    m_spinDelay->setVisible(needBin4g);

    m_lblBin->setText((emmc || emmc4g)
        ? "Bootloader (.bin / .imx):"
        : "Boot file (.bin / .imx):");

    validate();
}

void PresetDialog::browseBin4g()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Select 4G init file", m_bin4gPath->text(),
        "Binary files (*.bin *.imx);;All files (*)");
    if (!path.isEmpty())
        m_bin4gPath->setText(path);
}

void PresetDialog::browseBin()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Select boot file", m_binPath->text(),
        "Binary files (*.bin *.imx);;All files (*)");
    if (!path.isEmpty())
        m_binPath->setText(path);
}

void PresetDialog::browseWic()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Select image file", m_wicPath->text(),
        "WIC images (*.wic *.img);;All files (*)");
    if (!path.isEmpty())
        m_wicPath->setText(path);
}

void PresetDialog::validate()
{
    if (!m_btnOk) return;
    auto exists = [](QLineEdit* e){ const QString t = e->text().trimmed(); return !t.isEmpty() && QFileInfo::exists(t); };

    bool ok = !m_name->text().trimmed().isEmpty() && exists(m_binPath);

    if (m_rdEmmc->isChecked())
        ok = ok && exists(m_wicPath);
    else if (m_rdEmmc4g->isChecked())
        ok = ok && exists(m_bin4gPath) && exists(m_wicPath);

    m_btnOk->setEnabled(ok);
}

FirmwarePreset PresetDialog::preset() const
{
    FirmwarePreset p;
    p.id       = m_editId.isEmpty()
                     ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                     : m_editId;
    p.name     = m_name->text().trimmed();
    p.bin4gPath  = m_bin4gPath->text().trimmed();
    p.binPath    = m_binPath->text().trimmed();
    p.wicPath    = m_wicPath->text().trimmed();
    p.phaseDelay = m_spinDelay->value();
    if (m_rdEmmc4g->isChecked())
        p.type = FirmwarePreset::Type::EmmcAll4G;
    else if (m_rdEmmc->isChecked())
        p.type = FirmwarePreset::Type::EmmcAll;
    else
        p.type = FirmwarePreset::Type::SimpleBin;
    return p;
}
