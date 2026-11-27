#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <stop_token>

namespace society::content {

// Incremental, non-destructive intake of resident Files entries. The host
// supplies the formats and sizes it is prepared to process; filesystem events
// are only hints, and a complete pass is never evidence of complete coverage.
class FileDiscovery final {
public:
    using Candidate = std::function<bool(const QString &,quint64)>;
    /// Select observation only or streaming hash plus directory metadata.
    enum class Mode { Observe, HashMetadata };

    // Watch paths are transient host hints and must not be persisted or exposed
    // as a complete inventory. The status contains only the latest slice.
    struct Batch final {
        QJsonObject status;
        QStringList directories;
        QStringList files;
        // Matching public paths for successfully admitted candidates only.
        QStringList logicalFiles;
        QString error;
    };

    explicit FileDiscovery(QString container,Candidate candidate,Mode mode=Mode::Observe);
    ~FileDiscovery();
    FileDiscovery(const FileDiscovery &)=delete;
    FileDiscovery &operator=(const FileDiscovery &)=delete;

    // Consume at most 32 directory entries and admit at most one candidate.
    // Only one worker may call this object at a time. A hint received during a
    // pass schedules another pass after it, avoiding starvation of later files.
    [[nodiscard]] Batch step(bool changed,std::stop_token cancellation={});

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

}
