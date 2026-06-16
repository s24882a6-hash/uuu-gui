#pragma once
#include "firmwarepreset.h"
#include "devicemonitor.h"
#include <QMainWindow>
#include <QMap>
#include <QTranslator>

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
    void flashDevice(DeviceItemWidget* widget);

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
    void applyUuuSettings(const QString& uuuPath, const QString& sudoPrefix);
    FirmwarePreset* selectedPreset();
    void addDeviceWidget(const UsbDevice& dev);

    QString currentUuuPath()    const { return m_uuuPath;    }
    QString currentSudoPrefix() const { return m_sudoPrefix; }

    // --- Data ---
    QList<FirmwarePreset> m_presets;
    DeviceMonitor*        m_monitor          = nullptr;
    int                   m_activeFlashCount = 0;
    QString               m_uuuPath;
    QString               m_sudoPrefix;
    QTranslator           m_translator;

    // busId → widget
    QMap<QString, DeviceItemWidget*> m_deviceWidgets;

    // --- UI elements ---
    class QGroupBox* m_groupPresets = nullptr;
    class QGroupBox* m_groupDevices = nullptr;
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
