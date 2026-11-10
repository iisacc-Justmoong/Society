// Opt-in measurement of the native window's first swap and GUI event loop.
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <atomic>
#include <cstdio>
#include <memory>

namespace {
struct State {
    std::atomic<qint64> firstFrameMs{0};
    std::atomic<qint64> firstFrameElapsed{-1};
    QElapsedTimer clock;
    qint64 previousTick = 0, maximumGap = 0, samples = 0;
};
}

void societyLifecycleRuntimeProbe(QQmlApplicationEngine &engine)
{
    if (qEnvironmentVariableIntValue("SOCIETY_LIFECYCLE_PROBE") != 1) return;
    const auto scopePath = qEnvironmentVariable("SOCIETY_LIFECYCLE_PROBE_SCOPE");
    const QFileInfo scopeInfo(scopePath);
    if (!scopeInfo.isAbsolute() || !scopeInfo.isDir() || scopeInfo.isSymLink()
        || qEnvironmentVariableIntValue("SOCIETY_DISABLE_SESSION_RESTORE") != 1) return;
    const QDir scope(scopeInfo.canonicalFilePath());
    for (const auto variable : {"SOCIETY_STORAGE_SETTINGS_PATH", "SOCIETY_HELPER_DIRECTORY", "SOCIETY_CONTAINER_PATH"}) {
        const QFileInfo info(qEnvironmentVariable(variable));
        const auto relative = scope.relativeFilePath(info.absoluteFilePath());
        if (!info.isAbsolute() || relative == ".." || relative.startsWith("../")) return;
    }
    auto state = std::make_shared<State>();
    state->clock.start();
    auto *timer = new QTimer(&engine);
    timer->setInterval(20);
    QObject::connect(timer, &QTimer::timeout, &engine, [state] {
        if (!state->firstFrameMs.load()) return;
        const auto now = state->clock.elapsed();
        const auto previous = state->previousTick ? state->previousTick : state->firstFrameElapsed.load();
        if (previous >= 0) {
            const auto gap = now - previous;
            state->maximumGap = qMax(state->maximumGap, gap);
            if (gap >= 150) {
                std::fprintf(stderr, "SOCIETY_LIFECYCLE gap_ms=%lld since_frame_ms=%lld\n",
                    static_cast<long long>(gap), static_cast<long long>(now - state->firstFrameElapsed.load()));
                std::fflush(stderr);
            }
        }
        state->previousTick = now;
        ++state->samples;
    });
    timer->start();
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &engine,
        [state](QObject *root, const QUrl &) {
            auto *window = qobject_cast<QQuickWindow *>(root);
            if (!window) return;
            QObject::connect(window, &QQuickWindow::frameSwapped, window, [state] {
                if (state->firstFrameMs.load()) return;
                qint64 expected = 0;
                const auto now = QDateTime::currentMSecsSinceEpoch();
                state->firstFrameElapsed.store(state->clock.elapsed());
                if (state->firstFrameMs.compare_exchange_strong(expected, now)) {
                    std::fprintf(stderr, "SOCIETY_LIFECYCLE first_frame_ms=%lld\n", static_cast<long long>(now));
                    std::fflush(stderr);
                }
            }, Qt::DirectConnection);
        });
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, &engine, [state] {
        std::fprintf(stderr, "SOCIETY_LIFECYCLE quit_ms=%lld samples=%lld max_gap_ms=%lld\n",
            static_cast<long long>(QDateTime::currentMSecsSinceEpoch()),
            static_cast<long long>(state->samples), static_cast<long long>(state->maximumGap));
        std::fflush(stderr);
    });
}
