#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace ytdlp {
struct Paths {
    QString config;
    QString state;
    QString socket;
    static Paths resolve(const QString &config = {}, const QString &state = {});
};
QJsonObject readJson(const QString &path);
void writeJson(const QString &path, const QJsonObject &data, QJsonDocument::JsonFormat format = QJsonDocument::Indented);
QString expandPath(QString path);
QString utcNow();
}
