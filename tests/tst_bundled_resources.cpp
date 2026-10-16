#if defined(__aarch64__)
#include <arm_acle.h>
#endif
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <dlfcn.h>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 3) return 2;
    const auto bundle = QFileInfo(QString::fromLocal8Bit(argv[1])).canonicalFilePath();
    Dl_info core{};
    if (!dladdr(reinterpret_cast<const void *>(&qVersion), &core)
        || !QFileInfo(QString::fromLocal8Bit(core.dli_fname)).canonicalFilePath()
                .startsWith(bundle + "/Contents/Frameworks/")) {
        std::fprintf(stderr, "Resource verification must use the bundled Qt Core.\n");
        return 3;
    }
    const auto library = QFile::encodeName(bundle + "/Contents/Frameworks/libLVRS.dylib");
    if (!dlopen(library.constData(), RTLD_NOW | RTLD_GLOBAL)) {
        std::fprintf(stderr, "%s\n", dlerror());
        return 4;
    }
    QFile manifest(QString::fromLocal8Bit(argv[2]));
    if (!manifest.open(QIODevice::ReadOnly | QIODevice::Text)) return 5;
    QTextStream stream(&manifest);
    int count = 0;
    while (!stream.atEnd()) {
        const auto relative = stream.readLine().trimmed();
        if (relative.isEmpty()) continue;
        const auto resource = ":/qt/qml/LVRS/" + relative;
        if (!QFileInfo::exists(resource) || QFileInfo(resource).size() <= 0) {
            std::fprintf(stderr, "Missing embedded LVRS resource: %s\n", qPrintable(resource));
            return 6;
        }
        ++count;
    }
    if (count == 0) return 7;
    const auto output = QJsonDocument(QJsonObject{{"embeddedAssets", count},
        {"bundledQtCore", true}, {"bundledLvrs", true}}).toJson(QJsonDocument::Compact);
    std::printf("%s\n", output.constData());
    return 0;
}
