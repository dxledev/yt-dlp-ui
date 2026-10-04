#include "cli.h"
#include "core/storage.h"
#include "ipc/client.h"
#include "ipc/server.h"
#include "ui/window.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLockFile>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <stdexcept>

namespace {
QString option(QStringList &arguments, const QString &key) {
    const int index = arguments.indexOf(key);
    if (index < 0) return {};
    if (index + 1 >= arguments.size()) throw std::invalid_argument(("Missing value for " + key).toStdString());
    const auto value = arguments.takeAt(index + 1); arguments.removeAt(index); return value;
}
void configure(QCoreApplication &app) {
    app.setApplicationName("yt-dlp-ui"); app.setApplicationVersion("1.0.0"); app.setOrganizationName("Yt-Dlp");
}
}
int main(int argc, char **argv) {
    try {
        QStringList arguments;
        for (int i = 1; i < argc; ++i) arguments.append(QString::fromLocal8Bit(argv[i]));
        if (arguments.contains("--help") || arguments.contains("-h")) { QTextStream(stdout) << ytdlp::helpText(); return 0; }
        if (arguments.contains("--version")) { QTextStream(stdout) << "Yt-Dlp 1.0.0\n"; return 0; }
        const auto config = option(arguments, "--config-dir"); const auto state = option(arguments, "--state-dir");
        const bool daemon = arguments.removeAll("--daemon") > 0;
        const bool ui = arguments.removeAll("--ui") > 0;
        const auto screenshot = option(arguments, "--screenshot"); const auto quitAfter = option(arguments, "--quit-after");
        if (daemon || (!arguments.isEmpty() && !ui)) {
            QCoreApplication app(argc, argv); configure(app); const auto paths = ytdlp::Paths::resolve(config, state);
            if (!daemon) return ytdlp::runCli(paths, arguments);
            if (!arguments.isEmpty()) throw std::invalid_argument("Unexpected daemon arguments");
            QDir().mkpath(paths.config); QDir().mkpath(paths.state);
            QFile::setPermissions(paths.state, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
            QLockFile lock(paths.state + "/service.lock"); if (!lock.tryLock(0)) return 0;
            ytdlp::Server server(paths); if (!server.listen()) return 0;
            return app.exec();
        }
        if (!arguments.isEmpty()) throw std::invalid_argument("Unexpected window arguments");
        QApplication app(argc, argv); configure(app); app.setApplicationDisplayName("Yt-Dlp"); app.setDesktopFileName("io.github.yt_dlp_ui"); app.setStyle("Fusion");
        const auto paths = ytdlp::Paths::resolve(config, state); QDir().mkpath(paths.state);
        QLockFile lock(paths.state + "/window.lock");
        if (!lock.tryLock(0)) { ytdlp::request(paths, {{"command", "show"}}); return 0; }
        ytdlp::Window window(paths);
        if (QFileInfo::exists(paths.config + "/window.json")) {
            const auto geometry = ytdlp::readJson(paths.config + "/window.json");
            window.resize(std::clamp(geometry["width"].toInt(940), 660, 2400), std::clamp(geometry["height"].toInt(810), 680, 1600));
        }
        window.show();
        if (!screenshot.isEmpty()) QTimer::singleShot(1200, &window, [&window, screenshot] { window.grab().save(screenshot); });
        if (!quitAfter.isEmpty()) { bool valid = false; int milliseconds = quitAfter.toInt(&valid); if (!valid || milliseconds < 1) throw std::invalid_argument("--quit-after requires positive milliseconds"); QTimer::singleShot(milliseconds, &app, &QCoreApplication::quit); }
        return app.exec();
    } catch (const std::exception &error) { QTextStream(stderr) << "yt-dlp-ui: " << error.what() << '\n'; return 1; }
}
