#if defined(__aarch64__)
#include <arm_acle.h>
#endif
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
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
    const QDir source(QString::fromLocal8Bit(argv[2]));
    if (!source.exists("resources")) return 5;
    QDirIterator assets(source.filePath("resources"), {"*.svg", "*.ttf", "*.png"},
                        QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    int count = 0;
    while (assets.hasNext()) {
        const auto relative = source.relativeFilePath(assets.next());
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
