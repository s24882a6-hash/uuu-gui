#include "settingsdialog.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    setMinimumWidth(460);
    setupUi();
    loadFromSettings();
}

void SettingsDialog::setupUi()
{
    auto* root = new QVBoxLayout(this);
    root->setSpacing(12);

    // --- UUU binary ---
    auto* uuuGroup  = new QGroupBox(tr("UUU Binary"), this);
    auto* uuuLayout = new QHBoxLayout(uuuGroup);

    m_uuuCombo = new QComboBox(uuuGroup);
    m_uuuCombo->setEditable(true);
    m_uuuCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_uuuBrowse = new QPushButton(tr("Browse…"), uuuGroup);
    m_uuuBrowse->setFixedWidth(80);

    uuuLayout->addWidget(m_uuuCombo, 1);
    uuuLayout->addWidget(m_uuuBrowse);
    root->addWidget(uuuGroup);

    // --- Privilege + Language ---
    auto* otherGroup  = new QGroupBox(tr("General"), this);
    auto* otherLayout = new QFormLayout(otherGroup);

    m_sudoCombo = new QComboBox(otherGroup);
    m_sudoCombo->addItem(tr("None (run as-is)"), "");
#ifdef Q_OS_LINUX
    m_sudoCombo->addItem("sudo",   "sudo");
    m_sudoCombo->addItem("pkexec", "pkexec");
#elif defined(Q_OS_MAC)
    m_sudoCombo->addItem("sudo", "sudo");
#endif
    otherLayout->addRow(tr("Privilege:"), m_sudoCombo);

    m_langCombo = new QComboBox(otherGroup);
    m_langCombo->addItem("English", "en");
    m_langCombo->addItem("Русский", "ru");
    otherLayout->addRow(tr("Language:"), m_langCombo);

    root->addWidget(otherGroup);

    connect(m_uuuBrowse, &QPushButton::clicked, this, &SettingsDialog::browseUuu);
    connect(m_langCombo, &QComboBox::currentIndexChanged, this, [this]() {
        emit languageChanged(m_langCombo->currentData().toString());
        save();
    });
    connect(m_sudoCombo, &QComboBox::currentIndexChanged, this, [this]() { save(); });
    connect(m_uuuCombo, &QComboBox::currentTextChanged, this, [this]() { save(); });
}

void SettingsDialog::loadFromSettings()
{
    // Populate uuu combo with found binaries
    QStringList found = findUuuBinaries();
    for (const QString& p : found)
        m_uuuCombo->addItem(p);

    QSettings s("uuuapp", "UUUFlashTool");

    // Restore saved uuu path
    QString savedUuu = s.value("uuuPath").toString();
    if (!savedUuu.isEmpty()) {
        int idx = m_uuuCombo->findText(savedUuu);
        if (idx >= 0)
            m_uuuCombo->setCurrentIndex(idx);
        else
            m_uuuCombo->setEditText(savedUuu);
    }

    // Restore privilege
    QString savedPrefix = s.value("sudoPrefix").toString();
    int pidx = m_sudoCombo->findData(savedPrefix);
    if (pidx >= 0) m_sudoCombo->setCurrentIndex(pidx);

    // Restore language
    QString savedLang = s.value("language", "en").toString();
    int lidx = m_langCombo->findData(savedLang);
    if (lidx >= 0) m_langCombo->setCurrentIndex(lidx);
}

void SettingsDialog::browseUuu()
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

void SettingsDialog::save()
{
    QSettings s("uuuapp", "UUUFlashTool");
    s.setValue("uuuPath",    m_uuuCombo->currentText().trimmed());
    s.setValue("sudoPrefix", m_sudoCombo->currentData().toString());
    s.setValue("language",   m_langCombo->currentData().toString());
    emit settingsSaved();
}

QString SettingsDialog::uuuPath() const
{
    return m_uuuCombo->currentText().trimmed();
}

QString SettingsDialog::privilegePrefix() const
{
    return m_sudoCombo->currentData().toString();
}

QString SettingsDialog::language() const
{
    return m_langCombo->currentData().toString();
}

QStringList SettingsDialog::findUuuBinaries()
{
    QStringList candidates;

    QString inPath = QStandardPaths::findExecutable("uuu");
    if (!inPath.isEmpty()) candidates << inPath;

#ifdef Q_OS_WIN
    for (const QString& p : {"C:/Program Files/uuu/uuu.exe", "C:/uuu/uuu.exe"})
        if (QFileInfo::exists(p)) candidates << p;
#elif defined(Q_OS_MAC)
    for (const QString& p : {"/usr/local/bin/uuu", "/opt/homebrew/bin/uuu"})
        if (QFileInfo::exists(p) && !candidates.contains(p)) candidates << p;
#else
    for (const QString& p : {"/usr/local/bin/uuu", "/usr/bin/uuu"})
        if (QFileInfo::exists(p) && !candidates.contains(p)) candidates << p;
#endif

    return candidates;
}
