#include "apppaths.h"
#include <QCoreApplication>
#include <QDir>

QString AppPaths::helper()
{
#ifdef Q_OS_WIN
    static const QString name = QStringLiteral("uuu-helper.exe");
#else
    static const QString name = QStringLiteral("uuu-helper");
#endif
    return QDir(QCoreApplication::applicationDirPath()).filePath(name);
}
