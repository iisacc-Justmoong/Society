#include "Shared/BackendCommand.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
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
            "       society-cli [--broker DIR] file-job inspect JOB_ID [--container DIR]\n";
        return 0;
    }
    auto broker=qEnvironmentVariable("SOCIETY_LOCAL_BROKER_DIR",QDir::homePath()+"/.iisacc-society-ipc-v1");
    if (args.size()>=2 && args.first()=="--broker") { broker=args.at(1); args.removeFirst(); args.removeFirst(); }
    QString operation, expected, argument; QJsonObject parameters;
    if (args==QStringList{"status"}) operation="status";
    else if (args.size()>=3 && args.at(0)=="file-job") {
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
    const auto result=societyBackendCommand(broker,operation,parameters,expected);
    auto response=result.response;
    if (!result.ok()) { response.insert("accepted",false); response.insert("error",result.error); response.insert("uncertain",result.uncertain); }
    QTextStream(stdout) << QJsonDocument(response).toJson(QJsonDocument::Compact) << Qt::endl;
    return result.ok()?0:result.uncertain?2:1;
}
