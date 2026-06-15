#pragma once
#include <QString>
#include <QStringList>
#include <QJsonObject>

class FirmwarePreset {
public:
    enum class Type {
        SimpleBin,   // uuu <file.bin>
        EmmcAll,     // uuu -b emmc_all <bootloader> <wic>
        EmmcAll4G    // uuu <4g.bin>  →  uuu -b emmc_all <bootloader> <wic>
    };

    QString id;
    QString name;
    Type    type        = Type::SimpleBin;
    QString bin4gPath;  // EmmcAll4G only: first-phase 4G init .bin
    QString binPath;    // SimpleBin: the .bin; EmmcAll/EmmcAll4G: bootloader path
    QString wicPath;    // EmmcAll/EmmcAll4G: the .wic image path
    int     phaseDelay  = 2; // seconds to wait between phases (EmmcAll4G)

    FirmwarePreset() = default;
    explicit FirmwarePreset(const QString& name);

    // Returns one or more argument lists, each to be passed as a separate uuu invocation
    QList<QStringList> buildAllPhases() const;

    bool    isValid()      const;
    QString description()  const;

    QJsonObject          toJson()               const;
    static FirmwarePreset fromJson(const QJsonObject& obj);
};
