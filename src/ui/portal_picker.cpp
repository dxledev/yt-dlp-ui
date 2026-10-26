#include "portal_picker.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QUrl>
#include <QUuid>

namespace ytdlp {
void PortalResponse::response(uint code, const QVariantMap &results) {
    const auto uris = results["uris"].toStringList();
    if (code == 0 && !uris.isEmpty()) chosen(QUrl(uris.first()).toLocalFile());
    deleteLater();
}
void chooseFolderWithPortal(QObject *context, const QString &title, std::function<void(const QString &)> chosen, std::function<void()> unavailable) {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) { unavailable(); return; }
    const auto token = "ytdlp" + QUuid::createUuid().toString(QUuid::Id128);
    auto sender = bus.baseService().mid(1); sender.replace('.', '_');
    const auto request = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
    auto *receiver = new PortalResponse; receiver->setParent(context); receiver->chosen = std::move(chosen);
    bus.connect("org.freedesktop.portal.Desktop", request, "org.freedesktop.portal.Request", "Response", receiver, SLOT(response(uint,QVariantMap)));
    auto call = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.FileChooser", "OpenFile");
    call << QString() << title << QVariantMap{{"handle_token", token}, {"directory", true}};
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), receiver);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, receiver, [receiver, unavailable = std::move(unavailable)](QDBusPendingCallWatcher *done) {
        if (done->isError()) { unavailable(); receiver->deleteLater(); }
    });
}
}
