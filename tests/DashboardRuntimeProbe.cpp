// Opt-in inspection of the real view and store. Never changes library data.
#include "App/Dashboard/DashboardFiles.h"
#include "App/Drive/DriveController.h"
#include "App/Network/NetworkDriveController.h"
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

namespace {
QQuickItem *findItem(QQuickItem *item, const QString &name)
{
    if (item->objectName() == name) return item;
    for (auto *child : item->childItems())
        if (auto *found = findItem(child, name)) return found;
    return nullptr;
}
}

void societyDashboardRuntimeProbe(QObject *root)
{
    if (qEnvironmentVariableIntValue("SOCIETY_DASHBOARD_PROBE") != 1) return;
    auto *window = qobject_cast<QQuickWindow *>(root);
    auto *files = root->findChild<DashboardFiles *>("dashboardFiles");
    auto *drive = root->findChild<DriveController *>("driveController");
    auto *network = root->findChild<NetworkDriveController *>("networkDriveController");
    if (!window || !files || !drive || !network) return;
    auto *timer = new QTimer(root); timer->setInterval(500);
    QElapsedTimer elapsed; elapsed.start();
    QObject::connect(timer, &QTimer::timeout, root,
        [window, files, drive, network, timer, elapsed, firstRowsMs = qint64(-1), captured = false,
         observations = QJsonArray(), previous = QJsonObject()]() mutable {
            const auto item = [window](const QString &name) { return findItem(window->contentItem(), name); };
            const auto property = [&item](const QString &name, const char *key) {
                auto *found = item(name); return found ? found->property(key) : QVariant();
            };
            const auto recent = property("dashboardRecentFilesCards", "count").toInt();
            const auto history = property("dashboardGenerationHistoryCards", "count").toInt();
            if (firstRowsMs < 0 && recent > 0 && history > 0) firstRowsMs = elapsed.elapsed();
            const auto preview = property("dashboardGenerationHistoryCard0", "previewStatus").toInt();
            const auto directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
            const bool settled = firstRowsMs >= 0 && elapsed.elapsed() - firstRowsMs >= 2000 && preview == 1;
            const bool finished = elapsed.elapsed() >= 30000;
            if ((!captured && settled) || finished)
                captured = window->grabWindow().save(QDir(directory).filePath("dashboard-probe.png"));
            QJsonObject state{{"recentViewCount", recent}, {"historyViewCount", history},
                {"firstPreviewStatus", preview},
                {"firstRecentName", property("dashboardRecentFilesCard0", "filename").toString()},
                {"firstHistoryName", property("dashboardGenerationHistoryCard0", "filename").toString()}};
            if (state != previous) {
                previous = state; state.insert("elapsedMs", elapsed.elapsed()); observations.append(state);
            }
            const QJsonObject report{{"elapsedMs", elapsed.elapsed()}, {"firstRowsMs", firstRowsMs},
                {"finished", finished}, {"screenshotSaved", captured},
                {"active", QGuiApplication::applicationState() == Qt::ApplicationActive},
                {"rootPath", drive->rootPath()}, {"modelPath", files->containerPath()},
                {"driveError", drive->errorString()}, {"modelError", files->errorString()},
                {"networkReady", network->containerReady()}, {"loading", files->loading()},
                {"recentModelCount", files->recentFiles().size()}, {"historyModelCount", files->generationHistory().size()},
                {"recentViewCount", recent}, {"historyViewCount", history}, {"firstPreviewStatus", preview},
                {"observations", observations}, {"recentTitle", property("dashboardRecentFilesTitle", "text").toString()},
                {"historyTitle", property("dashboardGenerationHistoryTitle", "text").toString()}};
            QSaveFile output(QDir(directory).filePath("dashboard-probe.json"));
            if (output.open(QIODevice::WriteOnly)) { output.write(QJsonDocument(report).toJson()); output.commit(); }
            if (finished) timer->stop();
        });
    timer->start();
}
