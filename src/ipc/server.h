#pragma once
#include "core/queue.h"
#include "core/theme_watcher.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QSet>

namespace ytdlp {
class Server : public QObject {
    Q_OBJECT
public:
    explicit Server(Paths paths, QObject *parent = nullptr);
    bool listen();
private:
    Paths paths_;
    QLocalServer server_;
    Queue queue_;
    ThemeWatcher theme_;
    QSet<QLocalSocket *> subscribers_;
    QMap<QLocalSocket *, QByteArray> buffers_;
    QTimer broadcastTimer_;
    QString themeError_;
    void accept();
    void read(QLocalSocket *socket);
    void dispatch(QLocalSocket *socket, const QJsonObject &request);
    void reply(QLocalSocket *socket, const QJsonValue &id, const QJsonValue &data);
    void write(QLocalSocket *socket, const QJsonObject &message);
    void publish(const QJsonObject &message);
    QJsonObject snapshot() const;
    void inspect(QLocalSocket *socket, QJsonValue id, const QJsonObject &options);
};
}
