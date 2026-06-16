#pragma once
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    QString uuuPath()        const;
    QString privilegePrefix() const;
    QString language()       const;
    bool    saveLogsEnabled() const;
    QString logDir()         const;

signals:
    void languageChanged(const QString& lang);
    void settingsSaved();

private slots:
    void browseUuu();
    void browseLogDir();
    void save();

private:
    void setupUi();
    void loadFromSettings();

    static QStringList findUuuBinaries();

    QComboBox*   m_uuuCombo    = nullptr;
    QPushButton* m_uuuBrowse   = nullptr;
    QComboBox*   m_sudoCombo   = nullptr;
    QComboBox*   m_langCombo   = nullptr;
    QCheckBox*   m_chkSaveLogs = nullptr;
    QLineEdit*   m_logDirEdit  = nullptr;
    QPushButton* m_logDirBrowse = nullptr;
};
