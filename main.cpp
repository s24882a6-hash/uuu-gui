#include "mainwindow.h"
#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#ifdef Q_OS_WIN
#  include <QSettings>
#endif

#ifdef Q_OS_WIN
static bool windowsDarkModeEnabled()
{
    QSettings reg(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        QSettings::NativeFormat);
    return reg.value("AppsUseLightTheme", 1).toInt() == 0;
}

static void applyDarkPalette(QApplication& app)
{
    QPalette p;
    p.setColor(QPalette::Window,          QColor(32,  32,  32));
    p.setColor(QPalette::WindowText,      QColor(220, 220, 220));
    p.setColor(QPalette::Base,            QColor(18,  18,  18));
    p.setColor(QPalette::AlternateBase,   QColor(40,  40,  40));
    p.setColor(QPalette::ToolTipBase,     QColor(50,  50,  50));
    p.setColor(QPalette::ToolTipText,     QColor(220, 220, 220));
    p.setColor(QPalette::Text,            QColor(220, 220, 220));
    p.setColor(QPalette::Button,          QColor(45,  45,  45));
    p.setColor(QPalette::ButtonText,      QColor(220, 220, 220));
    p.setColor(QPalette::BrightText,      Qt::red);
    p.setColor(QPalette::Link,            QColor(42, 130, 218));
    p.setColor(QPalette::Highlight,       QColor(42, 130, 218));
    p.setColor(QPalette::HighlightedText, Qt::black);
    p.setColor(QPalette::Disabled, QPalette::Text,       QColor(100, 100, 100));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(100, 100, 100));
    app.setPalette(p);
}
#endif

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    if (windowsDarkModeEnabled()) {
        QApplication::setStyle("fusion");
    } else {
        QApplication::setStyle("windowsvista");
    }
#endif
    QApplication app(argc, argv);
    app.setApplicationName("UUU Flash Tool");
    app.setOrganizationName("uuuapp");
    app.setApplicationVersion("1.0.0");

#ifdef Q_OS_WIN
    if (windowsDarkModeEnabled())
        applyDarkPalette(app);
#endif

    MainWindow w;
    w.show();
    return app.exec();
}
