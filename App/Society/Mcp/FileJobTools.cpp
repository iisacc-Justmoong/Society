#include "FileJobTools.h"
#include <QJsonArray>
#include <utility>

namespace a = iiMultimodalAgent::agent;
namespace {
QJsonObject schema(QJsonObject properties,QJsonArray required)
{
    return {{"type","object"},{"properties",std::move(properties)},
            {"required",std::move(required)},{"additionalProperties",false}};
}
a::ToolResult invokeJob(const SocietyFileJobInvoker &invoke,const QString &operation,
                        QJsonObject args,const a::ToolContext &context)
{
    try {
        context.cancellation.throwIfCancelled();
        const auto result=invoke(operation,std::move(args),context);
        auto data=result.response;
        if (!result.ok()) {
            data.insert("accepted",false); data.insert("uncertain",result.uncertain);
            return {result.error,data,true};
        }
        return {operation=="fileOperation"?QStringLiteral("File job queued"):QStringLiteral("File job status"),data};
    } catch (const std::exception &error) { return {QString::fromUtf8(error.what()),{},true}; }
}
}
QList<a::Tool> societyFileJobTools(SocietyFileJobInvoker invoke)
{
    const auto text=QJsonObject{{"type","string"},{"minLength",1},{"maxLength",4096}};
    a::Tool submit;
    submit.definition={"file_job_submit",
        "Submit a Society container file operation. A queued acknowledgement is not completion; inspect its job ID.",
        schema({{"action",QJsonObject{{"type","string"},{"enum",QJsonArray{"duplicate","rename","trash","remove","restore","move","create-directory","copy"}}}},
                {"path",text},{"argument",text}}, {"action","path"}),{},false,false,true};
    submit.execute=[invoke](const QJsonObject &args,const a::ToolContext &context) {
        return invokeJob(invoke,"fileOperation",args,context);
    };
    a::Tool status;
    status.definition={"file_job_status","Inspect the durable state and result of a Society file job.",
        schema({{"jobId",text}},{"jobId"}),{},true,true};
    status.execute=[invoke](const QJsonObject &args,const a::ToolContext &context) {
        return invokeJob(invoke,"fileJobStatus",args,context);
    };
    return {std::move(submit),std::move(status)};
}
