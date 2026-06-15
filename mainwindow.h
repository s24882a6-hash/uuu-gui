#pragma once
#include "firmwarepreset.h"
#include "devicemonitor.h"
#include <QMainWindow>
#include <QMap>

class QListWidget;
class QListWidgetItem;
class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class DeviceItemWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // UUU binary
    void browseUuu();
    void onUuuChanged(int idx);

    // Presets
    void addPreset();
    void editPreset();
    void deletePreset();
    void onPresetSelectionChanged();

    // Devices
    void onDeviceConnected(UsbDevice dev);
    void onDeviceDisconnected(QString busId);
    void onMonitoringUnavailable(QString reason);

    // Flash
    void flashSelected();
    void flashDevice(DeviceItemWidget* widget);

    // Auto-flash
    void onAutoFlashToggled(bool enabled);

private:
    // --- Setup ---
    void setupUi();
    QWidget* makeUuuBar();
    QWidget* makePresetsPanel();
    QWidget* makeDevicesPanel();
    QWidget* makeBottomBar();

    // --- Persistence ---
    void loadSettings();
    void saveSettings();

    // --- Helpers ---
    void refreshPresetList();
    void refreshUuuDropdown();
    QStringList findUuuBinaries();
    QString     currentUuuPath() const;
    QString     currentSudoPrefix() const;
    FirmwarePreset* selectedPreset();
    void addDeviceWidget(const UsbDevice& dev);

    // --- Data ---
    QList<FirmwarePreset> m_presets;
    DeviceMonitor*        m_monitor          = nullptr;
    int                   m_activeFlashCount = 0;

    // busId → widget
    QMap<QString, DeviceItemWidget*> m_deviceWidgets;

    // --- UI elements ---
    QComboBox*   m_uuuCombo     = nullptr;
    QPushButton* m_uuuBrowse    = nullptr;
    QComboBox*   m_sudoCombo    = nullptr;   // "", "sudo", "pkexec"

    QListWidget* m_presetList   = nullptr;
    QPushButton* m_btnAdd       = nullptr;
    QPushButton* m_btnEdit      = nullptr;
    QPushButton* m_btnDelete    = nullptr;

    QVBoxLayout* m_devicesLayout= nullptr;   // inside scroll area
    QLabel*      m_noDevicesLbl = nullptr;

    QCheckBox*   m_autoFlash        = nullptr;
    QComboBox*   m_autoPreset       = nullptr;
    QCheckBox*   m_chkRebootAfter   = nullptr;
    QPushButton* m_btnFlashSel      = nullptr;
    QLabel*      m_statusBar        = nullptr;
};
