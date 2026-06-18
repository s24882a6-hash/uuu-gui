#include "settingsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
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

    // --- Language ---
    m_generalGroup    = new QGroupBox(tr("General"), this);
    auto* otherGroup  = m_generalGroup;
    auto* otherLayout = new QFormLayout(otherGroup);

    m_langCombo = new QComboBox(otherGroup);
    m_langCombo->addItem("English", "en");
    m_langCombo->addItem("Русский", "ru");
    m_langLabel = new QLabel(tr("Language:"), otherGroup);
    otherLayout->addRow(m_langLabel, m_langCombo);

    root->addWidget(otherGroup);

    // --- Flash Logs ---
    m_logsGroup      = new QGroupBox(tr("Flash Logs"), this);
    auto* logsGroup  = m_logsGroup;
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
}

void SettingsDialog::loadFromSettings()
{
    // Block all auto-save signals while restoring values.
    // Without this, setting one widget triggers save() before the others are
    // populated, which can overwrite QSettings with incomplete data.
    QSignalBlocker b3(m_langCombo);
    QSignalBlocker b4(m_chkSaveLogs);
    QSignalBlocker b5(m_logDirEdit);

    QSettings s;

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

void SettingsDialog::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void SettingsDialog::retranslateUi()
{
    setWindowTitle(tr("Settings"));
    m_generalGroup->setTitle(tr("General"));
    m_langLabel->setText(tr("Language:"));
    m_logsGroup->setTitle(tr("Flash Logs"));
    m_chkSaveLogs->setText(tr("Save logs to file"));
    m_logDirEdit->setPlaceholderText(tr("Log directory…"));
    m_logDirBrowse->setText(tr("Browse…"));
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
    QSettings s;
    s.setValue("language",   m_langCombo->currentData().toString());
    s.setValue("saveLogs",   m_chkSaveLogs->isChecked());
    s.setValue("logDir",     m_logDirEdit->text().trimmed());
    emit settingsSaved();
}
