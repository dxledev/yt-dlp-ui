#include "client.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <stdexcept>

namespace ytdlp {
bool ensureDaemon(const Paths &paths, QString *error) {
    QLocalSocket probe; probe.connectToServer(paths.socket);
    if (probe.waitForConnected(100)) return true;
    QProcess worker;
    worker.setProgram(QCoreApplication::applicationFilePath());
    worker.setArguments({"--config-dir", paths.config, "--state-dir", paths.state, "--daemon"});
    worker.setStandardInputFile(QProcess::nullDevice());
    worker.setStandardOutputFile(QProcess::nullDevice());
    worker.setStandardErrorFile(QProcess::nullDevice());
    if (!worker.startDetached()) { if (error) *error = "Cannot start download service"; return false; }
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 5000) {
        probe.abort(); probe.connectToServer(paths.socket);
        if (probe.waitForConnected(100)) return true;
        QThread::msleep(30);
    }
    if (error) *error = "Download service did not start; run yt-dlp-ui --daemon to see the error";
    return false;
}
QJsonObject request(const Paths &paths, const QJsonObject &message, int timeout) {
    QString error;
    if (!ensureDaemon(paths, &error)) throw std::runtime_error(error.toStdString());
    QLocalSocket socket; socket.connectToServer(paths.socket);
    if (!socket.waitForConnected(2000)) throw std::runtime_error(socket.errorString().toStdString());
    auto payload = message; payload["id"] = 1;
    socket.write(QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n'); socket.flush();
    QByteArray buffer;
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < timeout) {
        if (!socket.bytesAvailable()) socket.waitForReadyRead(std::max(1, timeout - int(timer.elapsed())));
        buffer += socket.readAll();
        if (buffer.size() > 8 * 1024 * 1024) throw std::runtime_error("IPC response too large");
        int newline;
        while ((newline = buffer.indexOf('\n')) >= 0) {
            auto response = QJsonDocument::fromJson(buffer.left(newline)).object(); buffer.remove(0, newline + 1);
            if (response["id"].toInt() != 1) continue;
            if (!response["ok"].toBool()) throw std::runtime_error(response["error"].toString().toStdString());
            return response;
        }
        if (socket.state() != QLocalSocket::ConnectedState) throw std::runtime_error("Download service disconnected");
    }
    throw std::runtime_error("IPC request timed out");
}
Client::Client(Paths paths, QObject *parent) : QObject(parent), paths_(std::move(paths)) {
    connect(&socket_, &QLocalSocket::connected, this, [this] { send("subscribe"); });
    connect(&socket_, &QLocalSocket::readyRead, this, &Client::read);
    connect(&socket_, &QLocalSocket::disconnected, this, [this] {
        callbacks_.clear(); pending_.clear();
        emit error("Download service disconnected; reconnecting…");
        if (!stopping_) QTimer::singleShot(1000, this, &Client::start);
    });
}
void Client::start() {
    QString message;
    if (!ensureDaemon(paths_, &message)) { emit error(message); QTimer::singleShot(3000, this, &Client::start); return; }
    socket_.connectToServer(paths_.socket);
}
void Client::send(const QString &command, const QJsonObject &options, std::function<void(QJsonValue)> callback) {
    if (socket_.state() != QLocalSocket::ConnectedState) { emit error("Download service is not connected"); return; }
    const int id = ++sequence_;
    if (callback) callbacks_.insert(id, std::move(callback));
    socket_.write(QJsonDocument(QJsonObject{{"id", id}, {"command", command}, {"options", options}}).toJson(QJsonDocument::Compact) + '\n');
}
void Client::read() {
    pending_ += socket_.readAll();
    if (pending_.size() > 8 * 1024 * 1024) { socket_.abort(); return; }
    int newline;
    while ((newline = pending_.indexOf('\n')) >= 0) {
        const auto value = QJsonDocument::fromJson(pending_.left(newline)).object(); pending_.remove(0, newline + 1);
        if (value["event"] == "snapshot") emit snapshot(value["data"].toObject());
        else if (value.contains("event")) emit event(value);
        else if (value.contains("id")) {
            auto callback = callbacks_.take(value["id"].toInt());
            if (!value["ok"].toBool()) emit error(value["error"].toString());
            else if (callback) callback(value["data"]);
        }
    }
}
}
