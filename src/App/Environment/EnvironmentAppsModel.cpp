#include "EnvironmentAppsModel.h"
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <filesystem>
#include <array>

EnvironmentAppsModel::EnvironmentAppsModel(QObject *parent) : QObject(parent) { refresh(); }

void EnvironmentAppsModel::refresh()
{
    struct App { const char *id, *name, *symbol, *description; };
    constexpr std::array catalogue {
        App{"society", "Society", "So", "Keep your files and devices together."},
        App{"dreamscapes", "Dreamscapes", "Dr", "Create images and explore visual ideas."},
        App{"vincent", "Vincent", "Vi", "Draw, paint and build your next artwork."},
        App{"canvas", "Canvas", "Ca", "Arrange layouts, boards and design assets."},
        App{"motion", "Motion", "Mo", "Edit short videos and motion studies."},
        App{"notes", "Notes", "No", "Capture notes and keep project references."},
        App{"studio", "Studio", "St", "A shared workspace for creative projects."},
        App{"render", "Render", "Re", "Prepare and process media on your devices."},
        App{"library", "Library", "Li", "Collect and organize reusable assets."}
    };
    QVariantList next;
    for (size_t i = 0; i < catalogue.size(); ++i) {
        const auto &app = catalogue[i];
        QString path;
#if defined(Q_OS_MACOS)
        // Only shipped, unambiguous iisacc products are detected. A generic
        // Notes/Motion/Studio bundle may belong to a different publisher.
        if (i == 1 || i == 2) {
            for (const auto &base : {QStringLiteral("/Applications"), QDir::homePath() + "/Applications"}) {
                const auto candidate = base + '/' + app.name + ".app";
                std::error_code ec;
                if (std::filesystem::is_regular_file((candidate + "/Contents/MacOS/" + app.name).toStdString(), ec)) {
                    path = candidate;
                    break;
                }
            }
        }
#endif
        const bool installed = i == 0 || !path.isEmpty();
        next.append(QVariantMap{{"id", app.id}, {"name", app.name}, {"symbol", app.symbol},
            {"description", app.description}, {"publisher", "iisacc"}, {"palette", int(i % 6)},
            {"installed", installed}, {"path", path},
            {"status", installed ? tr("Installed · This device") : tr("Not installed")},
            {"license", i == 0 ? tr("Included with your account") : tr("License not verified")},
            {"action", installed ? tr("Open") : tr("Install")}, {"actionEnabled", installed}});
    }
    if (next != m_apps) { m_apps = next; emit changed(); }
}

bool EnvironmentAppsModel::open(const QString &id)
{
    refresh();
    for (const auto &value : m_apps) {
        const auto app = value.toMap();
        if (app.value("id").toString() != id || !app.value("installed").toBool()) continue;
        if (id == "society") { emit societyRequested(); return true; }
        return QDesktopServices::openUrl(QUrl::fromLocalFile(app.value("path").toString()));
    }
    return false;
}
