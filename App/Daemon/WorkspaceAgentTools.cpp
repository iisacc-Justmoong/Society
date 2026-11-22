#include "WorkspaceAgentTools.h"
#include "Society/Mcp/FileJobTools.h"
#include "Society/Mcp/ResourceTools.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <utility>

namespace a = iiMultimodalAgent::agent;
namespace {
QJsonObject schema(QJsonObject properties, QJsonArray required = {}) {
    return {{"type","object"},{"properties",std::move(properties)},
            {"required",std::move(required)},{"additionalProperties",false}};
}
a::ToolResult commandResult(const SocietyBackendCommandReply &reply, const QString &label) {
    if (!reply.ok()) return {reply.error,{{"uncertain",reply.uncertain}},true};
    auto response=reply.response;
    response.remove("type"); response.remove("id");
    return {label,response};
}
}

std::shared_ptr<a::ToolRegistry> workspaceAgentTools(
    const QString &container, std::shared_ptr<const WorkspaceAgentToolTransport> transport)
{
    auto registry=std::make_shared<a::ToolRegistry>();
    const auto text=QJsonObject{{"type","string"},{"minLength",1},{"maxLength",4096}};
    a::Tool list;
    list.definition={"list_entries","List a page of a Society Files or Models directory; paths are relative to the container.",
        schema({{"path",text},{"offset",QJsonObject{{"type","integer"},{"minimum",0},{"maximum",2147483647}}},
                {"limit",QJsonObject{{"type","integer"},{"minimum",1},{"maximum",20}}}}),{},true,true};
    list.execute=[container,transport](const QJsonObject &args,const a::ToolContext &context) {
        context.cancellation.throwIfCancelled();
        if (!transport || !transport->directory) return a::ToolResult{"The Society directory service is unavailable.",{},true};
        const auto reply=transport->directory(container,args.value("path").toString("Files"),
            args.value("offset").toInt(0),args.value("limit").toInt(5));
        if (!reply.ok()) return commandResult(reply,{});
        auto result=reply.response; QJsonArray entries;
        for (const auto &entry:result.value("entries").toArray()) {
            const auto item=entry.toObject();
            entries.append(QJsonObject{{"name",item.value("name")},{"path",item.value("path")},
                {"directory",item.value("directory")},{"size",item.value("size")},{"modified",item.value("modified")}});
        }
        result.insert("entries",entries);
        if (QJsonDocument(result).toJson(QJsonDocument::Compact).size()>8192)
            return a::ToolResult{"Directory page is too large for the agent; request a smaller limit.",{},true};
        return commandResult({result,{},false},"Society directory page");
    };
    registry->add(std::move(list));
    a::Tool inspect;
    inspect.definition={"resource_inspect","Read the last observed identity, revision, locations, and replicas for a Society resource.",
        schema({{"path",text}},{"path"}),{},true,true};
    inspect.execute=[container,transport](const QJsonObject &args,const a::ToolContext &context) {
        context.cancellation.throwIfCancelled();
        if (!transport || !transport->command) return a::ToolResult{"The Society resource service is unavailable.",{},true};
        const auto reply=transport->command(container,"resourceInspect",args);
        if (!reply.ok()) return commandResult(reply,{});
        return a::ToolResult{"Society resource observation",reply.response.value("resource").toObject()};
    };
    registry->add(std::move(inspect));
    registry->add(societyDuplicateCandidatesTool([container,transport](const QString &operation,QJsonObject args,const a::ToolContext &context) {
        context.cancellation.throwIfCancelled();
        if (!transport || !transport->command) return SocietyBackendCommandReply{{},"The Society resource service is unavailable.",false};
        return transport->command(container,operation,args);
    }));
    for (auto &tool:societyFileJobTools([container,transport](const QString &operation,QJsonObject args,const a::ToolContext &context) {
        context.cancellation.throwIfCancelled();
        if (!transport || !transport->command) return SocietyBackendCommandReply{{},"The Society file service is unavailable.",false};
        return transport->command(container,operation,args);
    })) {
        if (tool.definition.name=="file_job_submit")
            tool.definition.description=QStringLiteral("Queue a Society file operation. Except for copy, path is relative to the container, "
                "for example Files/example.txt or Models/LLM/model.safetensors. For rename or create-directory, argument is the new name; "
                "for move, argument is the container-relative destination directory; for copy, path is the absolute external source "
                "and argument is the container-relative destination directory. Duplicate, trash, remove and restore use path. "
                "A queued acknowledgment is not completion; call file_job_status with the returned jobId.");
        registry->add(std::move(tool));
    }
    a::Tool import;
    import.definition={"model_import_submit","Queue safetensors model imports. Acceptance is not completion; use model_import_status to inspect progress.",
        schema({{"sources",QJsonObject{{"type","array"},{"minItems",1},{"maxItems",512},{"items",text}}}}, {"sources"}),{},false,false,true};
    import.execute=[container,transport](const QJsonObject &args,const a::ToolContext &context) {
        context.cancellation.throwIfCancelled();
        if (!transport || !transport->command) return a::ToolResult{"The Society import service is unavailable.",{},true};
        return commandResult(transport->command(container,"import",args),"Model import queued");
    };
    registry->add(std::move(import));
    a::Tool importStatus;
    importStatus.definition={"model_import_status","Inspect the daemon's current model import progress and recorded jobs.",schema({}),{},true,true};
    importStatus.execute=[container,transport](const QJsonObject &,const a::ToolContext &context) {
        context.cancellation.throwIfCancelled();
        if (!transport || !transport->command) return a::ToolResult{"The Society import service is unavailable.",{},true};
        const auto reply=transport->command(container,"status",{});
        if (!reply.ok()) return commandResult(reply,{});
        const auto state=reply.response.value("state").toObject();
        return a::ToolResult{"Model import status",{{"busy",state.value("busy")},{"progress",state.value("progress")},
            {"status",state.value("status")},{"error",state.value("errorString")},{"currentJobId",state.value("currentJobId")},
            {"jobs",state.value("jobs")},{"jobsError",state.value("jobsError")}}};
    };
    registry->add(std::move(importStatus));
    const auto windowTool=[container,transport](const QString &name,const QString &remote,const QString &description,
                                                QJsonObject input,bool readOnly) {
        a::Tool tool;
        tool.definition={name,description,std::move(input),{},readOnly,readOnly};
        tool.execute=[container,transport,remote](const QJsonObject &args,const a::ToolContext &context) {
            context.cancellation.throwIfCancelled();
            if (!transport || !transport->window) return a::ToolResult{"A verified Society window is unavailable.",{},true};
            auto pinned=args; pinned.insert("container",container);
            return transport->window(remote,pinned,context);
        };
        return tool;
    };
    registry->add(windowTool("ui_status","agent_status","Read the navigation state and sections of the Society window for this container.",schema({}),true));
    registry->add(windowTool("open_section","agent_open_section","Open a section using a key returned by ui_status.",
        schema({{"key",QJsonObject{{"type","string"},{"minLength",1},{"maxLength",128}}}},{"key"}),false));
    registry->add(windowTool("navigate","agent_navigate","Navigate the Society window to a directory path relative to this container.",
        schema({{"path",text}},{"path"}),false));
    registry->add(windowTool("refresh","agent_refresh","Refresh the connected Society window.",schema({}),false));
    return registry;
}

