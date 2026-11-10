#include "Society/Native/NativeFilesLauncher.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class NativeFilesLauncherTests final : public QObject {
    Q_OBJECT
private slots:
    void startsOnlyTheExpectedExecutable() {
        QTemporaryDir root(QDir::cleanPath(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/../../.native-launcher-XXXXXX")); QVERIFY(root.isValid());
        const auto program=root.filePath("SocietyNativeFiles"); QString error, seen; int calls=0;
        auto start=[&](const QString &path){++calls; seen=path; return true;};
        QVERIFY(!society::native::launchFiles(program,start,&error)); QCOMPARE(calls,0); QVERIFY(!error.isEmpty());
        QFile file(program); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture"); file.close();
        QVERIFY(file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner));
        QVERIFY2(society::native::launchFiles(program,start,&error),qPrintable(error)); QCOMPARE(calls,1); QCOMPARE(seen,program); QVERIFY(error.isEmpty());
        QVERIFY(!society::native::launchFiles(program,[](const QString &){return false;},&error)); QVERIFY(!error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(NativeFilesLauncherTests)
#include "tst_native_files_launcher.moc"
