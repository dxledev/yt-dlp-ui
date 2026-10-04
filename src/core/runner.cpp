#include "runner.h"
#include "download.h"
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>
#include <csignal>
#include <algorithm>

namespace ytdlp {
Runner::Runner(QObject *parent) : QObject(parent) {
    process_.setProcessChannelMode(QProcess::MergedChannels);
    QProcess::UnixProcessParameters parameters;
    parameters.flags = QProcess::UnixProcessFlag::CreateNewSession;
    process_.setUnixProcessParameters(parameters);
    connect(&process_, &QProcess::readyReadStandardOutput, this, &Runner::readOutput);
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        readOutput();
        if (!pending_.isEmpty()) { parseLine(pending_); pending_.clear(); }
        finish(code == 0 && status == QProcess::NormalExit && !cancelled_, log_.trimmed().right(6000));
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(false, process_.errorString());
    });
}
void Runner::start(const QJsonObject &job) {
    pending_.clear(); log_.clear(); cancelled_ = false; finishing_ = true;
    if (!QDir().mkpath(job["dest"].toString())) { finish(false, "Cannot create destination directory"); return; }
    process_.start(job["binary"].toString(), downloadArguments(job));
}
void Runner::cancel() {
    cancelled_ = true;
    const auto pid = process_.processId();
    if (pid <= 0) return;
    ::kill(-pid, SIGTERM);
    QTimer::singleShot(2000, this, [this, pid] {
        if (process_.state() != QProcess::NotRunning && process_.processId() == pid) ::kill(-pid, SIGKILL);
    });
}
void Runner::finish(bool success, const QString &error) {
    if (!finishing_) return;
    finishing_ = false;
    emit completed(success, error);
}
void Runner::readOutput() {
    pending_ += process_.readAllStandardOutput();
    int newline;
    while ((newline = pending_.indexOf('\n')) >= 0) {
        const auto line = pending_.left(newline).trimmed();
        pending_.remove(0, newline + 1);
        parseLine(line);
    }
    if (pending_.size() > 1024 * 1024) { pending_.clear(); log_ += "Output line too large\n"; }
}
void Runner::parseLine(const QByteArray &line) {
    const auto tab = line.indexOf('\t');
    const auto tag = line.left(tab);
    const auto payload = line.mid(tab + 1);
    if (tag == "YTDLP_META") {
        const auto metadata = QJsonDocument::fromJson(payload).object();
        if (!metadata.isEmpty()) emit updated({{"title", metadata["title"]}, {"metadata", metadata}});
    } else if (tag == "YTDLP_FILE") {
        const auto document = QJsonDocument::fromJson("[" + payload + "]");
        if (document.isArray()) emit updated({{"file", document.array().at(0)}});
    } else if (tag == "YTDLP_PROGRESS") {
        const auto progress = QJsonDocument::fromJson(payload).object();
        const auto bytes = progress["downloaded_bytes"].toDouble();
        const auto total = progress["total_bytes"].toDouble(progress["total_bytes_estimate"].toDouble());
        emit updated({{"percent", total > 0 ? QJsonValue(std::clamp(bytes * 100 / total, 0.0, 100.0)) : QJsonValue()},
            {"bytes", bytes}, {"totalBytes", total}, {"speed", progress["speed"]}, {"eta", progress["eta"]}, {"phase", "downloading"}});
    } else if (tag == "YTDLP_POST") {
        emit updated({{"phase", "processing"}});
    } else if (!line.isEmpty()) {
        log_ += QString::fromUtf8(line) + '\n';
        if (log_.size() > 32000) log_ = log_.right(24000);
        emit updated({{"log", log_}});
    }
}
}
