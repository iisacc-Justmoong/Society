#pragma once

#include <QDir>
#include <QString>

namespace society::cleanup {

// Treat a move into the Deleted section as cleanup too. Case-insensitive
// comparison is conservative on case-sensitive filesystems and avoids an
// alias bypass on filesystems where section names are case-insensitive.
[[nodiscard]] inline bool requiresPolicy(const QString &action,const QString &argument) {
    if (action=="trash" || action=="remove") return true;
    if (action!="move") return false;
    const auto destination=QDir::cleanPath(QDir::fromNativeSeparators(argument));
    return destination.section('/',0,0).compare(QStringLiteral("Deleted"),Qt::CaseInsensitive)==0;
}

}
