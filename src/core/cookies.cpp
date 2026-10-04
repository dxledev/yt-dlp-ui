#include "cookies.h"
#include "storage.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <stdexcept>

namespace ytdlp {
namespace {
void require(bool condition, const QString &message) {
    if (!condition) throw std::invalid_argument(message.toStdString());
}
QString textOption(const QJsonObject &options, const QString &key) {
    if (!options.contains(key)) return {};
    require(options[key].isString(), key + " must be a string");
    const auto text = options[key].toString().trimmed();
    require(text.size() <= 4096 && !text.contains(QRegularExpression("[\\x00-\\x1f\\x7f]")), "Invalid " + key);
    return text;
}
void validateBrowser(const QString &specification) {
    if (specification.isEmpty()) return;
    static const QRegularExpression pattern(
        R"(^([a-z]+)(?:\+([a-z0-9]+))?(?::(?!:)(.+?))?(?:::(.+))?$)");
    const auto match = pattern.match(specification);
    require(match.hasMatch() && browserNames().contains(match.captured(1)),
        "Choose a supported browser: " + browserNames().join(", "));
    const auto keyring = match.captured(2);
    require(keyring.isEmpty() || QStringList{"basictext", "gnomekeyring", "kwallet", "kwallet5", "kwallet6"}.contains(keyring), "Unsupported browser keyring");
    require(match.captured(4).isEmpty() || match.captured(1) == "firefox", "Containers are only supported for Firefox");
}
}
QStringList cookieKeys() { return {"cookiesFromBrowser", "cookiesFile"}; }
QStringList browserNames() { return {"brave", "chromium", "firefox", "chrome", "edge", "opera", "vivaldi", "whale", "safari"}; }
QJsonObject cookieOptions(const QJsonObject &options) {
    const auto browser = textOption(options, "cookiesFromBrowser");
    auto file = textOption(options, "cookiesFile");
    require(browser.isEmpty() || file.isEmpty(), "Choose browser cookies or a cookie file, not both");
    validateBrowser(browser);
    if (!file.isEmpty()) file = expandPath(file);
    return {{"cookiesFromBrowser", browser}, {"cookiesFile", file}};
}
QStringList cookieArguments(const QJsonObject &options) {
    const auto normalized = cookieOptions(options);
    const auto browser = normalized["cookiesFromBrowser"].toString();
    const auto file = normalized["cookiesFile"].toString();
    if (!browser.isEmpty()) return {"--cookies-from-browser", browser};
    if (!file.isEmpty()) {
        const QFileInfo info(file);
        require(info.isFile() && info.isReadable(), "Cannot read cookie file: " + file);
        return {"--cookies", file};
    }
    return {};
}
QString authenticationHint(const QString &error) {
    if (error.contains("sign in", Qt::CaseInsensitive) || error.contains("login required", Qt::CaseInsensitive))
        return "Sign-in required. Choose browser cookies in Settings, then retry.";
    return {};
}
}
