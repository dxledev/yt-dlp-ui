#include "theme.h"
#include "storage.h"

#include <QFileInfo>
#include <QDir>
#include <QLockFile>
#include <QRegularExpression>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace ytdlp {
namespace {

const QMap<QString, QStringList> palettes{
    {"forest", {"#101819", "#182324", "#1e2d2e", "#edf3ed", "#91a6a2", "#b6e3c6", "#152c24", "#304243", "#536967", "#355c4b", "#203230", "#2b453b"}},
    {"paper", {"#f4f1e9", "#fffcf5", "#ebe7dd", "#292f2c", "#6f7970", "#315c48", "#ffffff", "#ddd8cb", "#9aab9b", "#ccdfcc", "#eeeee1", "#dfebd7"}},
    {"slate", {"#131720", "#1c2230", "#252e40", "#ebeffa", "#97a3be", "#b2c7ff", "#1d2a4b", "#34405a", "#576986", "#3b4e78", "#252e44", "#30415d"}},
    {"rose", {"#211a22", "#2c232e", "#382d3b", "#f7eaf1", "#b8a0b2", "#efb7ce", "#3a2131", "#4e3a4c", "#85687f", "#654258", "#3b2c3b", "#4c3447"}},
};

[[noreturn]] void invalid(const QString &message) {
    throw std::invalid_argument(message.toStdString());
}

double luminance(const QColor &color) {
    const auto linear = [](double component) {
        return component <= .04045 ? component / 12.92 : std::pow((component + .055) / 1.055, 2.4);
    };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}

double minimumContrast(const QColor &color, const std::array<double, 4> &backgrounds) {
    const double foreground = luminance(color);
    double contrast = 21;
    for (const double background : backgrounds)
        contrast = std::min(contrast, (std::max(foreground, background) + .05)
                                      / (std::min(foreground, background) + .05));
    return contrast;
}

QColor readableStatusColor(const Theme &theme, double hue, double minimumSaturation) {
    const auto accent = theme.color("accent");
    const double saturation = std::clamp(double(accent.hslSaturationF()), minimumSaturation, .75);
    const double preferredLightness = std::clamp(double(accent.lightnessF()), .35, .8);
    const std::array backgrounds{luminance(theme.color("surface")), luminance(theme.color("background")),
                                luminance(theme.color("surface_alt")), luminance(theme.color("surface"))};
    const auto candidate = [hue, saturation](double lightness) {
        return QColor::fromRgb(QColor::fromHslF(hue, saturation, lightness).rgb());
    };
    auto best = candidate(preferredLightness);
    double bestContrast = minimumContrast(best, backgrounds);
    double closestLightness = 1;
    if (bestContrast >= 4.5)
        return best;
    for (int step = 25; step <= 85; ++step) {
        const double lightness = step / 100.0;
        const auto color = candidate(lightness);
        const double contrast = minimumContrast(color, backgrounds);
        const double distance = std::abs(lightness - preferredLightness);
        if ((contrast >= 4.5 && distance < closestLightness)
            || (bestContrast < 4.5 && contrast > bestContrast)) {
            best = color;
            bestContrast = contrast;
            if (contrast >= 4.5)
                closestLightness = distance;
        }
    }
    return best;
}

QColor generateDangerColor(const Theme &theme) {
    const auto surface = theme.color("surface");
    const double hue = std::fmod(1.01 + .025 * (surface.redF() - surface.blueF()), 1.0);
    return readableStatusColor(theme, hue, .45);
}

QColor generateSuccessColor(const Theme &theme) {
    const auto accent = theme.color("accent");
    const double hue = accent.hslHueF();
    const bool redAdjacent = hue >= 0 && (hue <= .125 || hue >= .875) && accent.hslSaturationF() >= .1;
    if (!redAdjacent)
        return accent;
    return readableStatusColor(theme, std::fmod(hue + .5, 1.0), .25);
}

void cacheStatusColors(Theme &theme) {
    theme.dangerColor = theme.colors.value("danger", generateDangerColor(theme));
    theme.successColor = theme.colors.value("success", generateSuccessColor(theme));
    theme.colors.insert("danger", theme.dangerColor);
    theme.colors.insert("success", theme.successColor);
}

}

