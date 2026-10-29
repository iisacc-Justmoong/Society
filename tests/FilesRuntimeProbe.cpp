// Opt-in measurements of the installed application's real Storage view.
#include "Society/Drive/DriveController.h"
#include <StorageDirectoryModel.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSValue>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QSGRendererInterface>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <memory>

namespace {
qint64 nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct FrameStamp {
    std::atomic<int> ready{-1}, synchronized{-1}, swapped{-1};
    std::atomic<qint64> ns{0};
};

QQuickItem *findItem(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name) return root;
    for (auto *child : root->childItems())
        if (auto *found = findItem(child, name)) return found;
    return nullptr;
}

int visualItemCount(QQuickItem *root)
{
    int count = 1;
    for (auto *child : root->childItems()) count += visualItemCount(child);
    return count;
}

class Probe final : public QObject {
public:
    Probe(QQuickWindow *window, DriveController *drive, QString output)
        : QObject(window), window(window), drive(drive), output(std::move(output))
    {
        window->setWidth(1212); window->setHeight(844);
        const auto shared = stamp;
        connect(window, &QQuickWindow::afterSynchronizing, window, [shared] {
            shared->synchronized.store(shared->ready.load());
        }, Qt::DirectConnection);
        connect(window, &QQuickWindow::frameSwapped, window, [shared] {
            const int stage = shared->synchronized.load();
            if (stage >= 0 && shared->swapped.load() != stage) {
                shared->ns.store(nowNs()); shared->swapped.store(stage);
            }
        }, Qt::DirectConnection);
        connect(&timer, &QTimer::timeout, this, [this] { poll(); });
        timer.start(2);
    }

private:
    void start(int next)
    {
        stage = next; stamp->ready.store(-1); stamp->swapped.store(-1);
        started = nowNs();
        if (stage == 0) {
            navigated = started;
            window->setProperty("selectedTab", "Storage");
            if (!drive->openSection("files")) finish(false);
        } else {
            auto *scroll = findItem(window->contentItem(), "filesTableScroll");
            if (scroll) scroll->setProperty("contentY", stage == 1 ? 24000.0
                : scroll->property("contentHeight").toDouble() - scroll->height());
        }
    }

    void poll()
    {
        if ((nowNs() - launched) / 1000000 > 40000) { finish(false); return; }
        if (stage == -1) {
            if (drive->contentsAvailable() && !drive->busy() && window->isExposed()) start(0);
            return;
        }
        if (stage < 0) return;
        auto *browser = findItem(window->contentItem(), "filesBrowser");
        auto *table = findItem(window->contentItem(), "filesTable");
        if (!browser || !table) return;
        auto *model = qobject_cast<iiSocietyContainer::StorageDirectoryModel *>(browser->property("directoryModel").value<QObject *>());
        const bool reconciled = model && model->status() == iiSocietyContainer::StorageDirectoryModel::Ready;
        if (reconciled && !report.contains("liveReconciliationMs"))
            report["liveReconciliationMs"] = (nowNs() - navigated) / 1000000.0;
        if (stage == 3) { if (reconciled) finish(true); return; }
        const auto cells = qmlEngine(table)->newQObject(table).property("visibleCellItems");
        const int count = cells.property("length").toInt();
        const bool populated = browser->property("count").toInt() == 2000
            && table->property("rowCount").toInt() == 2000 && count > 0 && count < 200;
        const bool positioned = stage == 0 || (stage == 1
            ? cells.property(0).property("rowIndex").toInt() > 900
            : cells.property(count - 1).property("rowIndex").toInt() == 1999);
        if (stamp->ready.load() != stage && populated && positioned) {
            if (stage == 0) report["guiReadyMs"] = (nowNs() - started) / 1000000.0;
            stamp->ready.store(stage); window->update();
        }
        if (stamp->swapped.load() != stage) return;
        const auto elapsed = (stamp->ns.load() - started) / 1000000.0;
        report[stage == 0 ? "firstPopulatedFrameMs" : stage == 1 ? "middleScrollFrameMs" : "lastScrollFrameMs"] = elapsed;
        report["visibleCells"] = count;
        report["rowCount"] = table->property("rowCount").toInt();
        if (stage == 0) {
            report["firstFrameReconciled"] = reconciled;
            report["sceneItems"] = visualItemCount(browser);
            report["screenshotSaved"] = window->grabWindow().save(output + ".png");
        }
        const int next = stage + 1; stage = -2; stamp->ready.store(-1);
        QTimer::singleShot(250, this, [this, next] {
            if (next < 3) start(next);
            else stage = 3;
        });
    }

    void finish(bool complete)
    {
        timer.stop(); stamp->ready.store(-1);
        report["complete"] = complete;
        report["application"] = QCoreApplication::applicationFilePath();
        report["source"] = drive->rootPath();
        report["windowVisible"] = window->isVisible();
        report["driveError"] = drive->errorString();
        report["graphicsApi"] = int(window->rendererInterface()->graphicsApi());
        report["graphicsApiName"] = window->rendererInterface()->graphicsApi() == QSGRendererInterface::Metal ? "Metal" : "Other";
        QSaveFile file(output);
        if (file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument(report).toJson()); file.commit(); }
        if (auto *scroll = findItem(window->contentItem(), "filesTableScroll")) scroll->setProperty("contentY", 0);
    }

    QQuickWindow *window;
    DriveController *drive;
    QString output;
    QTimer timer;
    const qint64 launched = nowNs();
    qint64 started = 0;
    qint64 navigated = 0;
    int stage = -1;
    QJsonObject report;
    std::shared_ptr<FrameStamp> stamp = std::make_shared<FrameStamp>();
};
}

void societyFilesRuntimeProbe(QObject *root)
{
    const auto output = qEnvironmentVariable("SOCIETY_FILES_PROBE_OUTPUT");
    if (output.isEmpty()) return;
    // A probe can navigate only a deliberately isolated, signed-out fixture.
    const QDir scope(QFileInfo(output).absolutePath());
    const auto inScope = [&scope](const char *variable) {
        const auto path = qEnvironmentVariable(variable);
        const auto relative = scope.relativeFilePath(path);
        return QFileInfo(path).isAbsolute() && relative != ".." && !relative.startsWith("../");
    };
    if (qEnvironmentVariableIntValue("SOCIETY_DISABLE_SESSION_RESTORE") != 1
        || !inScope("SOCIETY_STORAGE_SETTINGS_PATH") || !inScope("SOCIETY_HELPER_DIRECTORY")
        || !inScope("SOCIETY_CONTAINER_PATH")) return;
    auto *window = qobject_cast<QQuickWindow *>(root);
    auto *drive = root->findChild<DriveController *>("driveController");
    if (window && drive) new Probe(window, drive, output);
}
