#include "mainwindow.h"
#include <QApplication>
#include <QIcon>
#include <QLocale>
#include <QSettings>
#include <QStyleHints>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    // Identifies the QSettings storage path (org/app) for the default QSettings()
    // constructor used throughout the app. Keep "UUUFlashTool" to preserve the
    // existing settings location. The user-facing title is a separate tr() string.
    app.setApplicationName("UUUFlashTool");
    app.setOrganizationName("UUUFlashTool");
    app.setApplicationVersion(APP_VERSION);
#ifndef Q_OS_MAC
    // On macOS the Dock uses the bundle's .icns (rounded, safe-area); setting a
    // window icon here would override it with the full-bleed square. Other
    // platforms need it for the title bar / taskbar.
    app.setWindowIcon(QIcon(":/icons/UUUFlashTool.svg"));
#endif

    // Apply saved color scheme before any window is created.
    {
        QSettings s;
        QString theme = s.value("theme", "system").toString();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        if (theme == "light")
            app.styleHints()->setColorScheme(Qt::ColorScheme::Light);
        else if (theme == "dark")
            app.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
        // "system" → leave at Qt::ColorScheme::Unknown (default)
#endif
    }

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
