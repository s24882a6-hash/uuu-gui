#include "mainwindow.h"
#include <QApplication>
#include <QStyleFactory>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("UUU Flash Tool");
    app.setOrganizationName("uuuapp");
    app.setApplicationVersion("1.0.0");

    MainWindow w;
    w.show();
    return app.exec();
}
