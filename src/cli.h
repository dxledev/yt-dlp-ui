#pragma once
#include "core/storage.h"
#include <QStringList>
namespace ytdlp {
int runCli(const Paths &paths, QStringList arguments);
QString helpText();
}
