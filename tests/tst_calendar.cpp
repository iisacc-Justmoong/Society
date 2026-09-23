#include "App/Dashboard/DashboardCalendar.h"
#include <SocietyDrive.h>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace iiCalendar;

class CalendarTest : public QObject {
    Q_OBJECT
private slots:
    void calendarSnapshotAndPersistentCompletion() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/calendar-XXXXXX");
        QVERIFY(fixture.isValid());
        qputenv("SOCIETY_CALENDAR_DIRECTORY", fixture.filePath("state").toUtf8());
        QVERIFY(QDir().mkpath(fixture.filePath("drive")));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.filePath("drive")));
        DashboardCalendar model;
        model.setTimeZone("Asia/Seoul");
        model.selectDate("2026-09-19");
        model.setContainerPath(fixture.filePath("drive"));
        QTRY_VERIFY(!model.loading());
        QVERIFY2(model.errorString().isEmpty(), qPrintable(model.errorString()));
        QCOMPARE(model.days().size(), 42);
        QCOMPARE(model.days().first().toMap()["date"].toString(), "2026-08-31");
        QCOMPARE(model.monthTitle(), "September 2026");
        {
            Store store(model.databasePath().toStdString());
            for (int i = 0; i < 5; ++i) {
                Event event; event.id = "event-" + std::to_string(i);
                event.title = "Product review " + std::to_string(i);
                auto start = TimeZone("Asia/Seoul").resolve({Date(2026, 9, 19), Time(9, i, 0, 123456789012345678ULL)});
                event.schedule = TimedSchedule{{start, start + Duration(2700)}, "Asia/Seoul"};
                event.attachments.push_back({"brief-" + std::to_string(i), "Launch brief.pdf", "Files/brief.pdf", "application/pdf", 248000});
                store.put(event);
            }
            Event task; task.id = "task"; task.title = "Launch checklist"; task.kind = EventKind::Task;
            task.due = TimeZone("Asia/Seoul").resolve({Date(2026, 9, 19), Time(17)});
            store.put(task);
        }
        model.refresh(); QTRY_VERIFY(!model.loading());
        QCOMPARE(model.events().size(), 5);
        QCOMPARE(model.tasks().size(), 1);
        QCOMPARE(model.files().size(), 5);
        QVERIFY(model.events().first().toMap()["start"].toString().endsWith("123456789012345678Z"));
        QCOMPARE(model.summary(), "5 events · 1 open tasks · 5 files");
        QCOMPARE(model.days()[19].toMap()["count"].toInt(), 6);
        auto task = model.tasks().first().toMap();
        QVERIFY(model.setTaskCompleted("task", task["revision"].toString(), true));
        QTRY_VERIFY(!model.loading());
        QVERIFY(model.tasks().first().toMap()["completed"].toBool());
        QCOMPARE(model.completedTasks(), 1);
        QVERIFY(!model.setTaskCompleted("task", task["revision"].toString(), false));
        Store reopened(model.databasePath().toStdString());
        QCOMPARE(reopened.get("task")->status, EventStatus::Completed);
        model.moveDay(1); QTRY_VERIFY(!model.loading());
        QVERIFY(model.events().isEmpty());
        model.moveMonth(-1); QTRY_VERIFY(!model.loading());
        QCOMPARE(model.selectedDate(), "2026-08-20");
        model.selectDate("2026-02-30");
        QCOMPARE(model.selectedDate(), "2026-08-20");
        QVERIFY(!model.errorString().isEmpty());
        model.setContainerPath("");
        QVERIFY(model.events().isEmpty()); QVERIFY(model.tasks().isEmpty());
        QVERIFY(model.databasePath().isEmpty());
    }

    void staleResultsAndTimeZones() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/calendar-isolation-XXXXXX");
        qputenv("SOCIETY_CALENDAR_DIRECTORY", fixture.filePath("state").toUtf8());
        for (auto name : {"one", "two"}) {
            QVERIFY(QDir().mkpath(fixture.filePath(name)));
            QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.filePath(name)));
        }
        DashboardCalendar model;
        model.setTimeZone("America/New_York"); model.selectDate("2026-03-08");
        model.setContainerPath(fixture.filePath("one")); QTRY_VERIFY(!model.loading());
        auto first = model.databasePath();
        { Store store(first.toStdString()); Event event; event.id = "spring"; event.title = "DST day";
          event.schedule = AllDaySchedule{Date(2026, 3, 8), Date(2026, 3, 9), "America/New_York"}; store.put(event); }
        model.refresh(); QTRY_VERIFY(!model.loading());
        QCOMPARE(model.events().size(), 1);
        QCOMPARE(model.events().first().toMap()["time"].toString(), "All day");
        model.refresh(); model.setContainerPath(fixture.filePath("two"));
        QTRY_VERIFY(!model.loading()); QVERIFY(model.events().isEmpty());
        QVERIFY(first != model.databasePath());
        model.setCalendarSystem("hebrew"); QTRY_VERIFY(!model.loading());
        QCOMPARE(model.days().size(), 42);
        model.setTimeZone("Invalid/Zone"); QCOMPARE(model.timeZone(), "America/New_York");
        model.setContainerPath("relative");
        QVERIFY(model.databasePath().isEmpty()); QVERIFY(!model.errorString().isEmpty());
    }

    void recurringItemsAndLateContainerRecovery() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/calendar-recovery-XXXXXX");
        qputenv("SOCIETY_CALENDAR_DIRECTORY", fixture.filePath("state").toUtf8());
        DashboardCalendar model;
        model.setTimeZone("Asia/Seoul"); model.selectDate("2026-09-19");
        model.setContainerPath(fixture.filePath("later"));
        QVERIFY(model.databasePath().isEmpty());
        QVERIFY(QDir().mkpath(fixture.filePath("later")));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.filePath("later")));
        model.refresh(); QTRY_VERIFY(!model.loading()); QVERIFY(!model.databasePath().isEmpty());
        {
            Store store(model.databasePath().toStdString());
            Event task; task.id = "weekly"; task.title = "Weekly planning"; task.kind = EventKind::Task;
            task.schedule = AllDaySchedule{Date(2026, 9, 12), Date(2026, 9, 13), "Asia/Seoul"};
            task.recurrence.frequency = Frequency::Weekly; task.recurrence.count = 3; store.put(task);
            Event event; event.id = "midnight"; event.title = "Local midnight";
            auto start = Instant::parse("2026-09-18T15:00:00.999999999999999999Z");
            event.schedule = TimedSchedule{{start, start + Duration(60)}, "Asia/Seoul"}; store.put(event);
        }
        model.refresh(); QTRY_VERIFY(!model.loading());
        QCOMPARE(model.tasks().size(), 1); QCOMPARE(model.events().size(), 1);
        QVERIFY(model.tasks().first().toMap()["recurring"].toBool());
        QVERIFY(!model.setTaskCompleted("weekly", model.tasks().first().toMap()["revision"].toString(), true));
        model.setTimeZone("UTC"); QTRY_VERIFY(!model.loading()); QVERIFY(model.events().isEmpty());
        model.moveDay(-1); QTRY_VERIFY(!model.loading()); QCOMPARE(model.events().size(), 1);
        QCOMPARE(model.events().first().toMap()["time"].toString(), "15:00");
    }
};
QTEST_GUILESS_MAIN(CalendarTest)
#include "tst_calendar.moc"
