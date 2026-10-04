#include "server.h"
#include "core/download.h"
#include "core/cookies.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <stdexcept>

namespace ytdlp {
Server::Server(Paths paths, QObject *parent) : QObject(parent), paths_(std::move(paths)),
    queue_(paths_, this),
    theme_(paths_.config + "/theme.json", ensureTheme(paths_.config + "/theme.json"), this) {
    broadcastTimer_.setInterval(120); broadcastTimer_.setSingleShot(true);
    connect(&server_, &QLocalServer::newConnection, this, &Server::accept);
    connect(&queue_, &Queue::changed, this, [this] { if (!broadcastTimer_.isActive()) broadcastTimer_.start(); });
    connect(&broadcastTimer_, &QTimer::timeout, this, [this] { publish({{"event", "snapshot"}, {"data", snapshot()}}); });
    connect(&queue_, &Queue::failed, this, [this](const QString &error) { publish({{"event", "error"}, {"error", error}}); });
    connect(&theme_, &ThemeWatcher::changed, this, [this] { publish({{"event", "snapshot"}, {"data", snapshot()}}); });
    connect(&theme_, &ThemeWatcher::rejected, this, [this](const QString &error) {
        themeError_ = error; publish({{"event", "error"}, {"error", "Theme: " + error}});
    });
    connect(&theme_, &ThemeWatcher::recovered, this, [this] { themeError_.clear(); });
}
bool Server::listen() {
    QLocalServer::removeServer(paths_.socket);
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server_.listen(paths_.socket)) throw std::runtime_error(server_.errorString().toStdString());
    return true;
}
QJsonObject Server::snapshot() const {
    auto value = queue_.snapshot(); value["theme"] = theme_.theme().toJson(); value["themeError"] = themeError_;
    value["pid"] = QCoreApplication::applicationPid(); return value;
}
void Server::accept() {
    while (auto *socket = server_.nextPendingConnection()) {
        buffers_.insert(socket, {});
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { read(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            subscribers_.remove(socket); buffers_.remove(socket); socket->deleteLater();
        });
    }
}
void Server::write(QLocalSocket *socket, const QJsonObject &message) {
    if (socket->state() != QLocalSocket::ConnectedState) return;
    if (socket->bytesToWrite() > 8 * 1024 * 1024) { socket->disconnectFromServer(); return; }
    socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}
void Server::publish(const QJsonObject &message) { for (auto *socket : subscribers_) write(socket, message); }
void Server::reply(QLocalSocket *socket, const QJsonValue &id, const QJsonValue &data) { write(socket, {{"id", id}, {"ok", true}, {"data", data}}); }
void Server::read(QLocalSocket *socket) {
    auto &buffer = buffers_[socket]; buffer += socket->readAll();
    if (buffer.size() > 1024 * 1024) { socket->disconnectFromServer(); return; }
    int newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(buffer.left(newline), &error); buffer.remove(0, newline + 1);
        if (error.error != QJsonParseError::NoError || !document.isObject()) { write(socket, {{"ok", false}, {"error", "Expected a JSON object"}}); continue; }
        dispatch(socket, document.object());
    }
}
void Server::dispatch(QLocalSocket *socket, const QJsonObject &request) {
    const auto id = request["id"]; const auto command = request["command"].toString();
    const auto options = request["options"].toObject(); const bool dryRun = request["dryRun"].toBool();
    try {
        QJsonValue data;
        if (command == "status") data = snapshot();
        else if (command == "list") data = queue_.snapshot()["jobs"];
        else if (command == "job") data = queue_.job(options["id"].toString());
        else if (command == "add") data = queue_.add(options, dryRun);
        else if (command == "cancel") queue_.cancel(options["id"].toString(), dryRun);
        else if (command == "retry") queue_.retry(options["id"].toString(), dryRun);
        else if (command == "remove") queue_.remove(options["id"].toString(), dryRun);
        else if (command == "pause" || command == "resume") queue_.setPaused(command == "pause", dryRun);
        else if (command == "clear") {
            if (!dryRun && !options["confirmed"].toBool()) throw std::invalid_argument("Clearing history requires --yes");
            queue_.clear(dryRun);
        } else if (command == "settings") data = options.isEmpty() ? queue_.settings() : queue_.configure(options, dryRun);
        else if (command == "subscribe") { subscribers_.insert(socket); write(socket, {{"event", "snapshot"}, {"data", snapshot()}}); }
        else if (command == "show") { publish({{"event", "show"}}); }
        else if (command == "info") { inspect(socket, id, options); return; }
        else if (command == "shutdown") {
            if (!queue_.snapshot()["activeId"].toString().isEmpty()) throw std::invalid_argument("Pause and cancel the active download before shutdown");
            if (!dryRun) QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        } else throw std::invalid_argument(("Unknown command: " + command).toStdString());
        reply(socket, id, data);
    } catch (const std::exception &error) { write(socket, {{"id", id}, {"ok", false}, {"error", error.what()}}); }
}
void Server::inspect(QLocalSocket *socket, QJsonValue id, const QJsonObject &options) {
    for (auto it = options.begin(); it != options.end(); ++it) {
        if (it.key() != "url" && !cookieKeys().contains(it.key())) throw std::invalid_argument("Unknown media-info option");
    }
    const auto authentication = cookieOptions(options.contains("cookiesFromBrowser") || options.contains("cookiesFile") ? options : queue_.settings());
    const auto arguments = metadataArguments(options["url"].toString(), authentication);
    const auto binary = executablePath(queue_.settings()["binary"].toString());
    if (findChildren<QProcess *>(QString(), Qt::FindDirectChildrenOnly).size() >= 3) throw std::invalid_argument("Too many metadata requests; try again shortly");
    auto *process = new QProcess(this);
    auto *timeout = new QTimer(process); timeout->setSingleShot(true); timeout->start(60000);
    auto output = std::make_shared<QByteArray>(); auto errors = std::make_shared<QByteArray>();
    QPointer<QLocalSocket> target(socket);
    connect(process, &QProcess::readyReadStandardOutput, this, [process, output] {
        *output += process->readAllStandardOutput(); if (output->size() > 8 * 1024 * 1024) process->kill();
    });
    connect(process, &QProcess::readyReadStandardError, this, [process, errors] { *errors += process->readAllStandardError(); *errors = errors->right(12000); });
    connect(timeout, &QTimer::timeout, process, [process, errors] { *errors = "Metadata request timed out"; process->kill(); });
    connect(process, &QProcess::errorOccurred, this, [this, process, target, id](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            if (target) write(target, {{"id", id}, {"ok", false}, {"error", process->errorString()}});
            process->deleteLater();
        }
    });
    connect(process, &QProcess::finished, this, [this, process, timeout, target, id, output, errors](int code, QProcess::ExitStatus status) {
        timeout->stop(); *output += process->readAllStandardOutput(); *errors += process->readAllStandardError();
        const auto document = QJsonDocument::fromJson(*output);
        if (target) {
            if (code == 0 && status == QProcess::NormalExit && document.isObject()) reply(target, id, document.object());
            else write(target, {{"id", id}, {"ok", false}, {"error", QString::fromUtf8(errors->right(6000)).trimmed()}});
        }
        process->deleteLater();
    });
    connect(socket, &QLocalSocket::disconnected, process, [process] { if (process->state() != QProcess::NotRunning) process->kill(); });
    process->start(binary, arguments);
}
}
