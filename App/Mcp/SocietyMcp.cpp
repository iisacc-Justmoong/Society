#include "SocietyMcp.h"
#include "App/Drive/DriveController.h"
#include <agent/ObjectTools.h>
#include <agent/McpServer.h>
#include <mcp/LocalApplications.h>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QDebug>

namespace a = iiLocalLLM::agent;
namespace m = iiLocalLLM::mcp;
namespace {
QJsonObject input(QJsonObject properties = {}, QJsonArray required = {}) {
    return {{"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false}};
}
QJsonObject status(DriveController& drive) {
    return {{"has_drive", drive.hasDrive()}, {"root_path", drive.rootPath()}, {"current_path", drive.currentPath()},
        {"identifier", drive.identifier()}, {"at_root", drive.atRoot()}, {"busy", drive.busy()},
        {"contents_available", drive.contentsAvailable()}, {"mirror_pending", drive.mirrorPending()},
        {"sections", QJsonArray::fromVariantList(drive.sections())}, {"error", drive.errorString()}};
}
}
void installSocietyMcp(QObject* root, QObject* lifetime) {
    if (qEnvironmentVariable("IILOCALLLM_DISABLE_APP_MCP") == "1") return;
    auto* drive = root->findChild<DriveController*>("driveController");
    if (!drive) { qWarning() << "Society MCP: drive controller is unavailable"; return; }
    try {
        auto registry = std::make_shared<a::ToolRegistry>();
        auto bind = [&](QString name, QString description, QJsonObject schema, bool readOnly, auto handler) {
            a::ToolDefinition definition{name, description, schema, {}, readOnly, readOnly};
            return a::objectTool(drive, std::move(definition), [handler](QObject& object, const QJsonObject& args, const a::ToolContext& context) {
                return handler(static_cast<DriveController&>(object), args, context);
            });
        };
        auto state = bind("status", "Read Society's open container, navigation and synchronization availability.", input(), true,
            [](auto& controller, const auto&, const auto&) { return a::ToolResult{"Society state", status(controller)}; });
        registry->add(state);
        registry->add(bind("open_section", "Navigate Society to a section key returned by status.sections.",
            input({{"key", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}}, {"key"}), false,
            [](auto& controller, const auto& args, const auto&) {
                const bool ok = controller.openSection(args["key"].toString());
                return a::ToolResult{ok ? "Society section opened" : controller.errorString(), status(controller), !ok};
            }));
        registry->add(bind("navigate", "Navigate Society to an absolute directory inside its currently open container.",
            input({{"path", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}}}}, {"path"}), false,
            [](auto& controller, const auto& args, const auto&) {
                const bool ok = controller.navigate(args["path"].toString());
                return a::ToolResult{ok ? "Society directory opened" : controller.errorString(), status(controller), !ok};
            }));
        registry->add(bind("refresh", "Refresh Society's current container from local storage.", input(), false,
            [](auto& controller, const auto&, const auto&) {
                controller.refreshFromDisk(); return a::ToolResult{"Society refresh requested", status(controller)};
            }));
        // Snapshot navigation on the UI thread; directory enumeration stays on the MCP worker.
        a::Tool list;
        list.definition = {"list_entries", "List up to 200 entries in Society's current directory. Does not read file contents.",
            input({{"limit", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 200}}}}), {}, true, true};
        list.execute = [state](const QJsonObject& args, const a::ToolContext& context) {
            const auto snapshot = state.execute({}, context);
            if (snapshot.isError) return snapshot;
            if (!snapshot.data["contents_available"].toBool())
                return a::ToolResult{"Society's current container is not available", {}, true};
            const auto rootPath = snapshot.data["root_path"].toString();
            const auto path = snapshot.data["at_root"].toBool() ? rootPath : snapshot.data["current_path"].toString();
            const auto canonical = QFileInfo(path).canonicalFilePath();
            if (canonical.isEmpty() || (canonical != rootPath && !canonical.startsWith(rootPath + '/')))
                return a::ToolResult{"Society's directory changed or left the current container", {}, true};
            QDirIterator iterator(canonical, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::NoIteratorFlags);
            QJsonArray entries; const auto limit = args["limit"].toInt(100);
            while (iterator.hasNext() && entries.size() < limit) {
                context.cancellation.throwIfCancelled(); iterator.next(); const auto entry = iterator.fileInfo();
                entries.append(QJsonObject{{"name", entry.fileName()}, {"path", entry.absoluteFilePath()},
                    {"kind", entry.isSymLink() ? "link" : entry.isDir() ? "directory" : "file"}});
            }
            return a::ToolResult{"Society directory entries", {{"path", canonical}, {"entries", entries}, {"truncated", iterator.hasNext()}}};
        };
        registry->add(std::move(list));
        auto policy = std::make_shared<a::RulePolicy>(a::PermissionMode::DontAsk,
            QList<a::PermissionRule>{{"open_section", a::PermissionBehavior::Allow},
                {"navigate", a::PermissionBehavior::Allow}, {"refresh", a::PermissionBehavior::Allow}});
        a::McpServerOptions bridge; bridge.workingDirectory = QDir::currentPath(); bridge.appId = "com.iisacc.society";
        auto server = std::make_shared<m::LocalApplicationServer>(
            m::LocalApplicationIdentity{"com.iisacc.society", "Society", SOCIETY_APP_VERSION},
            a::mcpServerOptions(registry, policy, std::move(bridge)));
        if (!server->listen()) { qWarning() << "Society MCP:" << server->errorString(); return; }
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, lifetime, [server] { server->close(); });
        QObject::connect(drive, &QObject::destroyed, lifetime, [server] { server->close(); });
    } catch (const std::exception& error) { qWarning() << "Society MCP:" << error.what(); }
}
