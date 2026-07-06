#pragma once
#include "firmwarepreset.h"
#include <QDialog>

class QLineEdit;
class QRadioButton;
class QLabel;
class QPushButton;
class QSpinBox;

class PresetDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PresetDialog(QWidget* parent = nullptr);
    explicit PresetDialog(const FirmwarePreset& preset, QWidget* parent = nullptr);

    FirmwarePreset preset() const;

private slots:
    void onTypeChanged();
    void validate();

private:
    void setupUi();
    void populate(const FirmwarePreset& p);
    QString autoName() const;

    QLineEdit*    m_name        = nullptr;
    QRadioButton* m_rdSimple    = nullptr;
    QRadioButton* m_rdEmmc      = nullptr;
    QRadioButton* m_rdEmmc4g    = nullptr;
    QLabel*       m_lblBin4g    = nullptr;
    QLineEdit*    m_bin4gPath   = nullptr;
    QPushButton*  m_btnBin4g    = nullptr;
    QLabel*       m_lblBin      = nullptr;
    QLineEdit*    m_binPath     = nullptr;
    QPushButton*  m_btnBin      = nullptr;
    QLabel*       m_lblWic      = nullptr;
    QLineEdit*    m_wicPath     = nullptr;
    QPushButton*  m_btnWic      = nullptr;
    QLabel*       m_lblDelay    = nullptr;
    QSpinBox*     m_spinDelay   = nullptr;
    QPushButton*  m_btnOk       = nullptr;
    QPushButton*  m_btnCancel   = nullptr;

    QString m_editId;  // non-empty when editing an existing preset
};
