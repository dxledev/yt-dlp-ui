#include "theme_source.h"
#include "storage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <cmath>
#include <stdexcept>

namespace ytdlp {
namespace {

using Palette = QMap<QString, QColor>;

[[noreturn]] void fail(const QString &message) {
    throw std::invalid_argument(message.toStdString());
}

QColor opaqueColor(QString value) {
    static const QRegularExpression bare("^[0-9a-fA-F]{6}$");
    if (bare.match(value).hasMatch())
        value.prepend('#');
    const auto color = QColor::fromString(value);
    if (!color.isValid())
        fail("Unsupported palette color: " + value);
    return QColor(color.red(), color.green(), color.blue());
}

double channel(const QString &expression) {
    const auto parts = expression.trimmed().split('/');
    if (parts.size() > 2)
        fail("Unsupported Qt.rgba channel: " + expression);
    bool valid = false;
    double value = parts[0].trimmed().toDouble(&valid);
    if (!valid)
        fail("Invalid Qt.rgba channel: " + expression);
    if (parts.size() == 2) {
        bool denominatorValid = false;
        const double denominator = parts[1].trimmed().toDouble(&denominatorValid);
        if (!denominatorValid || denominator <= 0)
            fail("Invalid Qt.rgba divisor: " + expression);
        value /= denominator;
    }
    if (!std::isfinite(value) || value < 0 || value > 1)
        fail("Qt.rgba channels must be between 0 and 1");
    return value;
}

Palette qmlPalette(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        fail("Cannot read palette: " + path);
    QString contents = QString::fromUtf8(file.readAll());
    contents.remove(QRegularExpression("/\\*.*?\\*/", QRegularExpression::DotMatchesEverythingOption));
    contents.remove(QRegularExpression("//[^\\n]*"));
    static const QRegularExpression pattern(
        R"(property\s+color\s+(\w+)\s*:\s*(?:["'](#[\da-fA-F]{6}(?:[\da-fA-F]{2})?)["']|Qt\.rgba\(([^)]*)\)))");
    Palette result;
    auto matches = pattern.globalMatch(contents);
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (!match.captured(2).isEmpty()) {
            result.insert(match.captured(1), opaqueColor(match.captured(2)));
            continue;
        }
        const auto channels = match.captured(3).split(',');
        if (channels.size() != 4)
            fail("Qt.rgba needs four channels");
        channel(channels[3]);
        result.insert(match.captured(1), QColor::fromRgbF(channel(channels[0]), channel(channels[1]), channel(channels[2])));
    }
    return result;
}

Palette jsonPalette(QJsonObject data) {
    if (data.value("colours").isObject())
        data = data.value("colours").toObject();
    else if (data.value("colors").isObject())
        data = data.value("colors").toObject();
    else if (data.value("dark").isObject() || data.value("light").isObject()) {
        const auto mode = data.value("mode").toString("dark");
        data = data.value(mode).toObject();
    }
    Palette result;
    for (auto value = data.constBegin(); value != data.constEnd(); ++value) {
        if (!value.value().isString())
            continue;
        const QString text = value.value().toString();
        static const QRegularExpression color("^#?[0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?$");
        if (color.match(text).hasMatch())
            result.insert(value.key(), opaqueColor(text));
    }
    return result;
}

QColor pick(const Palette &palette, const QStringList &keys, QColor fallback = {}) {
    for (const auto &key : keys) {
        if (palette.contains(key))
            return palette.value(key);
    }
    if (!fallback.isValid())
        fail("Palette is missing " + keys.join(" / "));
    return fallback;
}

QColor mix(const QColor &base, const QColor &tint, double amount) {
    return QColor::fromRgbF(base.redF() * (1 - amount) + tint.redF() * amount,
                            base.greenF() * (1 - amount) + tint.greenF() * amount,
                            base.blueF() * (1 - amount) + tint.blueF() * amount);
}

double luminance(const QColor &color) {
    const auto linear = [](double component) {
        return component <= .04045 ? component / 12.92 : std::pow((component + .055) / 1.055, 2.4);
    };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}

