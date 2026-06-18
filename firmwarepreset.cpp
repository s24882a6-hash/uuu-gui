#include "firmwarepreset.h"
#include <QFileInfo>
#include <QUuid>
#include <QJsonObject>

FirmwarePreset::FirmwarePreset(const QString& name)
    : id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , name(name)
{}

QList<QStringList> FirmwarePreset::buildHelperPhases() const
{
    switch (type) {
    case Type::SimpleBin:
        return { { "--boot", binPath } };
    case Type::EmmcAll:
        return { { "--emmcall", binPath, wicPath } };
    case Type::EmmcAll4G:
        return { { "--boot", bin4gPath }, { "--emmcall", binPath, wicPath } };
    }
    return {};
}

bool FirmwarePreset::isValid() const
{
    if (name.trimmed().isEmpty()) return false;
    auto exists = [](const QString& p){ return !p.trimmed().isEmpty() && QFileInfo::exists(p); };
    switch (type) {
    case Type::SimpleBin:
        return exists(binPath);
    case Type::EmmcAll:
        return exists(binPath) && exists(wicPath);
    case Type::EmmcAll4G:
        return exists(bin4gPath) && exists(binPath) && exists(wicPath);
    }
    return false;
}

QString FirmwarePreset::description() const
{
    switch (type) {
    case Type::SimpleBin:
        return QString("uuu %1").arg(QFileInfo(binPath).fileName());
    case Type::EmmcAll:
        return QString("uuu -b emmc_all %1 %2")
            .arg(QFileInfo(binPath).fileName())
            .arg(QFileInfo(wicPath).fileName());
    case Type::EmmcAll4G:
        return QString("uuu %1 → uuu -b emmc_all %2 %3")
            .arg(QFileInfo(bin4gPath).fileName())
            .arg(QFileInfo(binPath).fileName())
            .arg(QFileInfo(wicPath).fileName());
    }
    return {};
}

QJsonObject FirmwarePreset::toJson() const
{
    QJsonObject obj;
    obj["version"]    = 1;
    obj["id"]         = id;
    obj["name"]       = name;
    obj["type"]       = static_cast<int>(type);
    obj["bin4gPath"]  = bin4gPath;
    obj["binPath"]    = binPath;
    obj["wicPath"]    = wicPath;
    obj["phaseDelay"] = phaseDelay;
    return obj;
}

FirmwarePreset FirmwarePreset::fromJson(const QJsonObject& obj)
{
    FirmwarePreset p;
    p.id       = obj["id"].toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    p.name     = obj["name"].toString();
    int typeInt = obj["type"].toInt(0);
    if (typeInt < 0 || typeInt > static_cast<int>(Type::EmmcAll4G))
        typeInt = 0;
    p.type = static_cast<Type>(typeInt);
    p.bin4gPath  = obj["bin4gPath"].toString();
    p.binPath    = obj["binPath"].toString();
    p.wicPath    = obj["wicPath"].toString();
    p.phaseDelay = obj["phaseDelay"].toInt(2);
    return p;
}
