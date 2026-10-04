#pragma once
#include <QJsonObject>
#include <QStringList>

namespace ytdlp {
QStringList cookieKeys();
QStringList browserNames();
QJsonObject cookieOptions(const QJsonObject &options);
QStringList cookieArguments(const QJsonObject &options);
QString authenticationHint(const QString &error);
}
