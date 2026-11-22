#include "ResourceTools.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <exception>
#include <utility>

namespace a = iiMultimodalAgent::agent;

a::Tool societyDuplicateCandidatesTool(SocietyResourceInvoker invoke)
{
    a::Tool tool;
    tool.definition={"resource_duplicate_candidates",
        "Find a page of indexed Files or Models with the same last-observed SHA-256 as a reference. "
        "Coverage is unknown; candidates are not verified current bytes or deletion recommendations.",
        {{"type","object"},{"properties",QJsonObject{
            {"path",QJsonObject{{"type","string"},{"minLength",1},{"maxLength",4096}}},
            {"limit",QJsonObject{{"type","integer"},{"minimum",1},{"maximum",20}}},
            {"cursor",QJsonObject{{"type","object"}}}}},
         {"required",QJsonArray{"path"}},{"additionalProperties",false}}, {},true,true};
    tool.execute=[invoke=std::move(invoke)](const QJsonObject &args,const a::ToolContext &context) {
        try {
            context.cancellation.throwIfCancelled();
            const auto reply=invoke("resourceDuplicates",args,context);
            if (!reply.ok()) return a::ToolResult{reply.error,{{"uncertain",reply.uncertain}},true};
            const auto page=reply.response.value("duplicates").toObject();
            if (QJsonDocument(page).toJson(QJsonDocument::Compact).size()>8192)
                return a::ToolResult{"Candidate page is too large for the agent; request a smaller limit, or use the CLI for very long paths.",{},true};
            return a::ToolResult{"Last-observed duplicate candidates",page};
        } catch (const std::exception &error) { return a::ToolResult{QString::fromUtf8(error.what()),{},true}; }
    };
    return tool;
}
