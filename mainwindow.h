#pragma once
#include "firmwarepreset.h"
#include "devicemonitor.h"
#include <QMainWindow>
#include <QMap>
#include <QTranslator>

class QGroupBox;
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
    void changeEvent(QEvent* event) override;

private slots:
    // Settings
    void openSettings();

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
    void flashDevice(DeviceItemWidget* widget, const FirmwarePreset& preset);
    void onDevicePermissionError(DeviceItemWidget* widget);
    void onDeviceAuthFailed(DeviceItemWidget* widget);
    // Obtain the session password (prompting once) and retry the device elevated.
    // Serialized so parallel flashes don't stack password dialogs.
    void requestElevation(DeviceItemWidget* widget, bool passwordWasWrong);

    // Auto-flash
    void onAutoFlashToggled(bool enabled);

private:
    // --- Setup ---
    void setupUi();
    QWidget* makePresetsPanel();
    QWidget* makeDevicesPanel();
    QWidget* makeBottomBar();

    // --- Persistence ---
    void loadSettings();
    void saveSettings();

    // --- Helpers ---
    void refreshPresetList();
    void retranslateUi();
    void applyLanguage(const QString& lang);
    void applySettings();
    FirmwarePreset* selectedPreset();
    void addDeviceWidget(const UsbDevice& dev);
    // Prompt for the administrator password (cached for the session). Returns
    // false if the user cancelled.
    bool promptPassword(bool incorrect);

    // --- Data ---
    QList<FirmwarePreset> m_presets;
    DeviceMonitor*        m_monitor          = nullptr;
    int                   m_activeFlashCount = 0;
    QString               m_helperPath;
    QString               m_sessionPassword;     // cached administrator password
    bool                  m_useElevation     = false;
    bool                  m_promptInProgress = false;
    QList<DeviceItemWidget*> m_pendingElevation; // devices awaiting the in-flight prompt
    QTranslator           m_translator;

    // busId → widget
    QMap<QString, DeviceItemWidget*> m_deviceWidgets;

    // --- UI elements ---
    QGroupBox* m_groupPresets = nullptr;
    QGroupBox* m_groupDevices = nullptr;
    QListWidget* m_presetList   = nullptr;
    QPushButton* m_btnAdd       = nullptr;
    QPushButton* m_btnEdit      = nullptr;
    QPushButton* m_btnDelete    = nullptr;

    QVBoxLayout* m_devicesLayout= nullptr;
    QLabel*      m_noDevicesLbl = nullptr;

    QPushButton* m_btnSettings      = nullptr;
    QCheckBox*   m_autoFlash        = nullptr;
    QComboBox*   m_autoPreset       = nullptr;
    QCheckBox*   m_chkRebootAfter   = nullptr;
    QPushButton* m_btnFlashSel      = nullptr;
    QLabel*      m_statusBar        = nullptr;
};
