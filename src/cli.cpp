#include "cli.h"
#include "core/theme_source.h"
#include "ipc/client.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <memory>
#include <QJsonArray>
#include <QLockFile>
#include <QTextStream>
#include <stdexcept>

namespace ytdlp {
namespace {
void output(const QJsonValue &value) {
    QByteArray bytes;
    if (value.isObject()) bytes = QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    else if (value.isArray()) bytes = QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    else bytes = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    QTextStream(stdout) << bytes << '\n';
}
QString takeValue(QStringList &arguments, const QString &flag) {
    if (arguments.isEmpty()) throw std::invalid_argument(("Missing value for " + flag).toStdString());
    return arguments.takeFirst();
}
int themeCommand(const Paths &paths, QStringList arguments, bool dryRun) {
    const QString path = paths.config + "/theme.json";
    const auto action = arguments.isEmpty() ? QString("show") : arguments.takeFirst();
    std::unique_ptr<QLockFile> lock;
    if (!dryRun && !QStringList{"show", "keys", "presets"}.contains(action)) {
        const auto directory = paths.config + "/.locks";
        if (!QDir().mkpath(directory)) throw std::runtime_error("Cannot create theme lock directory");
        lock = std::make_unique<QLockFile>(directory + "/theme.lock");
        if (!lock->tryLock(2000)) throw std::runtime_error("Theme is busy; try again");
    }
    auto theme = (action == "preset" || action == "import" || action == "follow" || action == "keys" || action == "presets" || !QFileInfo::exists(path)) ? presetTheme("slate") : loadTheme(path);
    if (action == "show") { if (!arguments.isEmpty()) throw std::invalid_argument("Unexpected theme arguments"); output(theme.toJson()); return 0; }
    if (action == "keys") { output(QJsonArray::fromStringList(colorKeys())); return 0; }
    if (action == "presets") { output(QJsonArray::fromStringList(presetNames())); return 0; }
    if (action == "set") theme = setColors(theme, arguments);
    else if (action == "preset") { theme = presetTheme(takeValue(arguments, action)); if (!arguments.isEmpty()) throw std::invalid_argument("Unexpected preset arguments"); }
    else if (action == "import") { theme = importPalette(expandPath(takeValue(arguments, action))); theme.sourceKind.clear(); theme.sourcePath.clear(); if (!arguments.isEmpty()) throw std::invalid_argument("Unexpected import arguments"); }
    else if (action == "follow") {
        auto kind = takeValue(arguments, action); auto source = takeValue(arguments, action);
        theme = followTheme(kind, source);
        if (!arguments.isEmpty()) throw std::invalid_argument("Unexpected follow arguments");
        if (QFileInfo(theme.sourcePath).absoluteFilePath() == path || (!QFileInfo(path).canonicalFilePath().isEmpty() && QFileInfo(theme.sourcePath).canonicalFilePath() == QFileInfo(path).canonicalFilePath())) throw std::invalid_argument("Theme source cannot be its own output file");
    } else if (action == "unfollow") { theme.sourceKind.clear(); theme.sourcePath.clear(); if (!arguments.isEmpty()) throw std::invalid_argument("Unexpected unfollow arguments"); }
    else if (action == "export") {
        const auto destination = expandPath(takeValue(arguments, action));
        if (!arguments.isEmpty()) throw std::invalid_argument("Unexpected export arguments");
        if (!dryRun) writeJson(destination, theme.toJson());
        output(theme.toJson()); return 0;
    } else throw std::invalid_argument("Unknown theme command");
    if (!dryRun) writeJson(path, theme.toJson());
    output(theme.toJson()); return 0;
}
}
QString helpText() {
    return R"(Yt-Dlp — a portable native client for yt-dlp
Usage: yt-dlp-ui [--config-dir DIR] [--state-dir DIR] [COMMAND]

No command opens the window. Commands print JSON.
  add URL [--mode video|audio] [--quality best|HEIGHT]
          [--video-format auto|mp4|mkv|webm]
          [--audio-format keep|mp3|m4a|opus|flac|wav] [--dest DIR] [--binary FILE]
          [--cookies-from-browser BROWSER[:PROFILE] | --cookies FILE | --no-cookies]
  list                        Download queue and history
  status                      Queue, settings, theme, service PID
  job ID                      Job details, progress, metadata and log
  info URL [COOKIE OPTIONS]   Inspect media without downloading
  watch                       Stream JSON events until interrupted
  cancel ID                   Cancel one queued or running job
  retry ID                    Add a finished job again
  remove ID                   Remove an idle entry; keep downloaded files
  pause | resume              Stop/start dispatching queued jobs
  clear --yes                 Clear finished history; keep downloaded files
  settings [KEY=VALUE ...]    Read or update defaults
                             Cookie keys: cookiesFromBrowser, cookiesFile
  show                        Bring an open window to the front
  shutdown                    Stop the idle service
  theme show | keys | presets
  theme preset NAME
  theme set COLOR=#RRGGBB [...]
  theme import FILE           Import JSON or QML palette once
  theme follow file PATH      Follow a palette file or directory
  theme unfollow              Keep current colors and stop following
  theme export FILE           Export persistent theme JSON

--dry-run previews mutations. --daemon runs the headless service.
--help and --version do not start a service.
)";
}
int runCli(const Paths &paths, QStringList arguments) {
    const bool dryRun = arguments.removeAll("--dry-run") > 0;
    const auto command = arguments.isEmpty() ? QString("status") : arguments.takeFirst();
    if (command == "theme") return themeCommand(paths, arguments, dryRun);
    if (command == "watch") {
        if (!arguments.isEmpty() || dryRun) throw std::invalid_argument("Usage: yt-dlp-ui watch");
        auto *client = new Client(paths, qApp);
        QObject::connect(client, &Client::snapshot, client, [](const QJsonObject &data) { output(QJsonObject{{"event", "snapshot"}, {"data", data}}); });
        QObject::connect(client, &Client::event, client, [](const QJsonObject &data) { output(data); });
        QObject::connect(client, &Client::error, client, [](const QString &error) { QTextStream(stderr) << error << '\n'; });
        client->start(); return qApp->exec();
    }
    QJsonObject options;
    if (command == "add" || command == "info") {
        options["url"] = takeValue(arguments, command);
        QMap<QString, QString> keys{{"--cookies-from-browser", "cookiesFromBrowser"}, {"--cookies", "cookiesFile"}};
        if (command == "add") {
            keys.insert("--mode", "mode"); keys.insert("--quality", "quality"); keys.insert("--audio-format", "audioFormat");
            keys.insert("--video-format", "videoFormat");
            keys.insert("--dest", "dest"); keys.insert("--binary", "binary");
        }
        while (!arguments.isEmpty()) {
            const auto flag = arguments.takeFirst();
            if (flag == "--no-cookies") { options["cookiesFromBrowser"] = ""; options["cookiesFile"] = ""; continue; }
            if (!keys.contains(flag)) throw std::invalid_argument(("Unknown option: " + flag).toStdString());
            options[keys[flag]] = takeValue(arguments, flag);
        }
    } else if (command == "job" || command == "cancel" || command == "retry" || command == "remove") options["id"] = takeValue(arguments, command);
    else if (command == "clear") options["confirmed"] = arguments.removeAll("--yes") > 0;
    else if (command == "settings") {
        for (const auto &assignment : arguments) {
            const auto separator = assignment.indexOf('=');
            if (separator < 1) throw std::invalid_argument("Expected KEY=VALUE");
            const auto key = assignment.left(separator); const auto value = assignment.mid(separator + 1);
            if (key == "maxHistory") { bool valid; const int number = value.toInt(&valid); if (!valid) throw std::invalid_argument("maxHistory must be an integer"); options[key] = number; }
            else options[key] = value;
        }
        arguments.clear();
    }
    if (!arguments.isEmpty()) throw std::invalid_argument("Unexpected command arguments");
    const auto response = request(paths, {{"command", command}, {"options", options}, {"dryRun", dryRun}}, command == "info" ? 65000 : 15000);
    output(response["data"].isNull() || response["data"].isUndefined() ? QJsonValue(QJsonObject{{"ok", true}, {"dryRun", dryRun}}) : response["data"]);
    return 0;
}
}