QStringList colorKeys() {
    return {"background", "surface", "surface_alt", "text", "muted", "accent", "accent_text",
            "border", "danger", "success"};
}

QStringList presetNames() {
    return palettes.keys();
}

Theme presetTheme(const QString &name) {
    if (!palettes.contains(name))
        invalid("Unknown preset: " + name + ". Available: " + presetNames().join(", "));
    Theme theme{name, {}, {}, {}};
    const QStringList keys{"background", "surface", "surface_alt", "text", "muted", "accent", "accent_text", "border"};
    const auto values = palettes.value(name);
    for (int index = 0; index < keys.size(); ++index)
        theme.colors.insert(keys[index], QColor(values[index]));
    cacheStatusColors(theme);
    return theme;
}

QJsonObject Theme::toJson() const {
    QJsonObject result;
    for (const auto &key : colorKeys()) {
        if (colors.contains(key)) result.insert(key, color(key).name());
    }
    QJsonObject data{{"version", 1}, {"name", name}, {"colors", result}};
    if (!sourceKind.isEmpty())
        data.insert("source", QJsonObject{{"kind", sourceKind}, {"path", sourcePath}});
    return data;
}

Theme parseTheme(const QJsonObject &data) {
    if (data.value("version").toInt(-1) != 1)
        invalid("Theme version must be 1");
    const QString name = data.value("name").toString().trimmed();
    if (name.isEmpty() || name.size() > 60)
        invalid("Theme name must contain 1–60 characters");
    if (!data.value("colors").isObject())
        invalid("Theme must contain a colors object");
    const auto colors = data.value("colors").toObject();
    const auto keys = colorKeys();
    for (auto color = colors.constBegin(); color != colors.constEnd(); ++color) {
        if (!keys.contains(color.key()))
            invalid("Unknown color: " + color.key());
    }
    static const QRegularExpression pattern("^#[0-9a-fA-F]{6}$");
    Theme theme{name, {}, {}, {}};
    for (const auto &key : keys) {
        if ((key == "danger" || key == "success") && !colors.contains(key))
            continue;
        const QString value = colors.value(key).toString();
        if (!pattern.match(value).hasMatch())
            invalid(key + ": expected a color in #RRGGBB format");
        theme.colors.insert(key, QColor(value));
    }
    cacheStatusColors(theme);
    if (data.contains("source")) {
        if (!data.value("source").isObject())
            invalid("Theme source must be an object");
        const auto source = data.value("source").toObject();
        theme.sourceKind = source.value("kind").toString();
        theme.sourcePath = source.value("path").toString();
        if (theme.sourceKind != "file"
            || !QFileInfo(theme.sourcePath).isAbsolute() || theme.sourcePath.isEmpty())
            invalid("Theme source needs a known kind and an absolute path");
    }
    return theme;
}

Theme loadTheme(const QString &path) {
    return parseTheme(readJson(path));
}

Theme ensureTheme(const QString &path) {
    const auto directory = QFileInfo(path).absolutePath() + "/.locks";
    if (!QDir().mkpath(directory)) throw std::runtime_error("Cannot create theme lock directory");
    QLockFile lock(directory + "/theme.lock");
    if (!lock.tryLock(2000)) throw std::runtime_error("Theme is busy; try again");
    if (QFileInfo::exists(path))
        return loadTheme(path);
    const auto theme = presetTheme("slate");
    writeJson(path, theme.toJson());
    return theme;
}

Theme setColors(Theme theme, const QStringList &assignments) {
    if (assignments.isEmpty())
        invalid("Provide at least one COLOR=#RRGGBB assignment");
    auto data = theme.toJson();
    auto colors = data.value("colors").toObject();
    for (const auto &assignment : assignments) {
        const auto separator = assignment.indexOf('=');
        const auto key = assignment.left(separator);
        if (separator < 1 || !colorKeys().contains(key))
            invalid("Expected COLOR=#RRGGBB. Colors: " + colorKeys().join(", "));
        colors.insert(key, assignment.mid(separator + 1));
    }
    data.insert("name", "custom");
    data.remove("source");
    data.insert("colors", colors);
    return parseTheme(data);
}

}
