#include "theme_watcher.h"
#include "core/storage.h"
#include "core/theme_source.h"

#include <QFileInfo>
#include <QDir>
#include <QLockFile>

namespace ytdlp {

ThemeWatcher::ThemeWatcher(QString path, Theme initial, QObject *parent)
    : QObject(parent), path_(std::move(path)), theme_(std::move(initial)) {
    QDir().mkpath(QFileInfo(path_).absolutePath() + "/.locks");
    updateWatches();
    debounce_.setSingleShot(true);
    debounce_.setInterval(35);
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this] { debounce_.start(); });
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this] { debounce_.start(); });
    connect(&debounce_, &QTimer::timeout, this, &ThemeWatcher::reload);
    QTimer::singleShot(0, this, &ThemeWatcher::reload);
}

void ThemeWatcher::updateWatches() {
    QStringList wanted{QFileInfo(path_).absolutePath()};
    if (QFileInfo::exists(path_))
        wanted.append(path_);
    wanted.append(sourceWatchPaths(theme_));
    wanted.removeDuplicates();
    const auto current = watcher_.files() + watcher_.directories();
    for (const auto &path : current) {
        if (!wanted.contains(path))
            watcher_.removePath(path);
    }
    for (const auto &path : wanted) {
        if (!current.contains(path))
            watcher_.addPath(path);
    }
}

void ThemeWatcher::reload() {
    QLockFile lock(QFileInfo(path_).absolutePath() + "/.locks/theme.lock");
    if (!lock.tryLock(0)) { debounce_.start(); return; }
    try {
        auto next = loadTheme(path_);
        if (!next.sourceKind.isEmpty()) {
            if (QFileInfo(next.sourcePath).canonicalFilePath() == QFileInfo(path_).canonicalFilePath())
                throw std::invalid_argument("Theme source cannot be its own output file");
            auto resolved = refreshSource(next);
            if (resolved != next)
                writeJson(path_, resolved.toJson());
            next = std::move(resolved);
        }
        if (next != theme_) {
            theme_ = std::move(next);
            emit changed();
        }
        if (rejected_) { rejected_ = false; emit recovered(); }
    } catch (const std::exception &error) {
        rejected_ = true;
        emit rejected(QString::fromUtf8(error.what()));
    }
    // Reattach watches after atomic replacement, including symlink target changes.
    updateWatches();
}

}
