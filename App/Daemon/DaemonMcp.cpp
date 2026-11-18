#include "DaemonMcp.h"
#include "UiToolBridge.h"
#include "Shared/BackendCommand.h"
#include "Society/Mcp/FileJobTools.h"
#include "Society/Mcp/SemanticTools.h"
#include <iiMultimodalAgent>
#include <QDir>
#include <QJsonArray>
#include <utility>

namespace a=iiMultimodalAgent::agent;
namespace m=iiMultimodalAgent::mcp;

DaemonMcp::DaemonMcp(QString brokerDirectory,QString registrationDirectory)
    : m_brokerDirectory(std::move(brokerDirectory)),m_registrationDirectory(std::move(registrationDirectory)) {}
DaemonMcp::~DaemonMcp() { stop(); }

bool DaemonMcp::start()
{
    if (m_server) return true;
    m_error.clear();
    if (!QDir::isAbsolutePath(m_brokerDirectory)
        || (!m_registrationDirectory.isEmpty() && !QDir::isAbsolutePath(m_registrationDirectory))) {
        m_error=QStringLiteral("The Society backend or registration directory is invalid."); return false;
    }
    try {
        auto registry=std::make_shared<a::ToolRegistry>();
        const auto broker=m_brokerDirectory;
        a::Tool state;
        state.definition={"status","Read the Society backend and, when connected, window navigation state.",
            {{"type","object"},{"properties",QJsonObject{}},{"additionalProperties",false}}, {}, true, true};
        state.execute=[broker](const QJsonObject &,const a::ToolContext &context) {
            context.cancellation.throwIfCancelled();
            const auto result=societyBackendCommand(broker,"status");
            if (!result.ok()) return a::ToolResult{result.error,{{"uncertain",result.uncertain}},true};
            auto data=result.response.value("state").toObject();
            const auto backend=data; const auto ui=societyUiToolCall(broker,QStringLiteral("status"),{},context,1500);
            if (!ui.isError) for (auto it=ui.data.constBegin();it!=ui.data.constEnd();++it) data.insert(it.key(),it.value());
            data.insert("backend",backend); data.insert("ui_status_available",!ui.isError);
            return a::ToolResult{QStringLiteral("Society state"),data};
        };
        registry->add(std::move(state));
        const auto agentTool=[&](const QString &name,const QString &operation,const QString &description,bool takesPrompt,bool readOnly) {
            a::Tool tool;
            QJsonObject properties;
            if (takesPrompt) {
                properties.insert("prompt",QJsonObject{{"type","string"},{"minLength",1},{"maxLength",16384}});
                properties.insert("attachments",QJsonObject{{"type","array"},{"maxItems",8},
                    {"items",QJsonObject{{"type","string"},{"minLength",1},{"maxLength",4096}}}});
            }
            QJsonObject schema{{"type","object"},{"properties",properties},{"additionalProperties",false}};
            if (takesPrompt) schema.insert("required",QJsonArray{"prompt"});
            tool.definition={name,description,schema,{},readOnly,readOnly};
            tool.execute=[broker,operation,takesPrompt](const QJsonObject &arguments,const a::ToolContext &context) {
                context.cancellation.throwIfCancelled();
                QJsonObject payload;
                if (takesPrompt) {
                    payload.insert("prompt",arguments.value("prompt"));
                    if (arguments.contains("attachments")) payload.insert("attachments",arguments.value("attachments"));
                }
                const auto reply=societyBackendCommand(broker,operation,payload);
                if (!reply.ok()) return a::ToolResult{reply.error,{{"uncertain",reply.uncertain}},true};
                return a::ToolResult{QStringLiteral("Society agent state"),reply.response.value("agent").toObject()};
            };
            return tool;
        };
        registry->add(agentTool("workspace_agent_status","agentSubscribe","Inspect the current Society workspace conversation.",false,true));
        registry->add(agentTool("workspace_agent_send","agentSend","Submit a message and optional Society Files attachments to the workspace agent.",true,false));
        registry->add(agentTool("workspace_agent_new","agentNew","Start a new Society workspace conversation.",false,false));
        registry->add(agentTool("workspace_agent_cancel","agentCancel","Cancel the current Society workspace response.",false,false));
        a::Tool selectAgentModel;
        selectAgentModel.definition={"workspace_agent_select","Set the default model preference for this workspace conversation; routing remains automatic.",
            {{"type","object"},{"properties",QJsonObject{{"model",QJsonObject{{"type","string"},{"minLength",1},{"maxLength",136}}}}},
             {"required",QJsonArray{"model"}},{"additionalProperties",false}}, {},false,false};
        selectAgentModel.execute=[broker](const QJsonObject &arguments,const a::ToolContext &context) {
            context.cancellation.throwIfCancelled();
            const auto reply=societyBackendCommand(broker,"agentSelect",{{"model",arguments.value("model")}});
            if (!reply.ok()) return a::ToolResult{reply.error,{{"uncertain",reply.uncertain}},true};
            return a::ToolResult{QStringLiteral("Society agent state"),reply.response.value("agent").toObject()};
        };
        registry->add(std::move(selectAgentModel));
        const auto suggestedTool=[&](const QString &name,const QString &operation,const QString &description,bool readOnly) {
            a::Tool tool;
            tool.definition={name,description,{{"type","object"},{"properties",QJsonObject{}},{"additionalProperties",false}}, {},readOnly,readOnly};
            tool.execute=[broker,operation](const QJsonObject &,const a::ToolContext &context) {
                context.cancellation.throwIfCancelled();
                const auto reply=societyBackendCommand(broker,operation);
                if (!reply.ok()) return a::ToolResult{reply.error,{{"uncertain",reply.uncertain}},true};
                return a::ToolResult{QStringLiteral("Suggested unfinished work"),reply.response.value("suggested").toObject()};
            };
            return tool;
        };
        // The first subscription may schedule indexing and model preparation.
        registry->add(suggestedTool("suggested_status","suggestedSubscribe","Read the current unfinished-work recommendations and their provenance.",false));
        registry->add(suggestedTool("suggested_refresh","suggestedRefresh","Schedule a fresh local unfinished-work assessment.",false));
        const auto recentTool=[&](const QString &name,const QString &operation,const QString &description) {
            a::Tool tool;
            tool.definition={name,description,{{"type","object"},{"properties",QJsonObject{}},{"additionalProperties",false}}, {},false,false};
            tool.execute=[broker,operation](const QJsonObject &,const a::ToolContext &context) {
                context.cancellation.throwIfCancelled();
                const auto reply=societyBackendCommand(broker,operation);
                if (!reply.ok()) return a::ToolResult{reply.error,{{"uncertain",reply.uncertain}},true};
                return a::ToolResult{QStringLiteral("Recent files"),reply.response.value("recent").toObject()};
            };
            return tool;
        };
        registry->add(recentTool("recent_status","recentSubscribe","Read up to 500 recent files ordered by observed activity."));
        registry->add(recentTool("recent_refresh","recentRefresh","Refresh the background Recent snapshot."));
        a::Tool resourceInspect;
        resourceInspect.definition={"resource_inspect","Read the last-observed resource, revision, locations, and declared replicas for a Society Files or Models item.",
            {{"type","object"},{"properties",QJsonObject{{"path",QJsonObject{{"type","string"},{"minLength",1},{"maxLength",4096}}}}},
             {"required",QJsonArray{"path"}},{"additionalProperties",false}}, {},true,false};
        resourceInspect.execute=[broker](const QJsonObject &args,const a::ToolContext &context) {
            context.cancellation.throwIfCancelled();
            const auto reply=societyBackendCommand(broker,"resourceInspect",{{"path",args.value("path")}},QString(),30000);
            if (!reply.ok()) return a::ToolResult{reply.error,{{"uncertain",reply.uncertain}},true};
            return a::ToolResult{QStringLiteral("Society resource observation"),reply.response.value("resource").toObject()};
        };
        registry->add(std::move(resourceInspect));
        const auto uiTool=[&](const QString &name,const QString &remote,const QString &description,QJsonObject schema,bool readOnly) {
            a::Tool tool;
            tool.definition={name,description,std::move(schema),{},readOnly,readOnly};
            tool.execute=[broker,remote](const QJsonObject &arguments,const a::ToolContext &context) {
                return societyUiToolCall(broker,remote,arguments,context);
            };
            return tool;
        };
        const QJsonObject emptySchema{{"type","object"},{"properties",QJsonObject{}},{"additionalProperties",false}};
        const auto uiSchema=[](const QString &key,int maximum) {
            return QJsonObject{{"type","object"},{"properties",QJsonObject{{key,QJsonObject{{"type","string"},
                {"minLength",1},{"maxLength",maximum}}}}},{"required",QJsonArray{key}},{"additionalProperties",false}};
        };
        registry->add(uiTool("ui_status","status","Read the connected Society window's navigation state.",emptySchema,true));
        registry->add(uiTool("open_section","open_section","Open a section in the connected Society window.",uiSchema("key",128),false));
        registry->add(uiTool("navigate","navigate","Navigate the connected Society window to a directory.",uiSchema("path",4096),false));
        registry->add(uiTool("refresh","refresh","Refresh the connected Society window.",emptySchema,false));
        auto question=a::userQuestionTool({false,"markdown"});
        question.requiresPermission=false;
        question.prepare={};
        question.execute=[broker](const QJsonObject &arguments,const a::ToolContext &context) {
            return societyUiToolCall(broker,QStringLiteral("AskUserQuestion"),arguments,context);
        };
        registry->add(std::move(question));
        a::Tool list;
        list.definition={"list_entries","List a Society directory through the container index.",
            {{"type","object"},{"properties",QJsonObject{
                {"path",QJsonObject{{"type","string"},{"minLength",1},{"maxLength",4096}}},
                {"offset",QJsonObject{{"type","integer"},{"minimum",0},{"maximum",2147483647}}},
                {"limit",QJsonObject{{"type","integer"},{"minimum",1},{"maximum",200}}}}},
             {"additionalProperties",false}}, {}, true, true};
        list.execute=[broker](const QJsonObject &args,const a::ToolContext &context) {
            context.cancellation.throwIfCancelled();
            const auto offset=args.value("offset").toInt(0), limit=args.value("limit").toInt(100);
            auto path=args.value("path").toString("Files"); QString root; bool legacy=false;
            if (!args.contains("path")) {
                const auto ui=societyUiToolCall(broker,QStringLiteral("status"),{},context,1500);
                if (!ui.isError) {
                    const auto selected=societyBackendCommand(broker,"status");
                    root=QDir::cleanPath(ui.data.value("root_path").toString());
                    const auto current=QDir::cleanPath(ui.data.value("current_path").toString());
                    const auto selectedRoot=QDir::cleanPath(selected.response.value("state").toObject().value("selectedContainer").toString());
                    if (!selected.ok() || !QDir::isAbsolutePath(root) || root!=selectedRoot || !QDir::isAbsolutePath(current)
                        || (current!=root && !current.startsWith(root+'/')))
                        return a::ToolResult{QStringLiteral("The Society window and selected drive changed; try again."),{},true};
                    legacy=true;
                    if (current==root) {
                        QJsonArray entries; const auto sections=ui.data.value("sections").toArray();
                        for (int i=offset;i<sections.size() && entries.size()<limit;++i) {
                            const auto name=sections[i].toObject().value("name").toString();
                            if (name.isEmpty() || name.contains('/') || name=="." || name=="..") continue;
                            entries.append(QJsonObject{{"name",name},{"path",QDir(root).filePath(name)},{"kind","directory"}});
                        }
                        return a::ToolResult{QStringLiteral("Society directory entries"),{{"path",root},{"entries",entries},
                            {"truncated",offset+entries.size()<sections.size()},{"hasMore",offset+entries.size()<sections.size()}}};
                    }
                    path=QDir(root).relativeFilePath(current);
                }
            }
            auto result=societyBackendDirectoryPage(broker,path,offset,limit);
            if (!result.ok()) return a::ToolResult{result.error,{},true};
            result.response.remove("type"); result.response.remove("id"); result.response.remove("accepted");
            auto entries=result.response.value("entries").toArray();
            for (int i=0;i<entries.size();++i) {
                auto item=entries[i].toObject();
                item.insert("kind",item.value("directory").toBool()?QStringLiteral("directory"):QStringLiteral("file"));
                if (legacy) item.insert("path",QDir(root).filePath(item.value("path").toString()));
                entries[i]=item;
            }
            result.response.insert("entries",entries);
            result.response.insert("truncated",result.response.value("hasMore"));
            if (legacy) result.response.insert("path",QDir(root).filePath(path));
            return a::ToolResult{QStringLiteral("Society directory page"),result.response};
        };
        registry->add(std::move(list));
        a::Tool import;
        import.definition={"model_import_submit",
            "Submit safetensors models to the Society daemon. Acceptance is not completion; inspect status using the job ID.",
            {{"type","object"},{"properties",QJsonObject{{"sources",QJsonObject{{"type","array"},
                {"minItems",1},{"maxItems",512},{"items",QJsonObject{{"type","string"},{"minLength",1},{"maxLength",4096}}}}}}},
             {"required",QJsonArray{"sources"}},{"additionalProperties",false}}, {}, false, false, true};
        import.execute=[broker](const QJsonObject &args,const a::ToolContext &context) {
            context.cancellation.throwIfCancelled();
            const auto result=societyBackendCommand(broker,"import",args);
            if (!result.ok()) return a::ToolResult{result.error,{{"uncertain",result.uncertain}},true};
            return a::ToolResult{QStringLiteral("Model import queued"),result.response};
        };
        registry->add(std::move(import));
        for (auto &tool:societyFileJobTools([broker](const QString &operation,QJsonObject args,const a::ToolContext &context) {
            context.cancellation.throwIfCancelled();
            return societyBackendCommand(broker,operation,std::move(args));
        })) registry->add(std::move(tool));
        for (auto &tool:societySemanticTools([broker](const a::ToolContext &context) {
            context.cancellation.throwIfCancelled();
            const auto result=societyBackendCommand(broker,"status");
            if (!result.ok()) throw std::runtime_error(result.error.toStdString());
            return result.response.value("state").toObject().value("selectedContainer").toString();
        })) registry->add(std::move(tool));
        auto policy=std::make_shared<a::RulePolicy>(a::PermissionMode::Default,
            QList<a::PermissionRule>{{"file_job_submit",a::PermissionBehavior::Allow}, {"model_import_submit",a::PermissionBehavior::Allow},
                {"open_section",a::PermissionBehavior::Allow}, {"navigate",a::PermissionBehavior::Allow},
                {"refresh",a::PermissionBehavior::Allow}, {"AskUserQuestion",a::PermissionBehavior::Allow},
                {"semantic_set_metadata",a::PermissionBehavior::Allow}, {"semantic_set_relation",a::PermissionBehavior::Allow},
                {"semantic_remove_relation",a::PermissionBehavior::Allow}, {"semantic_rebind",a::PermissionBehavior::Allow},
                {"workspace_agent_send",a::PermissionBehavior::Allow}, {"workspace_agent_new",a::PermissionBehavior::Allow},
                {"workspace_agent_cancel",a::PermissionBehavior::Allow}, {"workspace_agent_select",a::PermissionBehavior::Allow},
                {"suggested_status",a::PermissionBehavior::Allow}, {"suggested_refresh",a::PermissionBehavior::Allow},
                {"recent_status",a::PermissionBehavior::Allow}, {"recent_refresh",a::PermissionBehavior::Allow},
                {"resource_inspect",a::PermissionBehavior::Allow}});
        a::McpServerOptions bridge; bridge.workingDirectory=QDir::currentPath(); bridge.appId="com.iisacc.society";
        auto protocol=a::mcpServerOptions(registry,policy,std::move(bridge));
        protocol.experimentalCapabilities["iisacc/userQuestions"]=QJsonObject{
            {"schema","iisacc.user-question/1"},{"tool","AskUserQuestion"},{"responseChannel","local-ui"},
            {"permissionRequests",false},{"previewFormat","markdown"},{"previewRendering","plain-text"}};
        m::LocalApplicationServerOptions options; options.directory=m_registrationDirectory;
        auto server=std::make_unique<m::LocalApplicationServer>(
            m::LocalApplicationIdentity{"com.iisacc.society","Society",SOCIETY_APP_VERSION},
            std::move(protocol),std::move(options));
        if (!server->listen()) { m_error=server->errorString(); return false; }
        m_server=std::move(server); return true;
    } catch (const std::exception &error) { m_error=QString::fromUtf8(error.what()); return false; }
}
void DaemonMcp::stop()
{
    if (!m_server) return;
    m_server->close(); m_server.reset();
}
