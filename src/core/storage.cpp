#include "storage.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <stdexcept>

namespace ytdlp {
QString expandPath(QString path) {
    if (path == "~") path = QDir::homePath();
    else if (path.startsWith("~/")) path = QDir::homePath() + path.mid(1);
    return QFileInfo(path).absoluteFilePath();
}
Paths Paths::resolve(const QString &config, const QString &state) {
    Paths paths;
    paths.config = expandPath(config.isEmpty() ? qEnvironmentVariable("YT_DLP_UI_CONFIG_DIR", QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/yt-dlp-ui") : config);
    paths.state = expandPath(state.isEmpty() ? qEnvironmentVariable("YT_DLP_UI_STATE_DIR", QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + "/yt-dlp-ui") : state);
    const auto key = QCryptographicHash::hash((paths.config + '\n' + paths.state).toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    const auto runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    paths.socket = (runtime.isEmpty() ? paths.state : runtime) + "/yt-dlp-ui-" + key + ".sock";
    return paths;
}
QJsonObject readJson(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024)
        throw std::runtime_error(("Cannot read JSON: " + path).toStdString());
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        throw std::invalid_argument(("Invalid JSON: " + path + ": " + error.errorString()).toStdString());
    return document.object();
}
void writeJson(const QString &path, const QJsonObject &data, QJsonDocument::JsonFormat format) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        throw std::runtime_error(("Cannot create directory: " + path).toStdString());
    QSaveFile file(path);
    const auto bytes = QJsonDocument(data).toJson(format);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error(("Cannot save JSON: " + path + ": " + file.errorString()).toStdString());
}
QString utcNow() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
}
