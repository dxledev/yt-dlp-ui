#pragma once
#include <QJsonObject>
#include <QStringList>

namespace ytdlp {
QJsonObject defaultSettings();
QJsonObject validateSettings(QJsonObject settings);
QJsonObject validateDownload(QJsonObject options, const QJsonObject &defaults);
QJsonObject downloadOptions(const QJsonObject &job);
QStringList videoFormats();
QStringList downloadArguments(const QJsonObject &job);
QStringList metadataArguments(const QString &url, const QJsonObject &authentication = {});
bool isWebUrl(const QString &url);
QString executablePath(const QString &requested);
}
