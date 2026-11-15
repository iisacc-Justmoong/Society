#include "Daemon/RecentCatalog.h"
#include <SocietyDrive.h>
#include <SharedStorage.h>
#include <FileCatalog.h>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <filesystem>

class RecentCatalogTest final : public QObject {
    Q_OBJECT
private slots:
    void newestFilesAreBoundedAndActivitySurvivesRename() {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/recent-XXXXXX");
        QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer");
        QVERIFY(QDir().mkdir(root)); QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto files=root+"/Files";
        const auto base=QDateTime::currentDateTimeUtc().addDays(-3);
        for (int i=0;i<520;++i) {
            QFile file(files+QString("/file-%1.txt").arg(i,4,10,QLatin1Char('0')));
            QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("x"),qint64(1)); QVERIFY(file.flush());
            QVERIFY(file.setFileTime(base.addSecs(i),QFileDevice::FileModificationTime));
        }
        QFile hidden(files+"/.private"); QVERIFY(hidden.open(QIODevice::WriteOnly)); hidden.write("x"); hidden.close();
        QFile companion(files+"/file-0001.txt.iisacc.json"); QVERIFY(companion.open(QIODevice::WriteOnly)); companion.write("{}"); companion.close();
        QVERIFY(QDir().mkdir(files+"/folder"));
        const auto initial=society::recent::RecentCatalog::update(root,{},500);
        QVERIFY2(initial.error.isEmpty(),qPrintable(initial.error)); QCOMPARE(initial.items.size(),500);
        QCOMPARE(initial.items.front().name,QStringLiteral("file-0519.txt"));
        QCOMPARE(initial.items.back().name,QStringLiteral("file-0020.txt"));
        const auto old=files+"/file-0000.txt"; const auto now=QDateTime::currentMSecsSinceEpoch();
        const auto opened=society::recent::RecentCatalog::update(root,{{old,now}},500);
        QVERIFY2(opened.error.isEmpty(),qPrintable(opened.error)); QCOMPARE(opened.items.front().name,QStringLiteral("file-0000.txt"));
        QCOMPARE(opened.items.front().activityMs,now);
        const auto renamed=files+"/renamed.txt"; QVERIFY(QFile::rename(old,renamed));
        const auto restored=society::recent::RecentCatalog::update(root,{},500);
        QVERIFY2(restored.error.isEmpty(),qPrintable(restored.error)); QCOMPARE(restored.items.front().name,QStringLiteral("renamed.txt"));
        QCOMPARE(restored.items.front().activityMs,now);
        // A different file at an old path must not inherit the first file's activity.
        QFile replacement(old); QVERIFY(replacement.open(QIODevice::WriteOnly)); replacement.write("new"); QVERIFY(replacement.flush());
        QVERIFY(replacement.setFileTime(base.addSecs(518),QFileDevice::FileModificationTime)); replacement.close();
        const auto separated=society::recent::RecentCatalog::update(root,{},500);
        const auto hit=std::find_if(separated.items.cbegin(),separated.items.cend(),[&](const auto &x){return x.path==old;});
        QVERIFY(hit!=separated.items.cend()); QCOMPARE(hit->activityMs,base.addSecs(518).toMSecsSinceEpoch());
        const auto project=files+"/sample.iiproject"; QVERIFY(QDir().mkdir(project));
        QFile manifest(project+"/manifest.json"); QVERIFY(manifest.open(QIODevice::WriteOnly)); manifest.write("{}"); manifest.close();
        const auto packaged=society::recent::RecentCatalog::update(root,{},500);
        QVERIFY2(packaged.error.isEmpty(),qPrintable(packaged.error));
        QVERIFY(std::any_of(packaged.items.cbegin(),packaged.items.cend(),[&](const auto &x){return x.path==project;}));
        const auto packageItem=std::find_if(packaged.items.cbegin(),packaged.items.cend(),[&](const auto &x){return x.path==project;});
        QCOMPARE(packageItem->size,qint64(-1));
        QVERIFY(std::none_of(packaged.items.cbegin(),packaged.items.cend(),[&](const auto &x){return x.path==project+"/manifest.json";}));
        std::filesystem::create_hard_link(std::filesystem::path(old.toStdU16String()),std::filesystem::path((files+"/hardlink.txt").toStdU16String()));
        const auto ambiguous=society::recent::RecentCatalog::update(root,{{old,now+2000}},500);
        QVERIFY2(ambiguous.error.isEmpty(),qPrintable(ambiguous.error)); QVERIFY(ambiguous.partial);
        const auto unmerged=std::find_if(ambiguous.items.cbegin(),ambiguous.items.cend(),[&](const auto &x){return x.path==old;});
        QVERIFY(unmerged!=ambiguous.items.cend()); QCOMPARE(unmerged->activityMs,base.addSecs(518).toMSecsSinceEpoch());
#ifdef Q_OS_UNIX
        const auto backslash=files+"/back\\slash.txt";
        QFile named(backslash); QVERIFY(named.open(QIODevice::WriteOnly)); named.write("x"); QVERIFY(named.flush());
        QVERIFY(named.setFileTime(base.addSecs(5),QFileDevice::FileModificationTime)); named.close();
        const auto namedAt=QDateTime::currentMSecsSinceEpoch();
        const auto namedResult=society::recent::RecentCatalog::update(root,{{backslash,namedAt}},500);
        QVERIFY2(namedResult.error.isEmpty(),qPrintable(namedResult.error));
        const auto reread=society::recent::RecentCatalog::update(root,{},500);
        QVERIFY2(reread.error.isEmpty(),qPrintable(reread.error));
        QVERIFY(std::any_of(reread.items.cbegin(),reread.items.cend(),[&](const auto &x){return x.path==backslash;}));
        QVERIFY(QDir().mkdir(fixture.filePath("outside")));
        QFile outside(fixture.filePath("outside/private.txt")); QVERIFY(outside.open(QIODevice::WriteOnly)); outside.write("private"); outside.close();
        QVERIFY(QFile::link(fixture.filePath("outside"),files+"/redirect"));
        const auto redirected=files+"/redirect/private.txt";
        const auto confined=society::recent::RecentCatalog::update(root,{{redirected,now+3000}},500);
        QVERIFY2(confined.error.isEmpty(),qPrintable(confined.error));
        QVERIFY(std::none_of(confined.items.cbegin(),confined.items.cend(),[&](const auto &x){return x.path==redirected;}));
#endif
    }
    void includesFilesOutsideAnIncompleteCatalogBeforeLimiting() {
        QTemporaryDir fixture(QStringLiteral(SOCIETY_TEST_DIRECTORY)+"/recent-indexed-XXXXXX"); QVERIFY(fixture.isValid());
        const auto root=fixture.filePath("drive.societycontainer"); QVERIFY(QDir().mkdir(root));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root));
        const auto files=root+"/Files"; const auto base=QDateTime::currentDateTimeUtc().addDays(-1);
        std::vector<iiFileProvider::FileRegistration> registrations;
        for (int i=0;i<1030;++i) {
            const auto name=QString("file-%1.txt").arg(i,4,10,QLatin1Char('0'));
            QFile file(files+"/"+name); QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.write("x")==1); QVERIFY(file.flush());
            QVERIFY(file.setFileTime(base.addSecs(i),QFileDevice::FileModificationTime)); file.close();
            registrations.push_back({name.toStdString(),{}});
        }
        namespace fs=std::filesystem;
        const auto metadata=iiSocietyContainer::FileRepository::classificationDirectory(fs::path(root.toStdU16String()),"files");
        {
            iiFileProvider::FileCatalog catalog(fs::path(files.toStdU16String()),iiFileProvider::FileCatalog::Access::Create,metadata);
            const std::vector<iiFileProvider::FileRegistration> first(registrations.begin(),registrations.begin()+500);
            QCOMPARE(catalog.registerFiles(first).size(),std::size_t(500));
        }
        const auto result=society::recent::RecentCatalog::update(root,{},500);
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error)); QCOMPARE(result.items.size(),500);
        QCOMPARE(result.items.front().name,QStringLiteral("file-1029.txt"));
        QCOMPARE(result.items.back().name,QStringLiteral("file-0530.txt"));
        const auto pending=files+"/not-yet-indexed.txt";
        QFile fresh(pending); QVERIFY(fresh.open(QIODevice::WriteOnly)); fresh.write("new"); fresh.close();
        const auto now=QDateTime::currentMSecsSinceEpoch();
        const auto first=society::recent::RecentCatalog::update(root,{{pending,now}},500);
        QVERIFY2(first.error.isEmpty(),qPrintable(first.error)); QCOMPARE(first.items.front().path,pending);
        const auto afterRefresh=society::recent::RecentCatalog::update(root,{},500);
        QVERIFY2(afterRefresh.error.isEmpty(),qPrintable(afterRefresh.error)); QCOMPARE(afterRefresh.items.front().path,pending);
        QVERIFY(QFile::rename(pending,files+"/parked.txt"));
        QFile replaced(pending); QVERIFY(replaced.open(QIODevice::WriteOnly)); replaced.write("replacement"); QVERIFY(replaced.flush());
        QVERIFY(replaced.setFileTime(base.addSecs(700),QFileDevice::FileModificationTime)); replaced.close();
        const auto afterReplacement=society::recent::RecentCatalog::update(root,{},500);
        QVERIFY2(afterReplacement.error.isEmpty(),qPrintable(afterReplacement.error));
        const auto newFile=std::find_if(afterReplacement.items.cbegin(),afterReplacement.items.cend(),[&](const auto &x){return x.path==pending;});
        QVERIFY(newFile!=afterReplacement.items.cend()); QCOMPARE(newFile->activityMs,base.addSecs(700).toMSecsSinceEpoch());
        const auto indexed=files+"/file-0001.txt";
        QVERIFY2(society::recent::RecentCatalog::update(root,{{indexed,now+1000}},500).error.isEmpty(),"Cannot record fixture activity");
        QVERIFY(QFile::rename(indexed,files+"/parked-original.txt"));
        QFile different(indexed); QVERIFY(different.open(QIODevice::WriteOnly)); different.write("other"); QVERIFY(different.flush());
        QVERIFY(different.setFileTime(base.addSecs(700),QFileDevice::FileModificationTime)); different.close();
        const auto staleIndex=society::recent::RecentCatalog::update(root,{},500);
        QVERIFY2(staleIndex.error.isEmpty(),qPrintable(staleIndex.error));
        const auto replacement=std::find_if(staleIndex.items.cbegin(),staleIndex.items.cend(),[&](const auto &x){return x.path==indexed;});
        QVERIFY(replacement!=staleIndex.items.cend()); QCOMPARE(replacement->activityMs,base.addSecs(700).toMSecsSinceEpoch());
    }
};
QTEST_GUILESS_MAIN(RecentCatalogTest)
#include "tst_recent_catalog.moc"
