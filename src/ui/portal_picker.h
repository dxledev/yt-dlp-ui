#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <functional>

namespace ytdlp {
// Asks the desktop's configured FileChooser portal for a folder. Calls `unavailable` if the portal cannot be reached.
void chooseFolderWithPortal(QObject *context, const QString &title, std::function<void(const QString &)> chosen, std::function<void()> unavailable);

class PortalResponse : public QObject {
    Q_OBJECT
public:
    std::function<void(const QString &)> chosen;
public slots:
    void response(uint code, const QVariantMap &results);
};
}
