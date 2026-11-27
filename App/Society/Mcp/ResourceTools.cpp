#include "ResourceTools.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <exception>
#include <utility>

namespace a = iiMultimodalAgent::agent;
namespace {
// Keep transport errors, cancellation and the agent's response bound identical
// for each bounded resource response exposed by this adapter.
a::ToolResult resourcePage(const SocietyResourceInvoker &invoke,const QString &operation,const QString &key,
                           const QString &label,const QJsonObject &args,const a::ToolContext &context)
{
    try {
        context.cancellation.throwIfCancelled();
        const auto reply=invoke(operation,args,context);
        if (!reply.ok()) return {reply.error,{{"uncertain",reply.uncertain}},true};
        const auto page=reply.response.value(key).toObject();
        if (QJsonDocument(page).toJson(QJsonDocument::Compact).size()>8192)
            return {"Resource page is too large for the agent; request a smaller limit, or use the CLI for very long paths.",{},true};
        return {label,page};
    } catch (const std::exception &error) { return {QString::fromUtf8(error.what()),{},true}; }
}
}

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
        return resourcePage(invoke,"resourceDuplicates","duplicates","Last-observed duplicate candidates",args,context);
    };
    return tool;
}

a::Tool societyCompareBytesTool(SocietyResourceInvoker invoke)
{
    a::Tool tool;
    const auto path=QJsonObject{{"type","string"},{"minLength",1},{"maxLength",4096}};
    tool.definition={"resource_compare_bytes",
        "Compare separate temporary snapshots of two observed Files or Models resources, at most 64 MiB each. "
        "The captures are not atomic together and equality does not authorize deletion or deduplication.",
        {{"type","object"},{"properties",QJsonObject{{"path",path},{"otherPath",path}}},
         {"required",QJsonArray{"path","otherPath"}},{"additionalProperties",false}}, {},true,true};
    tool.execute=[invoke=std::move(invoke)](const QJsonObject &args,const a::ToolContext &context) {
        return resourcePage(invoke,"resourceCompareBytes","byteComparison","Snapshot byte comparison",args,context);
    };
    return tool;
}

a::Tool societyContentIndexTool(SocietyResourceInvoker invoke)
{
    a::Tool tool;
    const auto prefix=QJsonObject{{"type","string"},{"maxLength",256}};
    tool.definition={"resource_content_index",
        "Legacy endpoint: file-content embedding is disabled under the hash-and-metadata-only indexing policy.",
        {{"type","object"},{"properties",QJsonObject{
            {"path",QJsonObject{{"type","string"},{"minLength",1},{"maxLength",4096}}},
            {"model",QJsonObject{{"type","string"},{"minLength",1},{"maxLength",1024}}},
            {"documentPrefix",prefix},{"queryPrefix",prefix}}},
         {"required",QJsonArray{"path","model"}},{"additionalProperties",false}}, {},false,false,true};
    tool.execute=[invoke=std::move(invoke)](const QJsonObject &args,const a::ToolContext &context) {
        return resourcePage(invoke,"resourceContentIndex","content","Text revision indexed",args,context);
    };
    return tool;
}

a::Tool societyContentSearchTool(SocietyResourceInvoker invoke)
{
    a::Tool tool;
    const auto prefix=QJsonObject{{"type","string"},{"maxLength",256}};
    tool.definition={"resource_content_search",
        "Legacy endpoint: searching file-content embeddings is disabled under the hash-and-metadata-only indexing policy.",
        {{"type","object"},{"properties",QJsonObject{
            {"query",QJsonObject{{"type","string"},{"minLength",1},{"maxLength",4096}}},
            {"model",QJsonObject{{"type","string"},{"minLength",1},{"maxLength",1024}}},
            {"documentPrefix",prefix},{"queryPrefix",prefix},
            {"limit",QJsonObject{{"type","integer"},{"minimum",1},{"maximum",20}}}}},
         {"required",QJsonArray{"query","model"}},{"additionalProperties",false}}, {},true,false};
    tool.execute=[invoke=std::move(invoke)](const QJsonObject &args,const a::ToolContext &context) {
        return resourcePage(invoke,"resourceContentSearch","content","Semantic text matches",args,context);
    };
    return tool;
}

a::Tool societyContentPolicyTool(SocietyResourceInvoker invoke)
{
    a::Tool tool;
    tool.definition={"resource_content_policy",
        "Report that file-content embedding is disabled and whether a legacy profile remains configured.",
        {{"type","object"},{"properties",QJsonObject{}},{"additionalProperties",false}}, {},true,false};
    tool.execute=[invoke=std::move(invoke)](const QJsonObject &args,const a::ToolContext &context) {
        return resourcePage(invoke,"resourceContentPolicyGet","policy","Automatic text indexing policy",args,context);
    };
    return tool;
}

