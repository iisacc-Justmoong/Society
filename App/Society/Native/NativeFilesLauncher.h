#pragma once
#include <QString>
#include <functional>

namespace society::native {
using StartFiles = std::function<bool(const QString &program)>;
bool launchFiles(const QString &program, const StartFiles &start, QString *error = nullptr);
bool launchBundledFiles(QString *error = nullptr);
}
