#include "core/download.h"
#include "core/queue.h"
#include "core/runner.h"
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace ytdlp;
class WorkerTests : public QObject {
    Q_OBJECT
    QJsonObject settings(const QString &root) {
        auto value = defaultSettings(); value["binary"] = QString(SOURCE_DIRECTORY) + "/tests/fake-yt-dlp"; value["dest"] = root + "/downloads"; return value;
    }
private slots:
    void realYtDlpVideoAndAudio() {
        const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        const auto ytdlp = QStandardPaths::findExecutable("yt-dlp");
        if (ffmpeg.isEmpty() || ytdlp.isEmpty()) QSKIP("yt-dlp and ffmpeg are required for the real media check");
        QTemporaryDir directory; const auto root = directory.path();
        QProcess generator;
        generator.start(ffmpeg, {"-nostdin", "-v", "error", "-f", "lavfi", "-i", "color=c=blue:s=128x72:r=24", "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100", "-t", "1", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", root+"/source.mp4"});
        QVERIFY(generator.waitForFinished(10000)); QCOMPARE(generator.exitCode(), 0);
        const auto url = QUrl::fromLocalFile(root+"/source.mp4").toString();
        const QJsonObject format{{"format_id", "mp4"}, {"ext", "mp4"}, {"url", url}, {"vcodec", "h264"}, {"acodec", "aac"}, {"height", 72}, {"width", 128}};
        writeJson(root+"/info.json", {{"id", "native-test"}, {"title", "Native client test"}, {"ext", "mp4"}, {"url", url}, {"webpage_url", "https://example.com/test"}, {"extractor", "generic"}, {"extractor_key", "Generic"}, {"formats", QJsonArray{format}}});
        const auto quote = [](QString value) { return "'" + value.replace("'", "'\\''") + "'"; };
        QFile wrapper(root+"/yt-dlp-wrapper"); QVERIFY(wrapper.open(QIODevice::WriteOnly));
        wrapper.write(("#!/usr/bin/env bash\nset -euo pipefail\nexec " + quote(ytdlp) + " --enable-file-urls --load-info-json " + quote(root+"/info.json") + " \"$@\"\n").toUtf8()); wrapper.close();
        QVERIFY(wrapper.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        auto paths = Paths::resolve(root+"/config", root+"/state"); auto defaults = defaultSettings(); defaults["binary"] = wrapper.fileName(); defaults["dest"] = root+"/downloads";
        QFile cookies(root+"/cookies.txt"); QVERIFY(cookies.open(QIODevice::WriteOnly)); cookies.write("# Netscape HTTP Cookie File\n"); cookies.close();
        defaults["cookiesFile"] = cookies.fileName();
        writeJson(paths.config+"/settings.json", defaults); Queue queue(paths);
        const auto video = queue.add({{"url", "https://example.com/test"}})["id"].toString();
        const auto audio = queue.add({{"url", "https://example.com/test"}, {"mode", "audio"}, {"audioFormat", "flac"}})["id"].toString();
        QTRY_COMPARE_WITH_TIMEOUT(queue.job(video)["status"].toString(), QString("done"), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(queue.job(audio)["status"].toString(), QString("done"), 15000);
        QCOMPARE(queue.job(video)["title"].toString(), QString("Native client test"));
        QVERIFY(QFileInfo(queue.job(video)["file"].toString()).size() > 1000);
        QVERIFY(queue.job(audio)["file"].toString().endsWith(".flac"));
        QVERIFY(QFileInfo(queue.job(audio)["file"].toString()).size() > 1000);
        generator.start(ffmpeg, {"-nostdin", "-v", "error", "-i", root+"/source.mp4", "-map", "0:v", "-c", "copy", root+"/video.mp4", "-map", "0:a", "-c", "copy", root+"/audio.m4a"});
        QVERIFY(generator.waitForFinished(10000)); QCOMPARE(generator.exitCode(), 0);
        auto videoStream = format; videoStream["url"] = QUrl::fromLocalFile(root+"/video.mp4").toString(); videoStream["acodec"] = "none";
        const QJsonObject audioStream{{"format_id", "m4a"}, {"ext", "m4a"}, {"url", QUrl::fromLocalFile(root+"/audio.m4a").toString()}, {"vcodec", "none"}, {"acodec", "aac"}};
        auto info = readJson(root+"/info.json"); info["formats"] = QJsonArray{videoStream, audioStream}; writeJson(root+"/info.json", info);
        for (const auto &container : {"mp4", "mkv", "webm"}) {
            const auto id = queue.add({{"url", "https://example.com/test"}, {"videoFormat", container}})["id"].toString();
            QTRY_COMPARE_WITH_TIMEOUT(queue.job(id)["status"].toString(), QString("done"), 15000);
            const auto file = queue.job(id)["file"].toString(); QVERIFY(file.endsWith('.' + QString(container))); QVERIFY(QFileInfo(file).size() > 1000);
            queue.remove(id); QVERIFY(QFileInfo::exists(file));
        }
        QVERIFY(QFileInfo::exists(queue.job(video)["file"].toString()));
        QVERIFY(QFileInfo::exists(queue.job(audio)["file"].toString()));
    }
    void retryUsesCurrentCookieSettings() {
        QTemporaryDir directory; auto paths = Paths::resolve(directory.path()+"/config", directory.path()+"/state");
        writeJson(paths.config+"/settings.json", settings(directory.path())); Queue queue(paths);
        const auto failed = queue.add({{"url", "https://example.com/auth-required"}, {"quality", "720"}, {"videoFormat", "mkv"}})["id"].toString();
        QTRY_COMPARE(queue.job(failed)["status"].toString(), QString("failed"));
        QVERIFY(queue.job(failed)["error"].toString().contains("Please sign in"));
        queue.configure({{"cookiesFromBrowser", "brave"}, {"cookiesFile", ""}});
        queue.retry(failed);
        const auto retried = queue.snapshot()["jobs"].toArray().last().toObject()["id"].toString();
        QCOMPARE(queue.job(retried)["cookiesFromBrowser"].toString(), QString("brave"));
        QCOMPARE(queue.job(retried)["quality"].toString(), QString("720"));
        QCOMPARE(queue.job(retried)["videoFormat"].toString(), QString("mkv"));
        QTRY_COMPARE(queue.job(retried)["status"].toString(), QString("done"));
        QVERIFY(queue.job(failed)["cookiesFromBrowser"].toString().isEmpty());
        QCOMPARE(readJson(paths.config+"/settings.json")["cookiesFromBrowser"].toString(), QString("brave"));
    }
    void sequentialPersistenceAndRetry() {
        QTemporaryDir directory; const auto root = directory.path(); auto paths = Paths::resolve(root + "/config", root + "/state");
        writeJson(paths.config + "/settings.json", settings(root));
        QString firstId, secondId;
        {
            Queue queue(paths); queue.setPaused(true);
            auto first = queue.add({{"url", "https://example.com/one"}});
            auto second = queue.add({{"url", "https://example.com/two"}, {"mode", "audio"}, {"audioFormat", "opus"}});
            firstId = first["id"].toString(); secondId = second["id"].toString();
            QCOMPARE(queue.snapshot()["jobs"].toArray().size(), 2);
            queue.add({{"url", "https://example.com/dry"}}, true); QCOMPARE(queue.snapshot()["jobs"].toArray().size(), 2);
        }
        Queue queue(paths); QCOMPARE(queue.snapshot()["paused"].toBool(), true);
        queue.setPaused(false);
        QTRY_COMPARE(queue.job(firstId)["status"].toString(), QString("running"));
        QCOMPARE(queue.job(secondId)["status"].toString(), QString("queued"));
        QTRY_COMPARE(queue.job(firstId)["status"].toString(), QString("done"));
        QTRY_COMPARE(queue.job(secondId)["status"].toString(), QString("done"));
        QCOMPARE(queue.job(firstId)["title"].toString(), QString("Fixture media"));
        QCOMPARE(queue.job(firstId)["file"].toString(), QString("/tmp/fixture.mp4"));
        QCOMPARE(queue.job(firstId)["percent"].toInt(), 100);
        QVERIFY(queue.job(firstId)["finished"].toString() <= queue.job(secondId)["started"].toString());
        queue.retry(firstId); QCOMPARE(queue.snapshot()["jobs"].toArray().size(), 3);
        const auto retried = queue.snapshot()["jobs"].toArray().last().toObject()["id"].toString();
        QTRY_COMPARE(queue.job(retried)["status"].toString(), QString("done"));
        queue.clear(true); QCOMPARE(queue.snapshot()["jobs"].toArray().size(), 3);
        queue.clear(); QCOMPARE(queue.snapshot()["jobs"].toArray().size(), 0);
    }
    void removeQueuedAndFinishedEntries() {
        QTemporaryDir directory; auto paths = Paths::resolve(directory.path()+"/config", directory.path()+"/state");
        writeJson(paths.config+"/settings.json", settings(directory.path()));
        QString retained;
        {
            Queue queue(paths); queue.setPaused(true);
            const auto removed = queue.add({{"url", "https://example.com/remove"}})["id"].toString();
            retained = queue.add({{"url", "https://example.com/retain"}, {"videoFormat", "mp4"}})["id"].toString();
            queue.remove(removed, true); QCOMPARE(queue.snapshot()["jobs"].toArray().size(), 2);
            queue.remove(removed); QCOMPARE(queue.snapshot()["jobs"].toArray().size(), 1);
            QCOMPARE(queue.job(retained)["status"].toString(), QString("queued"));
            QVERIFY_EXCEPTION_THROWN(queue.job(removed), std::invalid_argument);
            QVERIFY_EXCEPTION_THROWN(queue.remove("missing", true), std::invalid_argument);
        }
        Queue recovered(paths); QCOMPARE(recovered.snapshot()["jobs"].toArray().size(), 1);
        QCOMPARE(recovered.job(retained)["videoFormat"].toString(), QString("mp4"));
        recovered.setPaused(false); QTRY_COMPARE(recovered.job(retained)["status"].toString(), QString("done"));
        recovered.remove(retained); QVERIFY(recovered.snapshot()["jobs"].toArray().isEmpty());
        QVERIFY(readJson(paths.state+"/queue.json")["jobs"].toArray().isEmpty());
    }
    void failureAndProcessGroupCancellation() {
        QTemporaryDir directory; auto paths = Paths::resolve(directory.path()+"/config", directory.path()+"/state");
        writeJson(paths.config+"/settings.json", settings(directory.path()));
        Queue queue(paths);
        const auto failure = queue.add({{"url", "https://example.com/fail"}})["id"].toString();
        QTRY_COMPARE(queue.job(failure)["status"].toString(), QString("failed"));
        QVERIFY(queue.job(failure)["error"].toString().contains("fixture failure"));
        const auto slow = queue.add({{"url", "https://example.com/slow"}})["id"].toString();
        QTRY_VERIFY(queue.job(slow)["metadata"].toObject().contains("fixturePid"));
        const auto pid = queue.job(slow)["metadata"].toObject()["fixturePid"].toInt();
        QFile children(QString("/proc/%1/task/%1/children").arg(pid)); QVERIFY(children.open(QIODevice::ReadOnly));
        const auto child = children.readAll().trimmed().split(' ').first(); QVERIFY(!child.isEmpty());
        QVERIFY_EXCEPTION_THROWN(queue.remove(slow), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(queue.remove(slow, true), std::invalid_argument);
        queue.cancel(slow, true); QCOMPARE(queue.job(slow)["status"].toString(), QString("running"));
        queue.cancel(slow); QCOMPARE(queue.job(slow)["status"].toString(), QString("cancelled"));
        QVERIFY_EXCEPTION_THROWN(queue.remove(slow), std::invalid_argument);
        QTRY_VERIFY(queue.snapshot()["activeId"].toString().isEmpty());
        queue.remove(slow); QCOMPARE(queue.snapshot()["jobs"].toArray().size(), 1);
        QFile childStat("/proc/" + child + "/stat");
        if (childStat.open(QIODevice::ReadOnly)) QVERIFY(childStat.readAll().split(')').last().trimmed().startsWith('Z'));
    }
    void recoveryAndSettings() {
        QTemporaryDir directory; auto paths = Paths::resolve(directory.path()+"/config", directory.path()+"/state");
        writeJson(paths.config+"/settings.json", settings(directory.path()));
        auto job = validateDownload({{"url", "https://example.com/one"}}, settings(directory.path()));
        job["id"] = "interrupted"; job["status"] = "running";
        writeJson(paths.state+"/queue.json", {{"version", 1}, {"paused", true}, {"jobs", QJsonArray{job}}});
        Queue queue(paths); QCOMPARE(queue.job("interrupted")["status"].toString(), QString("queued"));
        queue.configure({{"maxHistory", 10}}, true); QCOMPARE(queue.settings()["maxHistory"].toInt(), 200);
        queue.configure({{"maxHistory", 10}}); QCOMPARE(queue.settings()["maxHistory"].toInt(), 10);
        queue.setPaused(false); QTRY_COMPARE(queue.job("interrupted")["status"].toString(), QString("done"));
    }
};
QTEST_GUILESS_MAIN(WorkerTests)
#include "worker_tests.moc"
