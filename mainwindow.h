#pragma once
#include "firmwarepreset.h"
#include "devicemonitor.h"
#include <QMainWindow>
#include <QMap>
#include <QTranslator>

class QGroupBox;
class QComboBox;
class QCheckBox;
class QSplitter;
class QTextEdit;
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
    static void applyTheme(const QString& theme);

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

    // Devices
    void onDeviceConnected(UsbDevice dev);
    void onDeviceDisconnected(QString busId);
    void onMonitoringUnavailable(QString reason);

    // Flash
    void flashSelected();
    void flashDevice(DeviceItemWidget* widget, const FirmwarePreset& preset);
    void onDevicePermissionError(DeviceItemWidget* widget);
    void onDeviceAuthFailed(DeviceItemWidget* widget);
    // Resolve how to access the device (udev rule / sudo password / abort) and
    // retry it. Serialized so parallel flashes don't stack dialogs.
    void requestElevation(DeviceItemWidget* widget, bool passwordWasWrong);

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
    void updatePresetFilesLabel();
    void addDeviceWidget(const UsbDevice& dev);
    // Prompt for the administrator password (cached for the session). Returns
    // false if the user cancelled.
    bool promptPassword(bool incorrect);
    enum class ElevationOutcome { Elevated, Unprivileged, Aborted };
    // Ask the user how to gain device access: on Linux offers a one-time udev
    // rule install (via pkexec) before falling back to the sudo password.
    ElevationOutcome resolveElevation(bool passwordWasWrong);
#ifdef Q_OS_LINUX
    bool installUdevRule();
#endif

    // --- Data ---
    QList<FirmwarePreset> m_presets;
    DeviceMonitor*        m_monitor          = nullptr;
    QString               m_helperPath;
    QString               m_sessionPassword;     // cached administrator password
    bool                  m_useElevation     = false;
    bool                  m_promptInProgress = false;
    bool                  m_udevOfferDeclined = false;
    QList<DeviceItemWidget*> m_pendingElevation; // devices awaiting the in-flight prompt
    QTranslator           m_translator;

    // busId → widget
    QMap<QString, DeviceItemWidget*> m_deviceWidgets;

    // --- UI elements ---
    QSplitter*   m_splitter          = nullptr;
    QGroupBox*   m_groupPresets     = nullptr;
    QGroupBox*   m_groupDevices     = nullptr;
    QComboBox*   m_presetCombo      = nullptr;
    QTextEdit*   m_presetFilesLbl   = nullptr;
    QPushButton* m_btnAdd           = nullptr;
    QPushButton* m_btnEdit          = nullptr;
    QPushButton* m_btnDelete        = nullptr;

    QVBoxLayout* m_devicesLayout= nullptr;
    QLabel*      m_noDevicesLbl = nullptr;

    QPushButton* m_btnSettings      = nullptr;
    QCheckBox*   m_autoFlash        = nullptr;
    QCheckBox*   m_chkRebootAfter   = nullptr;
    QPushButton* m_btnFlashSel      = nullptr;
    QLabel*      m_statusBar        = nullptr;
};
