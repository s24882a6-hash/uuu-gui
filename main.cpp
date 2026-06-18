#include "mainwindow.h"
#include <QApplication>
#include <QIcon>
#include <QLocale>
#include <QSettings>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    // Identifies the QSettings storage path (org/app) for the default QSettings()
    // constructor used throughout the app. Keep "UUUFlashTool" to preserve the
    // existing settings location. The user-facing title is a separate tr() string.
    app.setApplicationName("UUUFlashTool");
    app.setOrganizationName("uuuapp");
    app.setApplicationVersion(APP_VERSION);
    app.setWindowIcon(QIcon(":/icons/uuuapp.svg"));

    // Set default language on first run (detect system locale)
    {
        QSettings appSettings;
        if (appSettings.value("language").toString().isEmpty()) {
            QString lang = (QLocale::system().language() == QLocale::Russian) ? "ru" : "en";
            appSettings.setValue("language", lang);
        }
    }
    // Translator is managed by MainWindow::applyLanguage via m_translator

    MainWindow w;
    w.show();
    return app.exec();
}
