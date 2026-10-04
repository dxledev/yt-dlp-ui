#pragma once
#include <QJsonObject>
#include <QObject>
#include <QProcess>

namespace ytdlp {
class Runner : public QObject {
    Q_OBJECT
public:
    explicit Runner(QObject *parent = nullptr);
    void start(const QJsonObject &job);
    void cancel();
    bool busy() const { return process_.state() != QProcess::NotRunning || finishing_; }
signals:
    void updated(const QJsonObject &fields);
    void completed(bool success, const QString &error);
private:
    QProcess process_;
    QByteArray pending_;
    QString log_;
    bool finishing_ = false;
    bool cancelled_ = false;
    void readOutput();
    void parseLine(const QByteArray &line);
    void finish(bool success, const QString &error);
};
}
