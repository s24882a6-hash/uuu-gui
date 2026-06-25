#pragma once
#include <QDialog>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

signals:
    void languageChanged(const QString& lang);
    void themeChanged(const QString& theme);
    void settingsSaved();

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void browseLogDir();
    void save();

private:
    void setupUi();
    void loadFromSettings();
    void retranslateUi();

    QGroupBox*   m_generalGroup  = nullptr;
    QGroupBox*   m_logsGroup     = nullptr;
    QLabel*      m_langLabel     = nullptr;
    QComboBox*   m_langCombo     = nullptr;
    QLabel*      m_themeLabel    = nullptr;
    QComboBox*   m_themeCombo    = nullptr;
    QCheckBox*   m_chkSaveLogs = nullptr;
    QLineEdit*   m_logDirEdit  = nullptr;
    QPushButton* m_logDirBrowse = nullptr;
};
