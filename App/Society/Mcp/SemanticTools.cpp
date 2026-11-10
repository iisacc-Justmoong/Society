#include "SemanticTools.h"
#include "Society/Semantics/SemanticRepository.h"
#include <QJsonArray>
#include <stdexcept>
#include <utility>

namespace a = iiMultimodalAgent::agent;
namespace {
QJsonObject input(QJsonObject properties = {}, QJsonArray required = {}) {
    return {{"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false}};
}
}
QList<a::Tool> societySemanticTools(SocietySemanticRoot root) {
    QList<a::Tool> tools;
    const auto semantic = [root = std::move(root)](const QJsonObject& args, const a::ToolContext& context, const auto& work) {
        try {
            context.cancellation.throwIfCancelled();
            const auto path = root(context);
            if (path.isEmpty()) throw std::runtime_error("Society's current container is not available");
            const auto actor = args.value("actor").toString(QStringLiteral("urn:iisacc:local-mcp:reader"));
            SemanticRepository repository(path, actor);
            return a::ToolResult{"Society semantic data", work(repository)};
        } catch (const std::exception& error) {
            return a::ToolResult{QString::fromUtf8(error.what()), {}, true};
        }
    };
    const auto address = QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}};
    const auto actor = QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}};
    const auto revision = QJsonObject{{"type", "string"}, {"pattern", "^(0|[1-9][0-9]*)$"}};
    a::Tool semanticRead;
    semanticRead.definition = {"semantic_read", "Read address-owned metadata and incoming/outgoing relations in Society.",
        input({{"address", address}}, {"address"}), {}, true, false};
    semanticRead.execute = [semantic](const QJsonObject& args, const a::ToolContext& context) {
        return semantic(args, context, [&](SemanticRepository& repository) {
            const auto address = args.value("address").toString();
            return QJsonObject{{"record", repository.read(address)}, {"relations", repository.relations(address)}};
        });
    };
    tools.append(std::move(semanticRead));
    a::Tool semanticRelations;
    semanticRelations.definition = {"semantic_relations", "Read incoming and outgoing relations even when a project target is unavailable.",
        input({{"address", address}}, {"address"}), {}, true, false};
    semanticRelations.execute = [semantic](const QJsonObject& args, const a::ToolContext& context) {
        return semantic(args, context, [&](SemanticRepository& repository) {
            return repository.relations(args.value("address").toString());
        });
    };
    tools.append(std::move(semanticRelations));
    a::Tool semanticMetadata;
    semanticMetadata.definition = {"semantic_set_metadata", "Replace explicitly user-declared metadata using its observed revision; actor is a host declaration.",
        input({{"address", address}, {"metadata", QJsonObject{{"type", "object"}}}, {"revision", revision}, {"actor", actor}},
          {"address", "metadata", "revision", "actor"}), {}, false, false, true};
    semanticMetadata.execute = [semantic](const QJsonObject& args, const a::ToolContext& context) {
        return semantic(args, context, [&](SemanticRepository& repository) {
            return repository.setMetadata(args.value("address").toString(), args.value("metadata").toObject(), args.value("revision").toString());
        });
    };
    tools.append(std::move(semanticMetadata));
    a::Tool semanticRelation;
    semanticRelation.definition = {"semantic_set_relation", "Record an explicit user-delegated relation owned by its source; not an inference proposal.",
        input({{"source", address}, {"predicate", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}},
           {"target", address}, {"metadata", QJsonObject{{"type", "object"}}}, {"revision", revision}, {"actor", actor}},
          {"source", "predicate", "target", "metadata", "revision", "actor"}), {}, false, false, true};
    semanticRelation.execute = [semantic](const QJsonObject& args, const a::ToolContext& context) {
        return semantic(args, context, [&](SemanticRepository& repository) {
            return repository.putRelation(args.value("source").toString(), args.value("predicate").toString(),
                args.value("target").toString(), args.value("metadata").toObject(), args.value("revision").toString());
        });
    };
    tools.append(std::move(semanticRelation));
    a::Tool semanticRemove;
    semanticRemove.definition = {"semantic_remove_relation", "Remove an address-owned relation using its observed revision.",
        input({{"source", address}, {"id", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}},
           {"revision", revision}, {"actor", actor}}, {"source", "id", "revision", "actor"}), {}, false, false, true};
    semanticRemove.execute = [semantic](const QJsonObject& args, const a::ToolContext& context) {
        return semantic(args, context, [&](SemanticRepository& repository) {
            return repository.removeRelation(args.value("source").toString(), args.value("id").toString(), args.value("revision").toString());
        });
    };
    tools.append(std::move(semanticRemove));
    a::Tool semanticRebind;
    semanticRebind.definition = {"semantic_rebind", "Record an explicitly user-asserted external move or same-address file update whose identity cannot be inferred.",
        input({{"source", address}, {"target", address}, {"actor", actor}}, {"source", "target", "actor"}), {}, false, false, true};
    semanticRebind.execute = [semantic](const QJsonObject& args, const a::ToolContext& context) {
        return semantic(args, context, [&](SemanticRepository& repository) {
            return repository.rebindRelocation(args.value("source").toString(), args.value("target").toString());
        });
    };
    tools.append(std::move(semanticRebind));
    return tools;
}
