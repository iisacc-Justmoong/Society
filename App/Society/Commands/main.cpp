#include "Shared/BackendCommand.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>

namespace {
int fail(const QString &error)
{
    QTextStream(stdout) << QJsonDocument(QJsonObject{{"accepted",false},{"error",error},{"uncertain",false}}).toJson(QJsonDocument::Compact) << Qt::endl;
    return 1;
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    auto args=app.arguments(); args.removeFirst();
    if (args==QStringList{"--help"} || args==QStringList{"-h"}) {
        QTextStream(stdout) << "Usage: society-cli [--broker DIR] status\n"
            "       society-cli [--broker DIR] file-job submit ACTION PATH [--argument VALUE] [--container DIR]\n"
            "       society-cli [--broker DIR] file-job inspect JOB_ID [--container DIR]\n"
            "       society-cli [--broker DIR] agent status|new|cancel\n"
            "       society-cli [--broker DIR] suggested status|refresh\n"
            "       society-cli [--broker DIR] recent status|refresh\n"
            "       society-cli [--broker DIR] resource inspect Files/PATH|Models/PATH [--container DIR]\n"
            "       society-cli [--broker DIR] resource duplicates Files/PATH|Models/PATH [--limit 1-20] [--cursor JSON] [--container DIR]\n"
            "       society-cli [--broker DIR] resource compare-bytes FIRST SECOND [--container DIR]\n"
            "       society-cli [--broker DIR] resource content-index Files/PATH --model URI [--document-prefix TEXT] [--query-prefix TEXT] [--container DIR]\n"
            "       society-cli [--broker DIR] resource content-search --query TEXT --model URI [--limit 1-20] [--document-prefix TEXT] [--query-prefix TEXT] [--container DIR]\n"
            "       society-cli [--broker DIR] resource content-policy status|disable [--container DIR]\n"
            "       society-cli [--broker DIR] resource content-policy enable --model URI [--document-prefix TEXT] [--query-prefix TEXT] [--container DIR]\n"
            "       society-cli [--broker DIR] resource search --query JSON [--limit 1-20] [--cursor JSON] [--container DIR]\n"
            "       society-cli [--broker DIR] resource lineage Files/PATH|Models/PATH [--direction incoming|outgoing|both] [--depth 1-3] [--limit 1-20] [--container DIR]\n"
            "       society-cli [--broker DIR] resource cleanup-plan Files/PATH|Models/PATH [--limit 1-20] [--container DIR]\n"
            "       society-cli [--broker DIR] agent send --stdin [--attach FILE]... [--container DIR]\n"
            "       society-cli [--broker DIR] agent select MODEL_ID\n"
            "       society-cli [--broker DIR] space status|cancel --session ID [--container DIR]\n"
            "       society-cli [--broker DIR] space send --stdin --session ID [--attach FILE]... [--effort low|standard|high] [--container DIR]\n"
            "File-content indexing, search, and policy enablement are disabled; file indexes use hashes and metadata.\n";
        return 0;
    }
    auto broker=qEnvironmentVariable("SOCIETY_LOCAL_BROKER_DIR",QDir::homePath()+"/.iisacc-society-ipc-v1");
    if (args.size()>=2 && args.first()=="--broker") { broker=args.at(1); args.removeFirst(); args.removeFirst(); }
    QString operation, expected, argument; QJsonObject parameters;
    if (args==QStringList{"status"}) operation="status";
    else if (args.size()>=3 && args.at(0)=="resource" && args.at(1)=="inspect") {
        operation="resourceInspect"; parameters={{"path",args.at(2)}};
        if (args.size()==5 && args.at(3)=="--container" && QDir::isAbsolutePath(args.at(4))) expected=args.at(4);
        else if (args.size()!=3) return fail("Invalid resource inspection command.");
    } else if (args.size()>=4 && args.at(0)=="resource" && args.at(1)=="compare-bytes") {
        operation="resourceCompareBytes"; parameters={{"path",args.at(2)},{"otherPath",args.at(3)}};
        if (args.size()==6 && args.at(4)=="--container" && QDir::isAbsolutePath(args.at(5))) expected=args.at(5);
        else if (args.size()!=4) return fail("Invalid byte comparison command.");
    } else if (args.size()>=3 && args.at(0)=="resource" && args.at(1)=="content-policy") {
        const auto command=args.at(2);
        if (command!="status" && command!="enable" && command!="disable") return fail("Invalid content policy command.");
        operation=command=="status"?QStringLiteral("resourceContentPolicyGet"):QStringLiteral("resourceContentPolicySet");
        if (command!="status") parameters.insert("enabled",command=="enable");
        args.erase(args.begin(),args.begin()+3); QSet<QString> seen;
        while (!args.isEmpty()) {
            if (args.size()<2) return fail("An option value is missing.");
            const auto key=args.takeFirst(),value=args.takeFirst();
            if (seen.contains(key)) return fail("A content policy option was repeated.");
            seen.insert(key);
            if (key=="--container" && QDir::isAbsolutePath(value)) expected=value;
            else if (command=="enable" && key=="--model" && !value.trimmed().isEmpty() && value.size()<=1024)
                parameters.insert("model",value);
            else if (command=="enable" && (key=="--document-prefix" || key=="--query-prefix") && value.size()<=256)
                parameters.insert(key=="--document-prefix"?"documentPrefix":"queryPrefix",value);
            else return fail("Invalid content policy option.");
        }
        if (command=="enable" && !parameters.contains("model")) return fail("An embedding model is required.");
    } else if (args.size()>=2 && args.at(0)=="resource" &&
               (args.at(1)=="content-index" || args.at(1)=="content-search")) {
        const bool indexing=args.at(1)=="content-index";
        operation=indexing?QStringLiteral("resourceContentIndex"):QStringLiteral("resourceContentSearch");
        if (indexing) {
            if (args.size()<3 || args.at(2).isEmpty()) return fail("A text resource path is required.");
            parameters.insert("path",args.at(2)); args.erase(args.begin(),args.begin()+3);
        } else args.erase(args.begin(),args.begin()+2);
        QSet<QString> seen;
        while (!args.isEmpty()) {
            if (args.size()<2) return fail("An option value is missing.");
            const auto key=args.takeFirst(), value=args.takeFirst();
            if (seen.contains(key)) return fail("A content search option was repeated.");
            seen.insert(key);
            if (key=="--model" && !value.isEmpty() && value.size()<=1024) parameters.insert("model",value);
            else if (key=="--query" && !indexing && !value.trimmed().isEmpty() && value.size()<=4096) parameters.insert("query",value);
            else if ((key=="--document-prefix" || key=="--query-prefix") && value.size()<=256)
                parameters.insert(key=="--document-prefix"?"documentPrefix":"queryPrefix",value);
            else if (key=="--limit" && !indexing) {
                bool ok=false; const auto number=value.toInt(&ok);
                if (!ok || number<1 || number>20) return fail("The limit must be from 1 to 20.");
                parameters.insert("limit",number);
            } else if (key=="--container" && QDir::isAbsolutePath(value)) expected=value;
            else return fail("Invalid content search option.");
        }
        if (!parameters.contains("model") || (!indexing && !parameters.contains("query")))
            return fail("A model and, for search, a query are required.");
    } else if (args.size()>=3 && args.at(0)=="resource" && args.at(1)=="duplicates") {
        operation="resourceDuplicates"; parameters={{"path",args.at(2)}};
        args.erase(args.begin(),args.begin()+3);
        bool sawLimit=false,sawCursor=false,sawContainer=false;
        while (!args.isEmpty()) {
            if (args.size()<2) return fail("An option value is missing.");
            const auto key=args.takeFirst(), value=args.takeFirst();
            if (key=="--limit" && !sawLimit) {
                bool ok=false; const auto limit=value.toInt(&ok);
                if (!ok || limit<1 || limit>20) return fail("The limit must be from 1 to 20.");
                parameters.insert("limit",limit); sawLimit=true;
            } else if (key=="--cursor" && !sawCursor && value.toUtf8().size()<=4096) {
                QJsonParseError error; const auto document=QJsonDocument::fromJson(value.toUtf8(),&error);
                if (error.error!=QJsonParseError::NoError || !document.isObject() || document.object().isEmpty())
                    return fail("The cursor must be a nonempty JSON object.");
                parameters.insert("cursor",document.object()); sawCursor=true;
            } else if (key=="--container" && !sawContainer && QDir::isAbsolutePath(value)) {
                expected=value; sawContainer=true;
            } else return fail("Invalid or repeated duplicate discovery option.");
        }
    } else if (args.size()>=3 && args.at(0)=="resource" && args.at(1)=="cleanup-plan") {
        operation="resourceCleanupPlan"; parameters={{"path",args.at(2)}};
        args.erase(args.begin(),args.begin()+3);
        bool sawLimit=false,sawContainer=false;
        while (!args.isEmpty()) {
            if (args.size()<2) return fail("An option value is missing.");
            const auto key=args.takeFirst(), value=args.takeFirst();
            if (key=="--limit" && !sawLimit) {
                bool ok=false; const auto number=value.toInt(&ok);
                if (!ok || number<1 || number>20) return fail("The limit must be from 1 to 20.");
                parameters.insert("limit",number); sawLimit=true;
            } else if (key=="--container" && !sawContainer && QDir::isAbsolutePath(value)) {
                expected=value; sawContainer=true;
            } else return fail("Invalid or repeated cleanup plan option.");
        }
    } else if (args.size()>=3 && args.at(0)=="resource" && args.at(1)=="lineage") {
        operation="resourceLineage"; parameters={{"path",args.at(2)}};
        args.erase(args.begin(),args.begin()+3);
        bool sawDirection=false,sawDepth=false,sawLimit=false,sawContainer=false;
        while (!args.isEmpty()) {
            if (args.size()<2) return fail("An option value is missing.");
            const auto key=args.takeFirst(), value=args.takeFirst();
            if (key=="--direction" && !sawDirection && (value=="incoming" || value=="outgoing" || value=="both")) {
                parameters.insert("direction",value); sawDirection=true;
            } else if ((key=="--depth" && !sawDepth) || (key=="--limit" && !sawLimit)) {
                bool ok=false; const auto number=value.toInt(&ok);
                if (!ok || number<1 || number>(key=="--depth"?3:20)) return fail("Invalid lineage bound.");
                parameters.insert(key=="--depth"?"depth":"limit",number);
                if (key=="--depth") sawDepth=true; else sawLimit=true;
            } else if (key=="--container" && !sawContainer && QDir::isAbsolutePath(value)) {
                expected=value; sawContainer=true;
            } else return fail("Invalid or repeated lineage option.");
        }
    } else if (args.size()>=2 && args.at(0)=="resource" && args.at(1)=="search") {
        operation="resourceSearch"; args.erase(args.begin(),args.begin()+2);
        bool sawQuery=false,sawLimit=false,sawCursor=false,sawContainer=false;
        while (!args.isEmpty()) {
            if (args.size()<2) return fail("An option value is missing.");
            const auto key=args.takeFirst(), value=args.takeFirst();
            if ((key=="--query" && !sawQuery && value.toUtf8().size()<=8192) ||
                (key=="--cursor" && !sawCursor && value.toUtf8().size()<=4096)) {
                QJsonParseError error; const auto document=QJsonDocument::fromJson(value.toUtf8(),&error);
                if (error.error!=QJsonParseError::NoError || !document.isObject() || (key=="--cursor" && document.object().isEmpty()))
                    return fail("The search query and cursor must be JSON objects.");
                if (key=="--query") { parameters.insert("query",document.object()); sawQuery=true; }
                else { parameters.insert("cursor",document.object()); sawCursor=true; }
            } else if (key=="--limit" && !sawLimit) {
                bool ok=false; const auto limit=value.toInt(&ok);
                if (!ok || limit<1 || limit>20) return fail("The limit must be from 1 to 20.");
                parameters.insert("limit",limit); sawLimit=true;
            } else if (key=="--container" && !sawContainer && QDir::isAbsolutePath(value)) {
                expected=value; sawContainer=true;
            } else return fail("Invalid or repeated indexed search option.");
        }
        if (!sawQuery) return fail("An indexed search query is required.");
    } else if (args.size()==2 && args.at(0)=="recent") {
        if (args.at(1)=="status") operation="recentSubscribe";
        else if (args.at(1)=="refresh") operation="recentRefresh";
        else return fail("Invalid Recent command.");
    } else if (args.size()==2 && args.at(0)=="suggested") {
        if (args.at(1)=="status") operation="suggestedSubscribe";
        else if (args.at(1)=="refresh") operation="suggestedRefresh";
        else return fail("Invalid Suggested command.");
    } else if (args.size()>=2 && args.at(0)=="agent") {
        const auto command=args.at(1);
        if (args.size()==2 && command=="status") operation="agentSubscribe";
        else if (args.size()==2 && command=="new") operation="agentNew";
        else if (args.size()==2 && command=="cancel") operation="agentCancel";
        else if (args.size()==3 && command=="select") { operation="agentSelect"; parameters.insert("model",args.at(2)); }
        else if (args.size()>=3 && command=="send" && args.at(2)=="--stdin") {
            QTextStream input(stdin); const auto prompt=input.read(16385);
            if (prompt.size()>16384 || !input.atEnd()) return fail("The agent message exceeds 16 KiB.");
            operation="agentSend"; parameters.insert("prompt",prompt);
            args.erase(args.begin(),args.begin()+3); QJsonArray attachments; bool sawContainer=false;
            while (!args.isEmpty()) {
                if (args.size()<2) return fail("An option value is missing.");
                const auto key=args.takeFirst(), value=args.takeFirst();
                if (key=="--attach" && !value.isEmpty() && attachments.size()<8) attachments.append(value);
                else if (key=="--container" && !sawContainer && QDir::isAbsolutePath(value)) { expected=value; sawContainer=true; }
                else return fail("Invalid attachment or container option.");
            }
            parameters.insert("attachments",attachments);
        } else return fail("Invalid agent command.");
    } else if (args.size()>=2 && args.at(0)=="space") {
        const auto command=args.at(1);
        if (command=="status") operation="spaceSubscribe";
        else if (command=="send") operation="spaceSend";
        else if (command=="cancel") operation="spaceCancel";
        else return fail("Invalid Space command.");
        args.erase(args.begin(),args.begin()+2);
        QString session,effort="standard"; QJsonArray attachments;
        bool sawInput=false,sawSession=false,sawEffort=false,sawContainer=false;
        while (!args.isEmpty()) {
            const auto key=args.takeFirst();
            if (key=="--stdin" && command=="send" && !sawInput) { sawInput=true; continue; }
            if (args.isEmpty()) return fail("An option value is missing.");
            const auto value=args.takeFirst();
            if (key=="--session" && !sawSession && !value.isEmpty()) { session=value; sawSession=true; }
            else if (key=="--effort" && command=="send" && !sawEffort && (value=="low" || value=="standard" || value=="high")) { effort=value; sawEffort=true; }
            else if (key=="--attach" && command=="send" && !value.isEmpty() && attachments.size()<8) attachments.append(value);
            else if (key=="--container" && !sawContainer && QDir::isAbsolutePath(value)) { expected=value; sawContainer=true; }
            else return fail("Invalid Space option.");
        }
        if (!sawSession || (command=="send" && !sawInput)) return fail("A Space session and message are required.");
        parameters.insert("spaceId",session);
        if (command=="send") {
            QTextStream input(stdin); const auto prompt=input.read(16385);
            if (prompt.size()>16384 || !input.atEnd()) return fail("The agent message exceeds 16 KiB.");
            parameters.insert("prompt",prompt); parameters.insert("attachments",attachments); parameters.insert("effort",effort);
        }
    } else if (args.size()>=3 && args.at(0)=="file-job") {
        const auto command=args.at(1);
        if (command=="submit" && args.size()>=4) {
            static const QSet<QString> actions{"duplicate","rename","trash","remove","restore","move","create-directory","copy"};
            if (!actions.contains(args.at(2))) return fail("Unknown file operation.");
            operation="fileOperation"; parameters={{"action",args.at(2)},{"path",args.at(3)}};
            args.erase(args.begin(),args.begin()+4);
        } else if (command=="inspect") {
            operation="fileJobStatus"; parameters={{"jobId",args.at(2)}};
            args.erase(args.begin(),args.begin()+3);
        } else return fail("Invalid file-job command.");
        bool sawArgument=false, sawContainer=false;
        while (!args.isEmpty()) {
            if (args.size()<2) return fail("An option value is missing.");
            const auto key=args.takeFirst(), value=args.takeFirst();
            if (key=="--argument" && operation=="fileOperation" && !sawArgument) { argument=value; sawArgument=true; }
            else if (key=="--container" && !sawContainer) { expected=value; sawContainer=true; }
            else return fail("Invalid or repeated option.");
        }
        if (sawContainer && (expected.isEmpty() || !QDir::isAbsolutePath(expected)))
            return fail("The expected Society container must be an absolute path.");
        if (sawArgument) parameters.insert("argument",argument);
    } else return fail("Invalid command. Use --help for usage.");
    const auto result=operation.startsWith("space")
        ? societySpaceCommand(broker,operation,parameters,expected,10000)
        : societyBackendCommand(broker,operation,parameters,expected,
                                operation=="resourceCompareBytes" || operation=="resourceContentIndex" || operation=="resourceContentSearch"
                                || operation=="resourceContentPolicySet"?120000:
                                operation=="resourceInspect" || operation=="resourceDuplicates" || operation=="resourceSearch"
                                || operation=="resourceLineage" || operation=="resourceCleanupPlan" || operation=="resourceContentPolicyGet"?30000:10000);
    auto response=result.response;
    if (!result.ok()) { response.insert("accepted",false); response.insert("error",result.error); response.insert("uncertain",result.uncertain); }
    QTextStream(stdout) << QJsonDocument(response).toJson(QJsonDocument::Compact) << Qt::endl;
    return result.ok()?0:result.uncertain?2:1;
}
