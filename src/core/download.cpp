#include "download.h"
#include "cookies.h"
#include "storage.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <stdexcept>

namespace ytdlp {
namespace {
void require(bool condition, const QString &error) {
    if (!condition) throw std::invalid_argument(error.toStdString());
}
void validateChoices(const QJsonObject &options) {
    require(QStringList{"video", "audio"}.contains(options["mode"].toString()), "Mode must be video or audio");
    require(QStringList{"keep", "mp3", "m4a", "opus", "flac", "wav"}.contains(options["audioFormat"].toString()), "Unsupported audio format");
    require(videoFormats().contains(options["videoFormat"].toString()), "Unsupported video format");
    const auto quality = options["quality"].toString();
    require(quality == "best" || QRegularExpression("^[1-9][0-9]{1,4}$").match(quality).hasMatch(), "Quality must be best or a height from 10 to 99999");
    require(!options["dest"].toString().trimmed().isEmpty(), "Destination is required");
}
QString outputTemplate(const QJsonObject &job) {
    const auto format = job["videoFormat"].toString("auto");
    const auto suffix = job["mode"] == "audio" ? QString(" (audio)") : format == "auto" ? QString() : " (" + format + ')';
    return "%(title)s [%(id)s]" + suffix + ".%(ext)s";
}
QStringList videoArguments(const QJsonObject &job) {
    const auto quality = job["quality"].toString();
    QStringList args{"--format", quality == "best" ? "bv*+ba/b" : "bv*[height<=" + quality + "]+ba/b[height<=" + quality + "]/b"};
    const auto format = job["videoFormat"].toString("auto");
    if (format != "auto") args << "--merge-output-format" << (format == "mkv" ? format : format + "/mkv") << "--recode-video" << format;
    return args;
}
}
QStringList videoFormats() { return {"auto", "mp4", "mkv", "webm"}; }
bool isWebUrl(const QString &text) {
    const QUrl url(text, QUrl::StrictMode);
    return text.size() <= 8192 && url.isValid() && !url.host().isEmpty()
        && (url.scheme() == "https" || url.scheme() == "http") && !text.contains('\n');
}
QString executablePath(const QString &requested) {
    const auto path = QFileInfo(requested).isAbsolute() ? requested : QStandardPaths::findExecutable(requested);
    require(!path.isEmpty() && QFileInfo(path).isFile() && QFileInfo(path).isExecutable(), "Cannot find executable: " + requested);
    return path;
}
QJsonObject defaultSettings() {
    return {{"mode", "video"}, {"quality", "best"}, {"audioFormat", "keep"}, {"videoFormat", "auto"},
        {"dest", QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)},
        {"binary", "yt-dlp"}, {"maxHistory", 200}, {"cookiesFromBrowser", ""}, {"cookiesFile", ""}, {"useNativePicker", true}};
}
QJsonObject validateSettings(QJsonObject settings) {
    const auto defaults = defaultSettings();
    for (auto it = settings.begin(); it != settings.end(); ++it)
        require(defaults.contains(it.key()), "Unknown setting: " + it.key());
    auto merged = defaults;
    for (auto it = settings.begin(); it != settings.end(); ++it) merged[it.key()] = it.value();
    validateChoices(merged);
    require(merged["binary"].isString() && !merged["binary"].toString().isEmpty(), "Binary must be a name or path");
    require(merged["useNativePicker"].isBool(), "useNativePicker must be true or false");
    const auto history = merged["maxHistory"].toDouble(-1);
    require(history >= 10 && history <= 2000 && history == int(history), "maxHistory must be an integer from 10 to 2000");
    merged["dest"] = expandPath(merged["dest"].toString());
    const auto authentication = cookieOptions(merged);
    for (const auto &key : cookieKeys()) merged[key] = authentication[key];
    return merged;
}
QJsonObject validateDownload(QJsonObject options, const QJsonObject &defaults) {
    for (auto it = options.begin(); it != options.end(); ++it)
        require((QStringList{"url", "mode", "quality", "audioFormat", "videoFormat", "dest", "binary"} + cookieKeys()).contains(it.key()), "Unknown download option: " + it.key());
    require(isWebUrl(options["url"].toString()), "Enter a valid http or https link");
    for (const auto &key : {"mode", "quality", "audioFormat", "dest", "binary"})
        if (!options.contains(key)) options[key] = defaults[key];
    if (!options.contains("videoFormat")) options["videoFormat"] = defaults.value("videoFormat").toString("auto");
    if (options.contains("cookiesFromBrowser") != options.contains("cookiesFile"))
        options[options.contains("cookiesFromBrowser") ? "cookiesFile" : "cookiesFromBrowser"] = "";
    for (const auto &key : cookieKeys())
        if (!options.contains(key)) options[key] = defaults.value(key).toString();
    validateChoices(options);
    options["dest"] = expandPath(options["dest"].toString());
    options["binary"] = executablePath(options["binary"].toString());
    const auto authentication = cookieOptions(options);
    for (const auto &key : cookieKeys()) options[key] = authentication[key];
    return options;
}
QJsonObject downloadOptions(const QJsonObject &job) {
    QJsonObject options;
    for (const auto &key : {"url", "mode", "quality", "audioFormat", "videoFormat", "dest", "binary"})
        if (job.contains(key)) options[key] = job[key];
    if (!options.contains("videoFormat")) options["videoFormat"] = "auto";
    for (const auto &key : cookieKeys()) options[key] = job.value(key).toString();
    return options;
}
QStringList downloadArguments(const QJsonObject &job) {
    QStringList args{"--ignore-config", "--no-playlist", "--newline", "--no-colors", "--progress", "--progress-delta", "0.3",
        "--no-simulate", "--paths", job["dest"].toString(), "--output", outputTemplate(job),
        "--print", "before_dl:YTDLP_META\t%(.{id,title,duration,webpage_url,uploader,thumbnail})j",
        "--print", "after_move:YTDLP_FILE\t%(filepath)j",
        "--progress-template", "download:YTDLP_PROGRESS\t%(progress)j",
        "--progress-template", "postprocess:YTDLP_POST\t%(progress.status)s"};
    if (job["mode"] == "audio") {
        args << "--format" << "ba/b" << "--extract-audio";
        if (job["audioFormat"] != "keep") args << "--audio-format" << job["audioFormat"].toString();
    } else args << videoArguments(job);
    args << cookieArguments(job) << "--" << job["url"].toString();
    return args;
}
QStringList metadataArguments(const QString &url, const QJsonObject &authentication) {
    require(isWebUrl(url), "Enter a valid http or https link");
    return QStringList{"--ignore-config", "--no-playlist", "--no-colors", "--skip-download", "--dump-single-json"}
        + cookieArguments(authentication) + QStringList{"--", url};
}
}
