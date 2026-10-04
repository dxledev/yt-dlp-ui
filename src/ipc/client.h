#pragma once
#include "core/storage.h"
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <functional>

namespace ytdlp {
bool ensureDaemon(const Paths &paths, QString *error = nullptr);
QJsonObject request(const Paths &paths, const QJsonObject &message, int timeout = 15000);
class Client : public QObject {
    Q_OBJECT
public:
    explicit Client(Paths paths, QObject *parent = nullptr);
    void start();
    void send(const QString &command, const QJsonObject &options = {}, std::function<void(QJsonValue)> callback = {});
signals:
    void snapshot(const QJsonObject &data);
    void event(const QJsonObject &data);
    void error(const QString &message);
private:
    Paths paths_;
    QLocalSocket socket_;
    QByteArray pending_;
    int sequence_ = 0;
    bool stopping_ = false;
    QMap<int, std::function<void(QJsonValue)>> callbacks_;
    void read();
};
}