a::Tool societySetContentPolicyTool(SocietyResourceInvoker invoke)
{
    a::Tool tool;
    const auto prefix=QJsonObject{{"type","string"},{"maxLength",256}};
    tool.definition={"resource_content_policy_set",
        "File-content embedding cannot be enabled. A disable request reports the effective policy without deleting legacy data.",
        {{"type","object"},{"properties",QJsonObject{
            {"enabled",QJsonObject{{"type","boolean"}}},
            {"model",QJsonObject{{"type","string"},{"minLength",1},{"maxLength",1024}}},
            {"documentPrefix",prefix},{"queryPrefix",prefix}}},
         {"required",QJsonArray{"enabled"}},{"additionalProperties",false}}, {},false,false,true};
    tool.execute=[invoke=std::move(invoke)](const QJsonObject &args,const a::ToolContext &context) {
        return resourcePage(invoke,"resourceContentPolicySet","policy","Automatic text indexing policy updated",args,context);
    };
    return tool;
}

a::Tool societyIndexedSearchTool(SocietyResourceInvoker invoke)
{
    a::Tool tool;
    const auto term=QJsonObject{{"type","string"},{"minLength",1},{"maxLength",128}};
    const auto terms=QJsonObject{{"type","array"},{"maxItems",16},{"items",term}};
    const auto query=QJsonObject{{"type","object"},{"properties",QJsonObject{
        {"section",QJsonObject{{"type","string"},{"enum",QJsonArray{"files","models"}}}},
        {"text",QJsonObject{{"type","string"},{"maxLength",512}}},
        {"keywords",terms},{"collections",terms},
        {"minimumRating",QJsonObject{{"type","integer"},{"minimum",0},{"maximum",5}}},
        {"directory",QJsonObject{{"type","string"},{"minLength",1},{"maxLength",1024}}}}},
        {"additionalProperties",false}};
    tool.definition={"resource_search",
        "Search indexed file metadata by section, text, keywords, collections or minimum rating, "
        "or use a saved virtual directory alone. This is not content embedding search; coverage is unknown.",
        {{"type","object"},{"properties",QJsonObject{
            {"query",query},
            {"limit",QJsonObject{{"type","integer"},{"minimum",1},{"maximum",20}}},
            {"cursor",QJsonObject{{"type","object"}}}}},
         {"required",QJsonArray{"query"}},{"additionalProperties",false}}, {},true,true};
    tool.execute=[invoke=std::move(invoke)](const QJsonObject &args,const a::ToolContext &context) {
        return resourcePage(invoke,"resourceSearch","search","Indexed resource search",args,context);
    };
    return tool;
}

a::Tool societyLineageTool(SocietyResourceInvoker invoke)
{
    a::Tool tool;
    tool.definition={"resource_lineage",
        "Trace recorded user-asserted relations from a Files or Models item. Coverage is unknown; "
        "results do not verify dependencies or authorize cleanup.",
        {{"type","object"},{"properties",QJsonObject{
            {"path",QJsonObject{{"type","string"},{"minLength",1},{"maxLength",4096}}},
            {"direction",QJsonObject{{"type","string"},{"enum",QJsonArray{"incoming","outgoing","both"}}}},
            {"depth",QJsonObject{{"type","integer"},{"minimum",1},{"maximum",3}}},
            {"limit",QJsonObject{{"type","integer"},{"minimum",1},{"maximum",20}}}}},
         {"required",QJsonArray{"path"}},{"additionalProperties",false}}, {},true,true};
    tool.execute=[invoke=std::move(invoke)](const QJsonObject &args,const a::ToolContext &context) {
        return resourcePage(invoke,"resourceLineage","lineage","Recorded resource relations",args,context);
    };
    return tool;
}

a::Tool societyCleanupPlanTool(SocietyResourceInvoker invoke)
{
    a::Tool tool;
    tool.definition={"resource_cleanup_plan",
        "Review recorded relations and policy blockers for a Files or Models item. "
        "The plan is not executable and cannot approve or authorize deletion.",
        {{"type","object"},{"properties",QJsonObject{
            {"path",QJsonObject{{"type","string"},{"minLength",1},{"maxLength",4096}}},
            {"limit",QJsonObject{{"type","integer"},{"minimum",1},{"maximum",20}}}}},
         {"required",QJsonArray{"path"}},{"additionalProperties",false}}, {},true,true};
    tool.execute=[invoke=std::move(invoke)](const QJsonObject &args,const a::ToolContext &context) {
        return resourcePage(invoke,"resourceCleanupPlan","cleanupPlan","Cleanup assessment",args,context);
    };
    return tool;
}
