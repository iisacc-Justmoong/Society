#pragma once

#include <QJsonObject>
#include <QString>

namespace iiSocietyContainer { class SharedStorage; }

// Application composition over the container graph and project-owned graph.
// Blocking I/O: call from a worker. Address bindings follow verified native
// identities. actorId is host-declared provenance rather than authentication. Project
// addresses must resolve locally because their canonical metadata is in the
// package. Other addresses may remain unresolved without reading file bytes.
class SemanticRepository final {
public:
    SemanticRepository(QString containerPath, QString actorId);

    // Returns address, owner, metadata and the owner's decimal revision.
    [[nodiscard]] QJsonObject read(const QString &address) const;
    // Replaces the entire metadata object. All writes compare the exact owner
    // revision returned by read(); a mismatch rejects the write.
    [[nodiscard]] QJsonObject setMetadata(const QString &address, const QJsonObject &metadata,
                                          const QString &expectedRevision) const;
    // A directed relation is owned by source; the target need not exist.
    // Relation metadata is a JSON object and the returned ID is stable on edits.
    [[nodiscard]] QJsonObject putRelation(const QString &source, const QString &predicate, const QString &target,
                                          const QJsonObject &metadata, const QString &expectedRevision) const;
    // Create a relation without replacing metadata on an existing matching
    // relation. The supplied owner revision is still checked.
    [[nodiscard]] QJsonObject putRelationIfAbsent(const QString &source, const QString &predicate, const QString &target,
                                                  const QJsonObject &metadata, const QString &expectedRevision) const;
    [[nodiscard]] QJsonObject removeRelation(const QString &source, const QString &relationId,
                                             const QString &expectedRevision) const;
    // Incoming and outgoing results are bounded. 'incomplete' reports truncation,
    // concurrent graph changes and registered projects that could not be read.
    [[nodiscard]] QJsonObject relations(const QString &address) const;
    // Follow bounded user-asserted relations in an already selected storage.
    // Use the SDK's observational storage handle; an ordinary SDK open may
    // migrate a legacy layout or start maintenance before this call.
    // Coverage and endpoint identity remain unknown; this is not authority to
    // delete or merge content. Background work already active may continue.
    [[nodiscard]] QJsonObject traceRelations(const iiSocietyContainer::SharedStorage &storage,
                                             const QString &address, const QString &direction,
                                             int maxDepth, int maxEdges) const;
    // A host coordinates these calls with a native move. Preparation records
    // an intent before the move; commit updates every affected address binding.
    void prepareRelocation(const QString &source, const QString &destination) const;
    void commitRelocation(const QString &source, const QString &destination) const;
    void cancelRelocation(const QString &source, const QString &destination) const;
    // An explicitly asserted external move can resolve an otherwise
    // unverifiable cross-volume file relocation.
    [[nodiscard]] QJsonObject rebindRelocation(const QString &sourceAddress, const QString &destinationAddress) const;

private:
    [[nodiscard]] QJsonObject putRelationImpl(const QString &source, const QString &predicate, const QString &target,
                                               const QJsonObject &metadata, const QString &expectedRevision, bool onlyIfAbsent) const;
    QString containerPath_, actorId_;
};
