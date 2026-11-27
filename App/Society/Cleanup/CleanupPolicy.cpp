#include "CleanupPolicy.h"
#include "Society/Resources/ResourceInspection.h"
#include "Society/Semantics/SemanticRepository.h"
#include <iiSocietyContainer>
#include <QJsonArray>
#include <exception>

namespace society::cleanup {
namespace {
// Select the identity owning the resource graph, including logical disks whose
// local storage identifier differs from the shared drive identifier.
QString storageId(const iiSocietyContainer::SharedStorage &storage) {
    const auto &drive=storage.drive();
    return drive.storageKind()==iiSocietyContainer::SocietyDrive::StorageKind::LogicalDisk
        ? drive.storageIdentifier() : drive.identifier();
}

// Confirm that two metadata observations describe the same recorded resource
// and revision. This deliberately does not assert byte equality or a backup.
bool matches(const society::resources::Inspection &before,const society::resources::Inspection &after) {
    if (!before.error.isEmpty() || !after.error.isEmpty()) return false;
    const auto first=before.snapshot.value("resource").toObject(), second=after.snapshot.value("resource").toObject();
    const auto firstRevision=before.snapshot.value("contentRevision").toObject();
    const auto secondRevision=after.snapshot.value("contentRevision").toObject();
    return !first.value("id").toString().isEmpty() && !firstRevision.value("id").toString().isEmpty()
        && first.value("id")==second.value("id") && firstRevision.value("id")==secondRevision.value("id")
        && firstRevision.value("digest")==secondRevision.value("digest");
}
}

QJsonObject evaluate(const Evidence &evidence) {
    QJsonArray reasons;
    if (!evidence.targetObservationMatches) reasons.append("target_unverified");
    if (!evidence.dependencyCoverageComplete) reasons.append("dependency_coverage_unknown");
    if (evidence.relationshipObservationIncomplete) reasons.append("relationship_observation_incomplete");
    if (evidence.hasRecordedRelations) reasons.append("recorded_relations_require_review");
    if (!evidence.recoverabilityVerified) reasons.append("recoverability_unverified");
    return {{"decision",reasons.isEmpty()?QStringLiteral("approval_required"):QStringLiteral("blocked")},
            {"reasons",reasons},{"approval","not_requested"},{"executable",false}};
}

Plan plan(const QString &container,const QString &logicalPath,int relationLimit) {
    Plan result;
    if (relationLimit<1 || relationLimit>20) { result.error="Invalid cleanup relation limit."; return result; }
    QString address;
    if (logicalPath.startsWith("Files/")) address="files/"+logicalPath.mid(6);
    else if (logicalPath.startsWith("Models/")) address="models/"+logicalPath.mid(7);
    else { result.error="Use a visible Files or Models path for a cleanup plan."; return result; }
    try {
        QString error;
        const auto storage=iiSocietyContainer::SharedStorage::inspect(container,&error);
        if (!storage) { result.error=error.isEmpty()?QStringLiteral("The Society storage is unavailable."):error; return result; }
        const auto containerId=storageId(*storage);
        if (containerId.isEmpty()) { result.error="The Society storage identity is unavailable."; return result; }
        const auto before=society::resources::inspect(container,logicalPath);
        const auto relations=SemanticRepository(container,"urn:iisacc:local:cleanup-review")
            .traceRelations(*storage,address,"both",1,relationLimit);
        const auto after=society::resources::inspect(container,logicalPath);
        const auto checked=iiSocietyContainer::SharedStorage::inspect(container,&error);
        if (!checked || storageId(*checked)!=containerId || relations.value("containerId")!=containerId) {
            result.error="The selected Society storage changed during cleanup assessment."; return result;
        }
        Evidence evidence;
        evidence.targetObservationMatches=matches(before,after);
        evidence.hasRecordedRelations=!relations.value("edges").toArray().isEmpty();
        evidence.relationshipObservationIncomplete=relations.value("incomplete").toBool();
        // The present graph does not cover all external projects or file-internal
        // references; declared replicas have not been inspected or restored.
        evidence.dependencyCoverageComplete=false;
        evidence.recoverabilityVerified=false;
        QJsonObject target{{"path",logicalPath},{"status",evidence.targetObservationMatches?"last_observed":"unverified"},
                           {"bytesVerified",false}};
        QJsonValue replicas(QJsonValue::Null);
        if (evidence.targetObservationMatches) {
            const auto resource=after.snapshot.value("resource").toObject();
            const auto revision=after.snapshot.value("contentRevision").toObject();
            target.insert("resourceId",resource.value("id")); target.insert("revisionId",revision.value("id"));
            target.insert("digest",revision.value("digest"));
            replicas=QJsonValue(int(after.snapshot.value("replicas").toArray().size()));
        } else {
            const auto detail=!after.error.isEmpty()?after.error:before.error;
            if (!detail.isEmpty()) target.insert("detail",detail);
        }
        result.value={{"containerId",containerId},{"operation","trash"},{"target",target},{"relations",relations},
            {"recoverability",QJsonObject{{"status","unverified"},{"recordedReplicas",replicas}}},
            {"snapshot","non_atomic"},{"policy",evaluate(evidence)}};
    } catch (const std::exception &error) {
        result.error=QString::fromUtf8(error.what());
        if (result.error.isEmpty()) result.error=QStringLiteral("Could not assess cleanup.");
    } catch (...) { result.error=QStringLiteral("Could not assess cleanup."); }
    return result;
}
}
