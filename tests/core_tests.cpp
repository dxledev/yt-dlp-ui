#include "core/download.h"
#include "core/cookies.h"
#include "core/storage.h"
#include "core/theme_source.h"
#include "core/theme_watcher.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cstdio>

using namespace ytdlp;
class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void themeValidation() {
        auto theme = presetTheme("slate");
        QCOMPARE(parseTheme(theme.toJson()), theme);
        QVERIFY_EXCEPTION_THROWN(setColors(theme, {"accent=garbage"}), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(setColors(theme, {"unknown=#123456"}), std::invalid_argument);
        QCOMPARE(setColors(theme, {"accent=#112233"}).hex("accent"), QString("#112233"));
    }
    void downloadValidation() {
        auto defaults = defaultSettings(); defaults["binary"] = "/usr/bin/true";
        auto job = validateDownload({{"url", "https://example.com/video?x=1&y=2"}, {"mode", "audio"}, {"audioFormat", "opus"}}, defaults);
        const auto arguments = downloadArguments(job);
        QVERIFY(arguments.contains("--no-playlist")); QVERIFY(arguments.contains("--ignore-config")); QVERIFY(arguments.contains("--extract-audio"));
        QCOMPARE(arguments.last(), job["url"].toString()); QCOMPARE(arguments[arguments.size() - 2], QString("--"));
        QVERIFY_EXCEPTION_THROWN(validateDownload({{"url", "file:///etc/passwd"}}, defaults), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(validateDownload({{"url", "https://example.com"}, {"quality", "720;touch /tmp/x"}}, defaults), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(validateSettings({{"maxHistory", 10.5}}), std::invalid_argument);
    }
    void videoFormatArgumentsAndLegacyDefaults() {
        auto defaults = defaultSettings(); defaults["binary"] = "/usr/bin/true";
        defaults.remove("videoFormat"); QCOMPARE(validateSettings(defaults)["videoFormat"].toString(), QString("auto"));
        const QJsonObject link{{"url", "https://example.com/video"}};
        const auto automatic = validateDownload(link, defaults);
        QCOMPARE(automatic["videoFormat"].toString(), QString("auto"));
        QVERIFY(!downloadArguments(automatic).contains("--recode-video"));
        for (const auto &format : {"mp4", "mkv", "webm"}) {
            auto options = link; options["videoFormat"] = format;
            const auto job = validateDownload(options, defaults); const auto args = downloadArguments(job);
            QCOMPARE(args[args.indexOf("--recode-video") + 1], QString(format));
            QVERIFY(args[args.indexOf("--merge-output-format") + 1].contains("mkv"));
            QVERIFY(args[args.indexOf("--output") + 1].contains("(" + QString(format) + ")"));
            QCOMPARE(downloadOptions(job)["videoFormat"].toString(), QString(format));
            options["mode"] = "audio"; QVERIFY(!downloadArguments(validateDownload(options, defaults)).contains("--recode-video"));
        }
        QVERIFY_EXCEPTION_THROWN(validateDownload({{"url", "https://example.com/video"}, {"videoFormat", "mp4;bad"}}, defaults), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(validateSettings({{"videoFormat", 12}}), std::invalid_argument);
        defaults["videoFormat"] = "webm";
        QCOMPARE(validateDownload(downloadOptions(automatic), defaults)["videoFormat"].toString(), QString("auto"));
        QCOMPARE(downloadOptions(link)["videoFormat"].toString(), QString("auto"));
    }
    void cookieArgumentsAndValidation() {
        auto defaults = defaultSettings(); defaults["binary"] = "/usr/bin/true";
        defaults["cookiesFromBrowser"] = "brave:Profile 1";
        const auto job = validateDownload({{"url", "https://example.com/video"}}, defaults);
        QCOMPARE(job["cookiesFromBrowser"].toString(), QString("brave:Profile 1"));
        const auto args = downloadArguments(job);
        QCOMPARE(args[args.indexOf("--cookies-from-browser") + 1], QString("brave:Profile 1"));
        QVERIFY(args.indexOf("--cookies-from-browser") < args.indexOf("--"));
        QVERIFY(metadataArguments(job["url"].toString(), defaults).contains("brave:Profile 1"));
        const auto anonymous = validateDownload({{"url", "https://example.com/video"}, {"cookiesFromBrowser", ""}}, defaults);
        QVERIFY(!downloadArguments(anonymous).contains("--cookies-from-browser"));
        QVERIFY_EXCEPTION_THROWN(cookieOptions({{"cookiesFromBrowser", "--exec=bad"}}), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(cookieOptions({{"cookiesFromBrowser", "brave\n--exec=bad"}}), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(cookieOptions({{"cookiesFromBrowser", 3}}), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(cookieOptions({{"cookiesFromBrowser", "brave"}, {"cookiesFile", "/tmp/file"}}), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(cookieOptions({{"cookiesFromBrowser", "brave+invalid"}}), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(cookieOptions({{"cookiesFromBrowser", "brave::Work"}}), std::invalid_argument);
        QCOMPARE(cookieOptions({{"cookiesFromBrowser", "firefox:Profile 1::Work"}})["cookiesFromBrowser"].toString(), QString("firefox:Profile 1::Work"));
        QCOMPARE(cookieOptions({{"cookiesFromBrowser", "brave+basictext:Default"}})["cookiesFromBrowser"].toString(), QString("brave+basictext:Default"));
        QVERIFY(!authenticationHint("ERROR: Please sign in. Use --cookies-from-browser").isEmpty());
        QVERIFY(authenticationHint("ERROR: video unavailable").isEmpty());
    }
    void cookieFileAndLegacyJobs() {
        QTemporaryDir directory; const auto path = directory.path() + "/cookies with spaces.txt";
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("# Netscape HTTP Cookie File\n"); file.close();
        auto defaults = defaultSettings(); defaults["binary"] = "/usr/bin/true"; defaults["cookiesFromBrowser"] = "brave";
        const auto job = validateDownload({{"url", "https://example.com/video"}, {"cookiesFile", path}}, defaults);
        QVERIFY(job["cookiesFromBrowser"].toString().isEmpty());
        const auto args = downloadArguments(job); QCOMPARE(args[args.indexOf("--cookies") + 1], path);
        QVERIFY(!args.contains("--cookies-from-browser"));
        QVERIFY(metadataArguments(job["url"].toString(), job).contains(path));
        QVERIFY_EXCEPTION_THROWN(cookieArguments({{"cookiesFile", directory.path()+"/missing.txt"}}), std::invalid_argument);
        auto legacy = defaults; legacy.remove("cookiesFromBrowser"); legacy.remove("cookiesFile");
        QVERIFY(validateSettings(legacy).contains("cookiesFromBrowser"));
        QVERIFY(downloadOptions({{"url", "https://example.com/video"}})["cookiesFromBrowser"].toString().isEmpty());
    }
    void paletteFormats() {
        QTemporaryDir dir;
        writeJson(dir.path() + "/colors.json", {{"colors", QJsonObject{{"mSurface", "#111111"}, {"mOnSurface", "#eeeeee"}, {"mPrimary", "#ffaa77"}}}});
        auto theme = followTheme("file", dir.path() + "/colors.json");
        QCOMPARE(theme.hex("background"), QString("#111111")); QCOMPARE(theme.hex("accent"), QString("#ffaa77"));
        QFile file(dir.path() + "/Colors.qml"); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("readonly property color background: \"#121212\"\nreadonly property color foreground: \"#ededed\"\nreadonly property color primary: Qt.rgba(128 / 255, 64 / 255, 32 / 255, 1)\n"); file.close();
        theme = followTheme("file", dir.path() + "/Colors.qml"); QCOMPARE(theme.hex("accent"), QString("#804020"));
    }
    void sourceTransitionMatrix() {
        QTemporaryDir directory; const auto root = directory.path();
        const QMap<QString, QString> modes{{"static", "forest"}, {"caelestia", "slate"}, {"noctalia", "rose"}, {"island", "paper"}};
        for (auto it = modes.begin(); it != modes.end(); ++it) { QDir().mkpath(root+"/"+it.key()); writeJson(root+"/"+it.key()+"/palette.json", presetTheme(it.value()).toJson()); }
        QVERIFY(QFile::link(root+"/static", root+"/current"));
        QVERIFY(QFile::link(root+"/current", root+"/active"));
        const auto initial = followTheme("file", root+"/active/palette.json"); writeJson(root+"/theme.json", initial.toJson());
        ThemeWatcher watcher(root+"/theme.json", initial);
        const QStringList transitions{"caelestia", "noctalia", "caelestia", "current", "noctalia", "current", "island", "current", "island", "noctalia", "island"};
        for (const auto &mode : transitions) {
            const auto temporary = root+"/next";
            QVERIFY(QFile::link(root+"/"+mode, temporary));
            QVERIFY(std::rename(temporary.toLocal8Bit().constData(), (root+"/active").toLocal8Bit().constData()) == 0);
            const auto expected = presetTheme(modes.value(mode == "current" ? "static" : mode)).hex("accent");
            QTRY_COMPARE(watcher.theme().hex("accent"), expected);
            QCOMPARE(loadTheme(root+"/theme.json").hex("accent"), expected);
        }
        writeJson(root+"/island/palette.json", presetTheme("forest").toJson());
        QVERIFY(QFile::remove(root+"/active")); QVERIFY(QFile::link(root+"/island", root+"/active"));
        QTRY_COMPARE(watcher.theme().hex("accent"), presetTheme("forest").hex("accent"));
        writeJson(root+"/island/palette.json", presetTheme("rose").toJson());
        QTRY_COMPARE(watcher.theme().hex("accent"), presetTheme("rose").hex("accent"));
    }
    void liveAtomicAndSymlink() {
        QTemporaryDir directory; const auto root = directory.path(); QDir().mkpath(root + "/one"); QDir().mkpath(root + "/two");
        writeJson(root + "/one/palette.json", presetTheme("slate").toJson()); writeJson(root + "/two/palette.json", presetTheme("paper").toJson());
        QVERIFY(QFile::link(root + "/one", root + "/active"));
        auto initial = followTheme("file", root + "/active/palette.json"); writeJson(root + "/theme.json", initial.toJson());
        ThemeWatcher watcher(root + "/theme.json", initial); QSignalSpy spy(&watcher, &ThemeWatcher::changed);
        QVERIFY(QFile::remove(root + "/active")); QVERIFY(QFile::link(root + "/two", root + "/active"));
        QTRY_COMPARE(watcher.theme().hex("accent"), presetTheme("paper").hex("accent"));
        QCOMPARE(loadTheme(root + "/theme.json").hex("accent"), presetTheme("paper").hex("accent"));
        writeJson(root + "/two/palette.json", presetTheme("rose").toJson());
        QTRY_COMPARE(watcher.theme().hex("accent"), presetTheme("rose").hex("accent"));
        writeJson(root + "/theme.json", presetTheme("forest").toJson());
        QTRY_COMPARE(watcher.theme().hex("accent"), presetTheme("forest").hex("accent"));
        QVERIFY(spy.count() >= 3);
        QFile corrupt(root + "/theme.json"); QVERIFY(corrupt.open(QIODevice::WriteOnly)); corrupt.write("{bad"); corrupt.close();
        QSignalSpy rejected(&watcher, &ThemeWatcher::rejected); QTRY_VERIFY(rejected.count() > 0);
        QCOMPARE(watcher.theme().hex("accent"), presetTheme("forest").hex("accent"));
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
