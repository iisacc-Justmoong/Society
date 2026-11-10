#include "SocietyMcp.h"
#include "UiBridgeRegistration.h"
#include "Society/Drive/DriveController.h"
#include <iiMultimodalAgent/agent/ObjectTools.h>
#include <iiMultimodalAgent/agent/QuestionInbox.h>
#include <iiMultimodalAgent/agent/UserQuestions.h>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QPointer>
#include <utility>

namespace a = iiMultimodalAgent::agent;
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
// The window is a client of the daemon's authenticated request channel.
void installSocietyMcp(QObject* root, QObject* lifetime) {
    if (qEnvironmentVariable("IILOCALLLM_DISABLE_APP_MCP") == "1") return;
    auto* drive = root->findChild<DriveController*>("driveController");
    if (!drive) { qWarning() << "Society UI bridge: drive controller is unavailable"; return; }
    try {
        auto registry = std::make_shared<a::ToolRegistry>();
        auto bind = [&](QString name, QString description, QJsonObject schema, bool readOnly, auto handler) {
            a::ToolDefinition definition{name, description, schema, {}, readOnly, readOnly};
            return a::objectTool(drive, std::move(definition), [handler](QObject& object, const QJsonObject& args, const a::ToolContext& context) {
                return handler(static_cast<DriveController&>(object), args, context);
            });
        };
        registry->add(bind("status", "Read the Society window navigation state.", input(), true,
            [](auto& controller, const auto&, const auto&) { return a::ToolResult{"Society window state", status(controller)}; }));
        registry->add(bind("open_section", "Navigate Society to a section key returned by status.sections.",
            input({{"key", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}}, {"key"}), false,
            [](auto& controller, const auto& args, const auto&) {
                const bool ok=controller.openSection(args["key"].toString());
                return a::ToolResult{ok?"Society section opened":controller.errorString(),status(controller),!ok};
            }));
        registry->add(bind("navigate", "Navigate Society to an absolute directory inside its current container.",
            input({{"path", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}}}}, {"path"}), false,
            [](auto& controller, const auto& args, const auto&) {
                const bool ok=controller.navigate(args["path"].toString());
                return a::ToolResult{ok?"Society directory opened":controller.errorString(),status(controller),!ok};
            }));
        registry->add(bind("refresh", "Refresh Society's current container.", input(), false,
            [](auto& controller, const auto&, const auto&) {
                controller.refreshFromDisk(); return a::ToolResult{"Society refresh requested",status(controller)};
            }));
        auto policy=std::make_shared<a::RulePolicy>(a::PermissionMode::Default,
            QList<a::PermissionRule>{{"open_section",a::PermissionBehavior::Allow},
                {"navigate",a::PermissionBehavior::Allow},{"refresh",a::PermissionBehavior::Allow}});
        auto* questions=new a::QuestionInbox(a::PermissionRequestsOptions{},lifetime);
        registry->add(a::userQuestionTool({false,"markdown"}));
        a::ToolRunnerOptions options; options.hooks.append(questions->hook());
        auto runner=std::make_shared<a::ToolRunner>(registry,policy,std::move(options));
        auto* registration=new UiBridgeRegistration(std::move(runner),lifetime);
        registration->start();
        root->setProperty("agentQuestionInbox",QVariant::fromValue(questions));
        const auto close=[registration=QPointer<UiBridgeRegistration>(registration),questions=QPointer<a::QuestionInbox>(questions)] {
            if (registration) registration->stop();
            if (questions) questions->close();
        };
        QObject::connect(qApp,&QCoreApplication::aboutToQuit,lifetime,close);
        QObject::connect(drive,&QObject::destroyed,lifetime,close);
    } catch (const std::exception& error) { qWarning() << "Society UI bridge:" << error.what(); }
}
