#pragma once

#include "core/theme.h"
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

namespace ytdlp {

class ThemeWatcher : public QObject {
    Q_OBJECT
public:
    ThemeWatcher(QString path, Theme initial, QObject *parent = nullptr);
    const Theme &theme() const { return theme_; }
    const QString &path() const { return path_; }

signals:
    void changed();
    void rejected(const QString &message);
    void recovered();

private:
    QString path_;
    Theme theme_;
    QFileSystemWatcher watcher_;
    QTimer debounce_;
    bool rejected_ = false;
    void reload();
    void updateWatches();
};

}
