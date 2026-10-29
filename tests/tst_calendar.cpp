#include "Society/Dashboard/DashboardCalendar.h"
#include "Society/Dashboard/CalendarWorkspace.h"
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
    void quickEntryIsAnEditableDraftWithoutWrites() {
        DashboardCalendar calendar; calendar.setTimeZone("Asia/Seoul"); calendar.selectDate("2026-10-23");
        CalendarWorkspace workspace; workspace.setCalendar(&calendar);
        const auto draft = workspace.quickDraft("Tidal Archive review Fri 2–3pm /Personal");
        QCOMPARE(draft.value("title").toString(), "Tidal Archive review");
        QCOMPARE(draft.value("startLocal").toString(), "2026-10-23 14:00");
        QCOMPARE(draft.value("endLocal").toString(), "2026-10-23 15:00");
        QVERIFY(draft.value("id").toString().isEmpty());
        const auto overnight = workspace.quickDraft("Render 2026-10-24 23:30–01:00");
        QCOMPARE(overnight.value("endLocal").toString(), "2026-10-25 01:00");
        QVERIFY(workspace.quickDraft("Invalid 25:00–26:00").isEmpty());
        QVERIFY(workspace.quickDraft("Review /Missing").isEmpty());
        QVERIFY(calendar.databasePath().isEmpty());
        calendar.selectDate("2026-10-30"); workspace.refresh();
        QCOMPARE(workspace.periodTitle(), "October 26–November 1, 2026");
    }
    void workspaceViewsFilterAndNavigationDoNotWrite() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/workspace-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        DashboardCalendar calendar; calendar.setTimeZone("Asia/Seoul");
        calendar.selectDate("2026-10-23"); calendar.setContainerPath(fixture.path());
        QTRY_VERIFY(!calendar.loading());
        CalendarWorkspace workspace; workspace.setCalendar(&calendar); workspace.setActive(true);
        const auto id = workspace.saveEvent({{"title", "Studio review"}, {"startLocal", "2026-10-23 14:00"},
            {"endLocal", "2026-10-23 15:00"}, {"timeZone", "Asia/Seoul"}, {"calendarId", "studio"}});
        QVERIFY2(!id.isEmpty(), qPrintable(workspace.errorString()));
        QTRY_COMPARE(workspace.visibleEvents().size(), 1);
        Store store(calendar.databasePath().toStdString());
        const auto original = *store.get(id.toStdString());
        const QList<QPair<QString,int>> views{{"day",1},{"week",7},{"month",42},{"year",365}};
        for (const auto &[mode,count] : views) {
            workspace.setViewMode(mode);
            QCOMPARE(workspace.periodDays().size(), count);
            QTRY_VERIFY(!workspace.loading());
            QCOMPARE(workspace.visibleEvents().size(), 1);
            QCOMPARE(workspace.visibleEvents().first().toMap().value("date").toString(), "2026-10-23");
        }
        QCOMPARE(workspace.yearMonths().size(), 12);
        workspace.setCalendarVisible("studio", false); QVERIFY(workspace.visibleEvents().isEmpty());
        workspace.setCalendarVisible("studio", true); QCOMPARE(workspace.visibleEvents().size(), 1);
        workspace.selectEvent(id); QCOMPARE(workspace.selectedEvent().value("id").toString(), id);
        workspace.movePeriod(1); QCOMPARE(calendar.selectedDate(), "2027-10-23");
        workspace.movePeriod(-1); QCOMPARE(calendar.selectedDate(), "2026-10-23");
        QCOMPARE(*store.get(id.toStdString()), original);
        workspace.setViewMode("invalid"); QCOMPARE(workspace.viewMode(), "year");
        workspace.setActive(false);
    }
    void workspaceTitleEditsPreservePreciseScheduleAndAlarmMetadata() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/workspace-precision-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        DashboardCalendar calendar; calendar.setContainerPath(fixture.path()); QTRY_VERIFY(!calendar.loading());
        Store store(calendar.databasePath().toStdString());
        Event event; event.id = "precise"; event.title = "Precise schedule";
        // A title-only edit must not round seconds, sub-seconds or a DST fold.
        event.schedule = TimedSchedule{{Instant::parse("2026-11-01T05:30:12.123456789123456789Z"),
            Instant::parse("2026-11-01T06:30:27.987654321987654321Z")}, "America/New_York"};
        event.alarms = {{Duration(600), "display", "Original alert"}, {Duration(3600), "audio", "Second alert"}};
        event.properties["other.consumer"] = "preserve";
        const auto original = store.put(event);
        CalendarWorkspace workspace; workspace.setCalendar(&calendar);
        auto draft = workspace.eventDraft("precise"); draft["title"] = "Renamed";
        QVERIFY2(!workspace.saveEvent(draft).isEmpty(), qPrintable(workspace.errorString()));
        const auto saved = store.get("precise"); QVERIFY(saved);
        QCOMPARE(saved->schedule, original.schedule); QCOMPARE(saved->alarms, original.alarms);
        QCOMPARE(saved->properties, original.properties);
    }
    void workspaceEditorPersistsAndRejectsRevisionConflicts() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/workspace-editor-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        DashboardCalendar calendar; calendar.setTimeZone("Asia/Seoul");
        calendar.selectDate("2026-10-23"); calendar.setContainerPath(fixture.path()); QTRY_VERIFY(!calendar.loading());
        CalendarWorkspace workspace; workspace.setCalendar(&calendar); workspace.setActive(true);
        const auto id = workspace.saveEvent({{"title", "Review"}, {"startLocal", "2026-10-23 14:00"},
            {"endLocal", "2026-10-23 15:00"}, {"timeZone", "Asia/Seoul"}, {"calendarId", "studio"},
            {"description", "Canvas notes"}, {"location", "Studio room"}, {"invitees", "Jun Lee; Mira Yoon"},
            {"alarmMinutes", 10}, {"repeat", "weekly"}, {"meetingUrl", "https://example.com/review"}});
        QVERIFY2(!id.isEmpty(), qPrintable(workspace.errorString()));
        Store reopened(calendar.databasePath().toStdString());
        auto stored = reopened.get(id.toStdString()); QVERIFY(stored);
        QCOMPARE(stored->participants.size(), size_t(2)); QCOMPARE(stored->alarms.size(), size_t(1));
        QCOMPARE(stored->recurrence.frequency, Frequency::Weekly);
        auto draft = workspace.eventDraft(id); QCOMPARE(draft.value("description").toString(), "Canvas notes");
        draft["title"] = "Updated review"; QVERIFY(!workspace.saveEvent(draft).isEmpty());
        QVERIFY(workspace.saveEvent(draft).isEmpty());
        QCOMPARE(reopened.get(id.toStdString())->title, std::string("Updated review"));
        QVERIFY(!workspace.removeEvent(id, draft.value("revision").toString()));
        const auto current = workspace.eventDraft(id);
        QVERIFY(workspace.removeEvent(id, current.value("revision").toString()));
        QVERIFY(!reopened.get(id.toStdString()));
        const auto staleContainerDraft = workspace.eventDraft();
        QTemporaryDir other(SOCIETY_TEST_DIRECTORY "/workspace-other-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(other.path()));
        calendar.setContainerPath(other.path()); QTRY_VERIFY(!calendar.loading());
        auto stale = staleContainerDraft; stale["title"] = "Wrong container";
        QVERIFY(workspace.saveEvent(stale).isEmpty());
        Store otherStore(calendar.databasePath().toStdString()); QVERIFY(otherStore.query().empty());
        workspace.setActive(false);
    }
    void workspaceCivilDaysAndOvernightSegments() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/workspace-dst-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        DashboardCalendar calendar; calendar.setTimeZone("America/New_York");
        calendar.selectDate("2026-03-08"); calendar.setContainerPath(fixture.path()); QTRY_VERIFY(!calendar.loading());
        CalendarWorkspace workspace; workspace.setCalendar(&calendar); workspace.setActive(true);
        QVERIFY(workspace.saveEvent({{"title", "Missing clock time"}, {"startLocal", "2026-03-08 02:30"},
            {"endLocal", "2026-03-08 03:30"}, {"timeZone", "America/New_York"}}).isEmpty());
        const auto allDay = workspace.saveEvent({{"title", "DST all day"}, {"allDay", true},
            {"startLocal", "2026-03-08"}, {"endLocal", "2026-03-08"}, {"timeZone", "America/New_York"}});
        QVERIFY2(!allDay.isEmpty(), qPrintable(workspace.errorString()));
        Store store(calendar.databasePath().toStdString());
        const auto event = store.get(allDay.toStdString()); QVERIFY(event);
        const auto schedule = std::get<AllDaySchedule>(event->schedule);
        QCOMPARE(schedule.end, Date(2026,3,9));
        QCOMPARE((TimeZone("America/New_York").dayRange(schedule.start).end
            - TimeZone("America/New_York").dayRange(schedule.start).start).seconds(), 23LL*3600);
        calendar.setTimeZone("Asia/Seoul"); calendar.selectDate("2026-10-23");
        const auto overnight = workspace.saveEvent({{"title", "Overnight"}, {"startLocal", "2026-10-23 23:30"},
            {"endLocal", "2026-10-24 01:00"}, {"timeZone", "Asia/Seoul"}});
        QVERIFY(!overnight.isEmpty()); workspace.setViewMode("week");
        QTRY_VERIFY([&] { int count=0; for (const auto &v:workspace.visibleEvents()) if(v.toMap().value("id")==overnight)++count; return count==2; }());
        const auto spill = workspace.saveEvent({{"title", "Previous-month grid cell"},
            {"startLocal", "2026-09-28 09:00"}, {"endLocal", "2026-09-28 10:00"}, {"timeZone", "Asia/Seoul"}});
        QVERIFY(!spill.isEmpty()); calendar.selectDate("2026-10-23"); workspace.setViewMode("month");
        QTRY_VERIFY(!workspace.loading());
        QVERIFY([&] { for (const auto &v:workspace.visibleEvents()) if(v.toMap().value("id")==spill) return v.toMap().value("date")=="2026-09-28"; return false; }());
        const auto foreign = workspace.saveEvent({{"title", "Los Angeles late evening"},
            {"startLocal", "2026-10-23 23:00"}, {"endLocal", "2026-10-24 00:00"}, {"timeZone", "America/Los_Angeles"}});
        QVERIFY(!foreign.isEmpty()); QCOMPARE(calendar.selectedDate(), "2026-10-24");
        workspace.setActive(false);
    }
    void monthCountsMatchNativeOverlap_data() {
        QTest::addColumn<QString>("zoneName"); QTest::addColumn<QString>("selected");
        QTest::newRow("ordinary") << QString("Asia/Seoul") << QString("2026-09-19");
        QTest::newRow("spring-dst") << QString("America/New_York") << QString("2026-03-08");
        QTest::newRow("fall-dst") << QString("America/New_York") << QString("2026-11-01");
        QTest::newRow("skipped-civil-day") << QString("Pacific/Apia") << QString("2011-12-30");
    }
    void monthCountsMatchNativeOverlap() {
        QFETCH(QString, zoneName); QFETCH(QString, selected);
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/calendar-counts-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.path()));
        DashboardCalendar model; model.setTimeZone(zoneName); model.selectDate(selected);
        model.setContainerPath(fixture.path()); QTRY_VERIFY(!model.loading());
        QVERIFY2(model.errorString().isEmpty(), qPrintable(model.errorString()));
        const TimeZone zone(zoneName.toStdString());
        const auto days = model.days();
        const auto selectedDate = Date::parse(selected.toStdString());
        {
            Store store(model.databasePath().toStdString());
            for (int i = 0; i < days.size(); ++i) {
                const auto date = Date::parse(days[i].toMap().value("date").toString().toStdString());
                // Cover grid edges, weekly gaps and the civil-time transition
                // without making hundreds of identical durable fixture writes.
                if (i > 1 && i < days.size() - 2 && i % 7 != 0
                    && date != selectedDate && date != selectedDate.addDays(-1) && date != selectedDate.addDays(1)) continue;
                const auto range = zone.dayRange(date);
                for (int variant = 0; variant < 4; ++variant) {
                    Event event; event.id = "count-" + std::to_string(i) + '-' + std::to_string(variant);
                    event.title = event.id;
                    auto start = range.start, end = range.end;
                    if (variant == 1) end = start; // Point events belong to their starting day.
                    if (variant == 2) { start = start - Duration(3600); end = end + Duration(3600); }
                    if (variant == 3) end = zone.dayRange(date.addDays(4)).end;
                    event.schedule = TimedSchedule{{start, end}, zoneName.toStdString()}; store.put(event);
                }
            }
        }
        model.refresh(); QTRY_VERIFY(!model.loading());
        QVERIFY2(model.errorString().isEmpty(), qPrintable(model.errorString()));
        Store store(model.databasePath().toStdString());
        const auto first = Date::parse(days.first().toMap().value("date").toString().toStdString());
        const auto last = Date::parse(days.last().toMap().value("date").toString().toStdString());
        const auto scheduled = store.scheduled({zone.dayRange(first).start, zone.dayRange(last).end});
        int emptyDays = 0;
        for (const auto &value : model.days()) {
            const auto row = value.toMap();
            const auto range = zone.dayRange(Date::parse(row.value("date").toString().toStdString()));
            int expected = 0;
            for (const auto &item : scheduled) if (item.occurrence.range.overlaps(range)) ++expected;
            QCOMPARE(row.value("count").toInt(), expected);
            if (expected == 0) ++emptyDays;
        }
        QVERIFY(emptyDays > 0);
    }
    void calendarSnapshotAndPersistentCompletion() {
        QTemporaryDir fixture(SOCIETY_TEST_DIRECTORY "/calendar-XXXXXX");
        QVERIFY(fixture.isValid());

        QVERIFY(QDir().mkpath(fixture.filePath("drive.societycontainer")));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.filePath("drive.societycontainer")));
        DashboardCalendar model;
        model.setTimeZone("Asia/Seoul");
        model.selectDate("2026-09-19");
        model.setContainerPath(fixture.filePath("drive.societycontainer"));
        QTRY_VERIFY(!model.loading());
        QVERIFY2(model.errorString().isEmpty(), qPrintable(model.errorString()));
        QVERIFY(model.databasePath().startsWith(fixture.filePath("drive.societycontainer/.society-runtime/calendar/")));
        QVERIFY(!QFileInfo::exists(fixture.filePath("state")));
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

        for (auto name : {"one.societycontainer", "two.societycontainer"}) {
            QVERIFY(QDir().mkpath(fixture.filePath(name)));
            QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.filePath(name)));
        }
        DashboardCalendar model;
        model.setTimeZone("America/New_York"); model.selectDate("2026-03-08");
        model.setContainerPath(fixture.filePath("one.societycontainer")); QTRY_VERIFY(!model.loading());
        auto first = model.databasePath();
        { Store store(first.toStdString()); Event event; event.id = "spring"; event.title = "DST day";
          event.schedule = AllDaySchedule{Date(2026, 3, 8), Date(2026, 3, 9), "America/New_York"}; store.put(event); }
        model.refresh(); QTRY_VERIFY(!model.loading());
        QCOMPARE(model.events().size(), 1);
        QCOMPARE(model.events().first().toMap()["time"].toString(), "All day");
        model.refresh(); model.setContainerPath(fixture.filePath("two.societycontainer"));
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

        DashboardCalendar model;
        model.setTimeZone("Asia/Seoul"); model.selectDate("2026-09-19");
        model.setContainerPath(fixture.filePath("later.societycontainer"));
        QVERIFY(model.databasePath().isEmpty());
        QVERIFY(QDir().mkpath(fixture.filePath("later.societycontainer")));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(fixture.filePath("later.societycontainer")));
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