a::PermissionResponse workspaceAgentPermission(const QString &container,
    const std::shared_ptr<const WorkspaceAgentToolTransport> &transport,
    const a::ToolCall &call,const a::PermissionDecision &,const a::ToolContext &context)
{
    const a::PermissionResponse denied{a::PermissionBehavior::Deny,"The requested action was not approved."};
    if (!transport || !transport->question || container.isEmpty()) return denied;
    context.cancellation.throwIfCancelled();
    const auto arguments=QString::fromUtf8(QJsonDocument(call.arguments).toJson(QJsonDocument::Indented));
    const auto question=QStringLiteral("Allow this one Society action?\n\nContainer: %1\nTool: %2\nArguments:\n%3")
        .arg(container,call.name,arguments);
    // Never ask for approval of a shortened or otherwise undisplayable action.
    // The existing question sheet has a bounded viewport and no scrolling.
    // Refuse large previews instead of placing approval controls outside it.
    if (question.size()>512 || question.toUtf8().size()>1536) return denied;
    const QJsonObject input{{"questions",QJsonArray{QJsonObject{{"question",question},{"header","Action"},
        {"options",QJsonArray{QJsonObject{{"label","Deny"},{"description","Do not perform this action."}},
                              QJsonObject{{"label","Allow once"},{"description","Perform only this reviewed action."}}}},
        {"multiSelect",false}}}}};
    const auto result=transport->question(input,context);
    context.cancellation.throwIfCancelled();
    if (result.isError || result.data.value("answers").toObject().value(question).toString()!=QStringLiteral("Allow once")) return denied;
    return {a::PermissionBehavior::Allow,"Approved for this action only."};
}
