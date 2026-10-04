#include "queue.h"
#include "download.h"
#include "cookies.h"
#include <QFileInfo>
#include <QUuid>
#include <stdexcept>

namespace ytdlp {
namespace {
bool terminal(const QString &status) { return QStringList{"done", "failed", "cancelled"}.contains(status); }
}
Queue::Queue(Paths paths, QObject *parent) : QObject(parent), paths_(std::move(paths)), runner_(this) {
    const auto settingsFile = paths_.config + "/settings.json";
    settings_ = validateSettings(QFileInfo::exists(settingsFile) ? readJson(settingsFile) : defaultSettings());
    if (!QFileInfo::exists(settingsFile)) writeJson(settingsFile, settings_);
    const auto queueFile = paths_.state + "/queue.json";
    if (QFileInfo::exists(queueFile)) {
        const auto data = readJson(queueFile);
        if (data["version"].toInt() != 1 || !data["jobs"].isArray()) throw std::runtime_error("Unsupported queue file");
        jobs_ = data["jobs"].toArray(); paused_ = data["paused"].toBool();
        for (int i = 0; i < jobs_.size(); ++i) {
            auto value = jobs_[i].toObject();
            if (value["status"] == "running") { value["status"] = "queued"; value["phase"] = "recovering"; jobs_[i] = value; }
        }
    }
    saveTimer_.setSingleShot(true); saveTimer_.setInterval(1000);
    connect(&saveTimer_, &QTimer::timeout, this, [this] {
        try { save(); } catch (const std::exception &error) { emit failed(error.what()); }
    });
    connect(&runner_, &Runner::updated, this, [this](const QJsonObject &fields) {
        const int index = indexOf(activeId_);
        auto value = jobs_[index].toObject();
        for (auto it = fields.begin(); it != fields.end(); ++it) value[it.key()] = it.value();
        jobs_[index] = value;
        if (!saveTimer_.isActive()) saveTimer_.start();
        emit changed();
    });
    connect(&runner_, &Runner::completed, this, [this](bool success, const QString &error) {
        const int index = indexOf(activeId_);
        auto value = jobs_[index].toObject();
        if (value["status"] != "cancelled") {
            value["status"] = success ? "done" : "failed";
            value["error"] = success ? QString() : (error.isEmpty() ? QString("yt-dlp exited unsuccessfully") : error);
        }
        value["finished"] = utcNow(); value["phase"] = value["status"];
        if (success) value["percent"] = 100;
        jobs_[index] = value; activeId_.clear(); trimHistory();
        try { announce(true); } catch (const std::exception &exception) { paused_ = true; emit failed(exception.what()); }
        QTimer::singleShot(0, this, &Queue::startNext);
    });
    QTimer::singleShot(0, this, &Queue::startNext);
}
int Queue::indexOf(const QString &id) const {
    for (int i = 0; i < jobs_.size(); ++i) if (jobs_[i].toObject()["id"] == id) return i;
    throw std::invalid_argument(("Unknown job: " + id).toStdString());
}
QJsonObject Queue::job(const QString &id) const { return jobs_[indexOf(id)].toObject(); }
QJsonObject Queue::snapshot() const {
    return {{"version", 1}, {"paused", paused_}, {"activeId", activeId_}, {"jobs", jobs_}, {"settings", settings_}};
}
void Queue::save() {
    writeJson(paths_.state + "/queue.json", {{"version", 1}, {"paused", paused_}, {"jobs", jobs_}}, QJsonDocument::Compact);
}
void Queue::announce(bool durable) { if (durable) { saveTimer_.stop(); save(); } emit changed(); }
QJsonObject Queue::add(const QJsonObject &options, bool dryRun) {
    auto value = validateDownload(options, settings_);
    if (dryRun) return {{"options", value}, {"arguments", QJsonArray::fromStringList(downloadArguments(value))}};
    if (jobs_.size() >= 2200 || QJsonDocument(snapshot()).toJson(QJsonDocument::Compact).size() > 4 * 1024 * 1024 - 40000) throw std::invalid_argument("Queue is full; clear finished history first");
    value["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    value["status"] = "queued"; value["added"] = utcNow(); value["title"] = "";
    value["percent"] = QJsonValue(); value["error"] = "";
    jobs_.append(value);
    announce(true); QTimer::singleShot(0, this, &Queue::startNext);
    return value;
}
void Queue::startNext() {
    if (paused_ || runner_.busy() || !activeId_.isEmpty()) return;
    for (int i = 0; i < jobs_.size(); ++i) {
        auto value = jobs_[i].toObject();
        if (value["status"] != "queued") continue;
        try {
            validateDownload(downloadOptions(value), settings_);
            value["status"] = "running"; value["started"] = utcNow(); value["phase"] = "preparing";
            activeId_ = value["id"].toString(); jobs_[i] = value; announce(true);
            runner_.start(value);
            return;
        } catch (const std::exception &error) {
            activeId_.clear(); value["status"] = "failed"; value["error"] = error.what(); value["finished"] = utcNow(); jobs_[i] = value;
            try { announce(true); } catch (...) { paused_ = true; emit failed("Cannot persist queue"); return; }
        }
    }
}
void Queue::cancel(const QString &id, bool dryRun) {
    const auto index = indexOf(id); auto value = jobs_[index].toObject();
    if (terminal(value["status"].toString())) throw std::invalid_argument("Job has already finished");
    if (dryRun) return;
    value["status"] = "cancelled"; value["finished"] = utcNow(); jobs_[index] = value; announce(true);
    if (activeId_ == id) runner_.cancel();
}
void Queue::retry(const QString &id, bool dryRun) {
    auto value = job(id);
    if (!terminal(value["status"].toString())) throw std::invalid_argument("Only finished jobs can be retried");
    auto options = downloadOptions(value);
    for (const auto &key : cookieKeys()) options.remove(key);
    add(options, dryRun);
}
void Queue::remove(const QString &id, bool dryRun) {
    const auto index = indexOf(id);
    if (id == activeId_) throw std::invalid_argument("Cancel the active download before removing it");
    if (dryRun) return;
    jobs_.removeAt(index); announce(true);
}
void Queue::setPaused(bool paused, bool dryRun) { if (!dryRun) { paused_ = paused; announce(true); if (!paused) startNext(); } }
void Queue::clear(bool dryRun) {
    if (dryRun) return;
    QJsonArray retained;
    for (const auto &value : jobs_) if (!terminal(value.toObject()["status"].toString()) || value.toObject()["id"] == activeId_) retained.append(value);
    jobs_ = retained; announce(true);
}
QJsonObject Queue::configure(const QJsonObject &patch, bool dryRun) {
    auto next = settings_;
    for (auto it = patch.begin(); it != patch.end(); ++it) next[it.key()] = it.value();
    next = validateSettings(next);
    if (!dryRun) { writeJson(paths_.config + "/settings.json", next); settings_ = next; trimHistory(); announce(true); }
    return next;
}
void Queue::trimHistory() {
    int finished = 0;
    for (const auto &value : jobs_) if (terminal(value.toObject()["status"].toString())) ++finished;
    int bytes = QJsonDocument(snapshot()).toJson(QJsonDocument::Compact).size();
    for (int i = 0; i < jobs_.size() && (finished > settings_["maxHistory"].toInt() || bytes > 4 * 1024 * 1024); ) {
        const auto value = jobs_[i].toObject();
        if (terminal(value["status"].toString()) && value["id"] != activeId_) {
            bytes -= QJsonDocument(value).toJson(QJsonDocument::Compact).size();
            jobs_.removeAt(i); --finished;
        } else ++i;
    }
}
}
