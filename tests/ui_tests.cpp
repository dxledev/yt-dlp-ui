#include "ui/window.h"
#include "ui/cookie_settings.h"
#include <QApplication>
#include <QComboBox>
#include <QJsonArray>
#include "core/download.h"
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

using namespace ytdlp;
class UiTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("YT_DLP_UI_REDUCED_MOTION", "1"); }
    void animatedModalSwapKeepsBackdrop() {
        qputenv("YT_DLP_UI_REDUCED_MOTION", "0");
        QTemporaryDir dir; Window window(Paths::resolve(dir.path() + "/config", dir.path() + "/state"), nullptr, false); window.show();
        window.findChild<QPushButton *>("browseButton")->click(); QVERIFY(window.modal()->isVisible()); QTest::qWait(250);
        window.openFolderPicker(); QVERIFY(window.modal()->isVisible());
        window.modal()->dismiss(); QTRY_VERIFY(!window.modal()->isVisible());
        QVERIFY(window.findChild<QWidget *>("composerCard")->isEnabled());
        qputenv("YT_DLP_UI_REDUCED_MOTION", "1");
    }
    void embeddedModalAndResize() {
        QTemporaryDir dir; auto paths = Paths::resolve(dir.path() + "/config", dir.path() + "/state");
        Window window(paths, nullptr, false); window.show();
        auto *browse = window.findChild<QPushButton *>("browseButton"); QVERIFY(browse); browse->click();
        QVERIFY(window.modal()->isVisible()); for (auto *widget : QApplication::topLevelWidgets()) if (widget->isVisible()) QCOMPARE(widget, &window);
        window.resize(660, 680); QTest::qWait(20);
        QVERIFY(window.rect().contains(window.modal()->geometry()));
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape); QVERIFY(!window.modal()->isVisible());
        auto *audio = window.findChild<QPushButton *>("audioMode"); audio->click();
        auto *format = window.findChild<QComboBox *>("videoFormatInput"); QVERIFY(format); QVERIFY(!format->isVisible());
        auto *quality = window.findChild<QComboBox *>("qualityInput"); QCOMPARE(quality->count(), 6); QCOMPARE(quality->itemData(1).toString(), QString("mp3"));
        window.findChild<QPushButton *>("videoMode")->click(); QCOMPARE(quality->itemData(3).toString(), QString("1080"));
        QVERIFY(format->isVisible()); QCOMPARE(format->count(), 4); format->setCurrentIndex(format->findData("webm"));
        audio->click(); window.findChild<QPushButton *>("videoMode")->click(); QCOMPARE(format->currentData().toString(), QString("webm"));
        window.resize(660, 680); QTest::qWait(20); QCOMPARE(window.width(), 660);
        QVERIFY(!quality->geometry().intersects(format->geometry())); QVERIFY(format->parentWidget()->rect().contains(format->geometry()));
        window.resize(940, 810);
        const QJsonObject data{{"settings", defaultSettings()}, {"theme", presetTheme("slate").toJson()}, {"jobs", QJsonArray{}}, {"paused", false}};
        QVERIFY(QMetaObject::invokeMethod(&window, "updateSnapshot", Qt::DirectConnection, Q_ARG(QJsonObject, data)));
        window.openSettings(); QVERIFY(window.modal()->isVisible()); window.grab().save(qEnvironmentVariable("YT_DLP_UI_TEST_SETTINGS", "/tmp/yt-dlp-ui-settings.png"));
        window.modal()->dismiss();
        window.grab().save(qEnvironmentVariable("YT_DLP_UI_TEST_SCREENSHOT", "/tmp/yt-dlp-ui-test.png"));
    }
    void populatedAndThemedModal() {
        QTemporaryDir dir; Window window(Paths::resolve(dir.path()+"/config", dir.path()+"/state"), nullptr, false);
        window.show();
        QJsonObject job{{"id", "fixture"}, {"url", "https://example.com/video"}, {"title", "An afternoon worth keeping"},
            {"mode", "video"}, {"quality", "1080"}, {"dest", "/tmp/Downloads"}, {"status", "running"},
            {"percent", 47.2}, {"speed", 2500000}, {"eta", 22}, {"phase", "downloading"}};
        QJsonObject data{{"settings", defaultSettings()}, {"theme", presetTheme("slate").toJson()}, {"jobs", QJsonArray{job}}, {"paused", false}};
        QVERIFY(QMetaObject::invokeMethod(&window, "updateSnapshot", Qt::DirectConnection, Q_ARG(QJsonObject, data)));
        auto *list = window.findChild<QListWidget *>("downloadList"); QCOMPARE(list->count(), 1); list->setCurrentRow(0);
        QVERIFY(window.findChild<QPushButton *>("cancelButton")->isEnabled());
        window.openDetails(); QVERIFY(window.modal()->isVisible());
        data["theme"] = presetTheme("paper").toJson();
        QVERIFY(QMetaObject::invokeMethod(&window, "updateSnapshot", Qt::DirectConnection, Q_ARG(QJsonObject, data)));
        QVERIFY(window.styleSheet().contains("#f4f1e9"));
        window.grab().save("/tmp/yt-dlp-ui-modal.png"); window.modal()->dismiss();
        data["theme"] = presetTheme("slate").toJson();
        QVERIFY(QMetaObject::invokeMethod(&window, "updateSnapshot", Qt::DirectConnection, Q_ARG(QJsonObject, data)));
        window.grab().save("/tmp/yt-dlp-ui-queue.png");
    }
    void cookieSettingsAndInlineErrors() {
        QTemporaryDir directory;
        Window window(Paths::resolve(directory.path()+"/config", directory.path()+"/state"), nullptr, false); window.show();
        const QJsonObject data{{"settings", defaultSettings()}, {"theme", presetTheme("slate").toJson()}, {"jobs", QJsonArray{}}, {"paused", false}};
        QVERIFY(QMetaObject::invokeMethod(&window, "updateSnapshot", Qt::DirectConnection, Q_ARG(QJsonObject, data)));
        window.openSettings();
        auto *cookies = dynamic_cast<CookieSettings *>(window.findChild<QWidget *>("cookieSettings")); QVERIFY(cookies);
        auto *source = window.findChild<QComboBox *>("cookieSource"); QVERIFY(source);
        QCOMPARE(source->currentData().toString(), QString());
        source->setCurrentIndex(source->findData("brave")); QCOMPARE(cookies->options()["cookiesFromBrowser"].toString(), QString("brave"));
        auto *browser = window.findChild<QLineEdit *>("cookieBrowserInput"); browser->setText("brave:Profile 1");
        QCOMPARE(cookies->options()["cookiesFromBrowser"].toString(), QString("brave:Profile 1"));
        source->setCurrentIndex(source->findData("file"));
        QVERIFY_EXCEPTION_THROWN(cookies->options(), std::invalid_argument);
        window.modal()->showError("Enter a cookie file path");
        auto *error = window.findChild<QLabel *>("modalError"); QVERIFY(error && error->isVisible());
        source->setCurrentIndex(source->findData("")); QVERIFY(cookies->options()["cookiesFromBrowser"].toString().isEmpty());
        source->setCurrentIndex(source->findData("brave"));
        window.modal()->showError(""); error->hide();
        window.grab().save("/tmp/yt-dlp-ui-cookie-settings.png");
        for (auto *widget : QApplication::topLevelWidgets()) if (widget->isVisible()) QCOMPARE(widget, &window);
    }
    void entryTrashAndSelection() {
        QTemporaryDir directory; Window window(Paths::resolve(directory.path()+"/config", directory.path()+"/state"), nullptr, false); window.show();
        QJsonObject first{{"id", "first"}, {"title", "A finished download"}, {"url", "https://example.com/one"}, {"status", "done"}, {"mode", "video"}, {"quality", "1080"}, {"videoFormat", "mp4"}, {"percent", 100}, {"dest", "/tmp/Downloads"}};
        auto second = first; second["id"] = "second"; second["title"] = "A failed download"; second["status"] = "failed"; second["error"] = "Fixture failure"; second["percent"] = 0;
        auto defaults = defaultSettings(); defaults["videoFormat"] = "mkv";
        QJsonObject data{{"settings", defaults}, {"jobs", QJsonArray{first, second}}, {"theme", presetTheme("slate").toJson()}};
        QVERIFY(QMetaObject::invokeMethod(&window, "updateSnapshot", Qt::DirectConnection, Q_ARG(QJsonObject, data)));
        QCOMPARE(window.findChild<QComboBox *>("videoFormatInput")->currentData().toString(), QString("mkv"));
        auto *list = window.findChild<QListWidget *>("downloadList"); auto *delegate = qobject_cast<JobDelegate *>(list->itemDelegate()); QVERIFY(delegate);
        QSignalSpy removals(delegate, &JobDelegate::removeRequested); QTest::qWait(20);
        const auto icon = JobDelegate::removeButtonRect(list->visualItemRect(list->item(1))).center();
        QTest::mouseClick(list->viewport(), Qt::RightButton, Qt::NoModifier, icon); QCOMPARE(removals.count(), 0);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, icon); QCOMPARE(removals.count(), 1); QCOMPARE(removals.takeFirst().first().toString(), QString("second"));
        QTest::mouseDClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, icon); QVERIFY(!window.modal()->isVisible()); QCOMPARE(removals.count(), 0);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(0)).center()); QCOMPARE(removals.count(), 0);
        list->setCurrentRow(1); data["jobs"] = QJsonArray{second};
        QVERIFY(QMetaObject::invokeMethod(&window, "updateSnapshot", Qt::DirectConnection, Q_ARG(QJsonObject, data)));
        QCOMPARE(list->currentRow(), 0); QCOMPARE(list->currentItem()->data(Qt::UserRole).toJsonObject()["id"].toString(), QString("second"));
        data["jobs"] = QJsonArray{first};
        QVERIFY(QMetaObject::invokeMethod(&window, "updateSnapshot", Qt::DirectConnection, Q_ARG(QJsonObject, data)));
        QCOMPARE(list->currentRow(), -1); QVERIFY(!window.findChild<QPushButton *>("detailsButton")->isEnabled());
        data["activeId"] = "first";
        QVERIFY(QMetaObject::invokeMethod(&window, "updateSnapshot", Qt::DirectConnection, Q_ARG(QJsonObject, data)));
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, JobDelegate::removeButtonRect(list->visualItemRect(list->item(0))).center()); QCOMPARE(removals.count(), 0);
        data["activeId"] = ""; data["jobs"] = QJsonArray{first, second};
        QVERIFY(QMetaObject::invokeMethod(&window, "updateSnapshot", Qt::DirectConnection, Q_ARG(QJsonObject, data)));
        window.resize(660, 680); QTest::qWait(20); window.grab().save("/tmp/yt-dlp-ui-trash-format.png");
        data["jobs"] = QJsonArray{};
        QVERIFY(QMetaObject::invokeMethod(&window, "updateSnapshot", Qt::DirectConnection, Q_ARG(QJsonObject, data)));
        QCOMPARE(list->count(), 0); QVERIFY(!list->isVisible());
    }
};
QTEST_MAIN(UiTests)
#include "ui_tests.moc"
