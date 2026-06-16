#include "settingsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
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

    // --- Flash Logs ---
    auto* logsGroup  = new QGroupBox(tr("Flash Logs"), this);
    auto* logsLayout = new QVBoxLayout(logsGroup);

    m_chkSaveLogs = new QCheckBox(tr("Save logs to file"), logsGroup);
    logsLayout->addWidget(m_chkSaveLogs);

    auto* dirRow   = new QHBoxLayout;
    m_logDirEdit   = new QLineEdit(logsGroup);
    m_logDirEdit->setPlaceholderText(tr("Log directory…"));
    m_logDirBrowse = new QPushButton(tr("Browse…"), logsGroup);
    m_logDirBrowse->setFixedWidth(80);
    dirRow->addWidget(m_logDirEdit, 1);
    dirRow->addWidget(m_logDirBrowse);
    logsLayout->addLayout(dirRow);
    root->addWidget(logsGroup);

    auto updateLogDirState = [this]() {
        m_logDirEdit->setEnabled(m_chkSaveLogs->isChecked());
        m_logDirBrowse->setEnabled(m_chkSaveLogs->isChecked());
    };
    updateLogDirState();

    connect(m_uuuBrowse,    &QPushButton::clicked, this, &SettingsDialog::browseUuu);
    connect(m_logDirBrowse, &QPushButton::clicked, this, &SettingsDialog::browseLogDir);
    connect(m_chkSaveLogs,  &QCheckBox::toggled,   this, [this, updateLogDirState]() {
        updateLogDirState();
        save();
    });
    connect(m_logDirEdit,   &QLineEdit::textChanged, this, [this]() { save(); });
    connect(m_langCombo, &QComboBox::currentIndexChanged, this, [this]() {
        emit languageChanged(m_langCombo->currentData().toString());
        save();
    });
    connect(m_sudoCombo, &QComboBox::currentIndexChanged, this, [this]() { save(); });
    connect(m_uuuCombo, &QComboBox::currentTextChanged, this, [this]() { save(); });
}

void SettingsDialog::loadFromSettings()
{
    // Block all auto-save signals while restoring values.
    // Without this, setting one widget triggers save() before the others are
    // populated, which can overwrite QSettings with incomplete data.
    QSignalBlocker b1(m_uuuCombo);
    QSignalBlocker b2(m_sudoCombo);
    QSignalBlocker b3(m_langCombo);
    QSignalBlocker b4(m_chkSaveLogs);
    QSignalBlocker b5(m_logDirEdit);

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

    // Restore language (apply silently — already active in the app)
    QString savedLang = s.value("language", "en").toString();
    int lidx = m_langCombo->findData(savedLang);
    if (lidx >= 0) m_langCombo->setCurrentIndex(lidx);

    // Restore log settings
    m_chkSaveLogs->setChecked(s.value("saveLogs", false).toBool());
    m_logDirEdit->setText(s.value("logDir",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString());
    m_logDirEdit->setEnabled(m_chkSaveLogs->isChecked());
    m_logDirBrowse->setEnabled(m_chkSaveLogs->isChecked());
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

void SettingsDialog::browseLogDir()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select log directory"),
                                                    m_logDirEdit->text());
    if (!dir.isEmpty())
        m_logDirEdit->setText(dir);
}

void SettingsDialog::save()
{
    QSettings s("uuuapp", "UUUFlashTool");
    s.setValue("uuuPath",    m_uuuCombo->currentText().trimmed());
    s.setValue("sudoPrefix", m_sudoCombo->currentData().toString());
    s.setValue("language",   m_langCombo->currentData().toString());
    s.setValue("saveLogs",   m_chkSaveLogs->isChecked());
    s.setValue("logDir",     m_logDirEdit->text().trimmed());
    emit settingsSaved();
}

QString SettingsDialog::uuuPath() const        { return m_uuuCombo->currentText().trimmed(); }
QString SettingsDialog::privilegePrefix() const { return m_sudoCombo->currentData().toString(); }
QString SettingsDialog::language() const        { return m_langCombo->currentData().toString(); }
bool    SettingsDialog::saveLogsEnabled() const { return m_chkSaveLogs->isChecked(); }
QString SettingsDialog::logDir() const          { return m_logDirEdit->text().trimmed(); }

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
