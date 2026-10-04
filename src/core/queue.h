#pragma once
#include "runner.h"
#include "storage.h"
#include <QJsonArray>
#include <QTimer>

namespace ytdlp {
class Queue : public QObject {
    Q_OBJECT
public:
    Queue(Paths paths, QObject *parent = nullptr);
    QJsonObject snapshot() const;
    QJsonObject add(const QJsonObject &options, bool dryRun = false);
    QJsonObject job(const QString &id) const;
    void cancel(const QString &id, bool dryRun = false);
    void retry(const QString &id, bool dryRun = false);
    void remove(const QString &id, bool dryRun = false);
    void setPaused(bool paused, bool dryRun = false);
    void clear(bool dryRun = false);
    QJsonObject configure(const QJsonObject &patch, bool dryRun = false);
    const QJsonObject &settings() const { return settings_; }
signals:
    void changed();
    void failed(const QString &error);
private:
    Paths paths_;
    QJsonObject settings_;
    QJsonArray jobs_;
    Runner runner_;
    QTimer saveTimer_;
    bool paused_ = false;
    QString activeId_;
    int indexOf(const QString &id) const;
    void startNext();
    void save();
    void announce(bool durable);
    void trimHistory();
};
}
