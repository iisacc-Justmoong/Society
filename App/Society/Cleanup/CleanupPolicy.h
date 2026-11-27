#pragma once

#include <QJsonObject>
#include <QString>

namespace society::cleanup {

// Evidence is supplied by independent observation adapters. Unknown facts must
// remain false; a metadata stamp is not proof of current byte identity.
struct Evidence final {
    bool targetObservationMatches=false;
    bool dependencyCoverageComplete=false;
    bool hasRecordedRelations=false;
    bool relationshipObservationIncomplete=false;
    bool recoverabilityVerified=false;
};

// A plan is an explanation for review, never an approval or executable token.
struct Plan final {
    QJsonObject value;
    QString error;
};

// Evaluate only the supplied facts. Even complete evidence reaches an
// approval-required state, and this function never grants execution authority.
[[nodiscard]] QJsonObject evaluate(const Evidence &evidence);

// Read an observed Files/Models target and its bounded direct relations without
// repairing bindings or mutating files. The current collector cannot prove
// complete dependency coverage or a usable recovery copy, so it cannot produce
// an executable cleanup operation. This blocking call belongs on an I/O worker.
[[nodiscard]] Plan plan(const QString &container,const QString &logicalPath,int relationLimit);

}
