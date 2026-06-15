#pragma once
#include <QDialog>

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
    QString language()       const;   // "en" or "ru"

private slots:
    void browseUuu();

private:
    void setupUi();
    void loadFromSettings();

    static QStringList findUuuBinaries();

    QComboBox*   m_uuuCombo  = nullptr;
    QPushButton* m_uuuBrowse = nullptr;
    QComboBox*   m_sudoCombo = nullptr;
    QComboBox*   m_langCombo = nullptr;
};
