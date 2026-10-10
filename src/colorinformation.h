#ifndef COLORINFORMATION_H
#define COLORINFORMATION_H

#include <QByteArray>
#include <QColorSpace>
#include <QString>

namespace QvColor {
enum class ICCState { Unknown, Absent, Embedded, Invalid, Unsupported };
enum class Origin { Unspecified, ICC, Container };
struct SourceInfo
{
    ICCState iccState{ ICCState::Unknown };
    Origin origin{ Origin::Unspecified };
    QByteArray iccData;
    QString spaceName;
    QString profileDescription;
    QString decoderSpace;
    bool isRaw{ false };
    bool isVector{ false };
    bool conflictingDeclarations{ false };
};
struct Information
{
    SourceInfo source;
    QString outputSpace;
    bool hasOutput{ false };
    bool nativeOutput{ false };
    bool preview{ false };
};
// Container evidence only: never infer embedding from a generated QColorSpace.
SourceInfo readSource(const QString &path);
QString spaceName(const QColorSpace &space);
} // namespace QvColor
#endif