Theme mapPalette(const Palette &palette, const QString &name) {
    const auto background = pick(palette, {"background", "base", "m3background", "mSurface", "surface"});
    const auto text = pick(palette, {"foreground", "onBackground", "on_background", "text", "mOnSurface", "onSurface", "on_surface"});
    const auto accent = pick(palette, {"primary", "accent", "mPrimary", "m3primary"});
    const auto surface = pick(palette, {"surface_container_low", "surfaceContainerLow", "backgroundAlt", "mantle"}, mix(background, text, .035));
    const auto alternate = pick(palette, {"surface_container", "surfaceContainer", "backgroundGray", "surface0", "mSurfaceVariant"}, mix(background, text, .07));
    const auto outline = pick(palette, {"outline", "border", "mOutline"}, accent);
    const QColor onAccent = luminance(accent) > .4 ? QColor("#151515") : QColor("#ffffff");
    Theme theme{name, {
        {"background", background}, {"surface", surface}, {"surface_alt", alternate}, {"text", text},
        {"muted", pick(palette, {"foregroundInactive", "on_surface_variant", "onSurfaceVariant", "mOnSurfaceVariant", "muted"}, mix(background, text, .65))},
        {"accent", accent}, {"accent_text", pick(palette, {"on_primary", "onPrimary", "mOnPrimary"}, onAccent)},
        {"border", mix(surface, outline, .25)}, 
    }, {}, {}};
    for (const auto &key : {"danger", "success"}) {
        const QStringList aliases = QString(key) == "danger"
            ? QStringList{"danger", "error", "red", "mError"}
            : QStringList{"success", "green"};
        for (const auto &alias : aliases) {
            if (palette.contains(alias)) {
                theme.colors.insert(key, palette.value(alias));
                break;
            }
        }
    }
    // Use the same 8-bit channels as the JSON to avoid repeated save/reload cycles.
    return parseTheme(theme.toJson());
}

QString expandedPath(QString path) {
    if (path.startsWith("~/"))
        path = QDir::homePath() + path.mid(1);
    return QFileInfo(path).absoluteFilePath();
}

QString sourcePath(const QString &requested) {
    const auto path = expandedPath(requested);
    if (!QFileInfo(path).isDir())
        return path;
    for (const auto &file : {"theme.json", "palette.json", "Colors.qml", "colors.json"}) {
        if (QFileInfo::exists(path + "/" + file))
            return path + "/" + file;
    }
    fail("No supported palette found in: " + path);
}

}

Theme importPalette(const QString &path, const QString &name) {
    const QString paletteName = name.isEmpty() ? QFileInfo(path).completeBaseName() : name;
    if (path.endsWith(".qml", Qt::CaseInsensitive))
        return mapPalette(qmlPalette(path), paletteName);
    const auto data = readJson(path);
    if (data.value("version").toInt() == 1 && data.value("colors").isObject() && data.value("colors").toObject().contains("accent_text"))
        return parseTheme(data);
    return mapPalette(jsonPalette(data), paletteName);
}

Theme followTheme(const QString &kind, const QString &path) {
    if (kind != "file" || path.isEmpty())
        fail("Use theme follow file FILE_OR_DIRECTORY");
    const auto resolved = sourcePath(path);
    auto theme = importPalette(resolved);
    theme.sourceKind = kind;
    theme.sourcePath = resolved;
    return theme;
}

Theme refreshSource(const Theme &theme) {
    return theme.sourceKind.isEmpty() ? theme : followTheme(theme.sourceKind, theme.sourcePath);
}

QStringList sourceWatchPaths(const Theme &theme) {
    if (theme.sourcePath.isEmpty())
        return {};
    QStringList paths;
    auto collect = [&paths](QString path) {
        while (!path.isEmpty()) {
            if (QFileInfo::exists(path))
                paths.append(path);
            const auto parent = QFileInfo(path).dir().absolutePath();
            if (parent == path)
                break;
            path = parent;
        }
    };
    // Observe both names so replacing a symlink or its target also reloads.
    collect(theme.sourcePath);
    const auto canonical = QFileInfo(theme.sourcePath).canonicalFilePath();
    if (!canonical.isEmpty())
        collect(canonical);
    paths.removeDuplicates();
    return paths;
}

}
