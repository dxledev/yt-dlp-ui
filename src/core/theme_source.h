#pragma once

#include "theme.h"

namespace ytdlp {

Theme importPalette(const QString &path, const QString &name = {});
Theme followTheme(const QString &kind, const QString &path = {});
Theme refreshSource(const Theme &theme);
QStringList sourceWatchPaths(const Theme &theme);

}
