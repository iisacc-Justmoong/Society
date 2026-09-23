#include "DashboardCalendar.h"
#include <SocietyDrive.h>
#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrentRun>
#include <algorithm>

using namespace iiCalendar;
namespace {
QString string(const std::string &value) { return QString::fromStdString(value); }
QString timeText(Instant instant, const TimeZone &zone) {
    const auto time = zone.at(instant).local.time;
    return QStringLiteral("%1:%2").arg(time.hour(), 2, 10, QChar('0')).arg(time.minute(), 2, 10, QChar('0'));
}
QVariantMap row(const ScheduledEvent &item, const TimeZone &zone) {
    const auto &event = item.event;
    const auto duration = item.occurrence.range.end - item.occurrence.range.start;
    QStringList subtitle;
    if (item.occurrence.allDay) subtitle << QObject::tr("All day");
    else if (duration.seconds() > 0) subtitle << QObject::tr("%1 min").arg((duration.seconds() + 59) / 60);
    if (!event.location.name.empty()) subtitle << string(event.location.name);
    else if (!event.description.empty()) subtitle << string(event.description);
    const bool recurring = event.recurrence.frequency != Frequency::None || !event.recurrence.additional.empty();
    if (recurring) subtitle << QObject::tr("Repeats");
    return {{"id", string(event.id)}, {"title", string(event.title)},
        {"description", string(event.description)}, {"subtitle", subtitle.join(QStringLiteral(" · "))},
        {"time", item.occurrence.allDay ? QObject::tr("All day") : timeText(item.occurrence.range.start, zone)},
        {"start", string(item.occurrence.range.start.toString())}, {"end", string(item.occurrence.range.end.toString())},
        {"due", event.due ? timeText(*event.due, zone) : QString()},
        {"completed", event.status == EventStatus::Completed}, {"revision", QString::number(event.revision)},
        {"recurring", recurring}, {"location", string(event.location.name)},
        {"participants", static_cast<int>(event.participants.size())},
        {"attachmentCount", static_cast<int>(event.attachments.size())}};
}
QVariantList rows(const DetailSection &section, const TimeZone &zone) {
    QVariantList result;
    for (const auto &item : section.preview) result.append(row(item, zone));
    return result;
}
QVariantList dayRows(const CalendarView &view, Date selected) {
    QVariantList result;
    for (const auto &cell : view.days)
        result.append(QVariantMap{{"date", string(cell.date.toString())}, {"day", cell.calendarDate.day},
            {"inMonth", cell.inPeriod}, {"today", cell.today}, {"selected", cell.date == selected}, {"count", 0}});
    return result;
}
}

DashboardCalendar::DashboardCalendar(QObject *parent) : QObject(parent) {
    try { m_zone = TimeZone::system(); } catch (...) { m_zone = TimeZone("UTC"); }
    m_selected = m_zone.at(Clock::sample().wallTime).local.date;
    updateDays();
    m_timer.setInterval(30000);
    connect(&m_timer, &QTimer::timeout, this, &DashboardCalendar::refresh);
    connect(&m_watcher, &QFutureWatcher<Snapshot>::finished, this, [this] {
        const auto snapshot = m_watcher.result();
        if (snapshot.generation == m_generation) {
            m_days = snapshot.days; m_events = snapshot.events; m_tasks = snapshot.tasks;
            m_activity = snapshot.activity; m_notes = snapshot.notes; m_reminders = snapshot.reminders;
            m_files = snapshot.files; m_completedTasks = snapshot.completedTasks; m_error = snapshot.error;
        }
        m_loading = false;
        if (m_pending) { m_pending = false; refresh(); }
        else emit changed();
    });
}
DashboardCalendar::~DashboardCalendar() { m_watcher.waitForFinished(); }
void DashboardCalendar::clearDetails() {
    m_events.clear(); m_tasks.clear(); m_activity.clear(); m_notes.clear(); m_reminders.clear(); m_files.clear();
    m_completedTasks = 0;
}
void DashboardCalendar::fail(const std::exception &error) { m_error = string(error.what()); emit changed(); }
void DashboardCalendar::updateDays() {
    ViewRequest request; request.anchor = m_selected; request.calendarSystem = m_calendarSystem.toStdString();
    request.today = m_zone.at(Clock::sample().wallTime).local.date;
    m_days = dayRows(makeView(request, m_calendars), m_selected);
}
void DashboardCalendar::setContainerPath(const QString &path) {
    if (m_containerPath == path) return;
    ++m_generation; m_containerPath = path; m_databasePath.clear(); m_driveIdentifier.clear(); m_error.clear(); clearDetails(); updateDays();
    m_timer.stop();
    if (!path.isEmpty()) {
        if (!QDir::isAbsolutePath(path)) m_error = tr("Calendar requires an absolute Society container path.");
        else {
            QString error;
            const auto drive = iiSocietyContainer::SocietyDrive::open(path, &error);
            if (!drive || !drive->isReady()) m_error = error.isEmpty() ? tr("Calendar is waiting for the Society container.") : error;
            else {
                m_driveIdentifier = drive->identifier();
                QString base = qEnvironmentVariable("SOCIETY_CALENDAR_DIRECTORY");
                if (base.isEmpty()) base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/Calendar";
                const auto key = QCryptographicHash::hash(drive->identifier().toUtf8(), QCryptographicHash::Sha256).toHex();
                if (!QDir::isAbsolutePath(base) || !QDir().mkpath(base)) m_error = tr("Cannot create the local calendar directory.");
                else m_databasePath = QDir(base).filePath(QString::fromLatin1(key) + ".sqlite");
            }
        }
    }
    emit containerPathChanged(); emit changed();
    if (!m_databasePath.isEmpty()) { if (m_active) m_timer.start(); refresh(); }
}
void DashboardCalendar::setTimeZone(const QString &zone) {
    try {
        const TimeZone next(zone.toStdString()); if (next.id() == m_zone.id()) return;
        m_zone = next; ++m_generation; clearDetails(); updateDays(); emit configurationChanged(); refresh();
    } catch (const std::exception &e) { fail(e); }
}
void DashboardCalendar::setCalendarSystem(const QString &system) {
    try {
        m_calendars.get(system.toStdString()); if (system == m_calendarSystem) return;
        m_calendarSystem = system; ++m_generation; updateDays(); emit configurationChanged(); refresh();
    } catch (const std::exception &e) { fail(e); }
}
void DashboardCalendar::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    if (active && !m_databasePath.isEmpty()) { m_timer.start(); refresh(); } else m_timer.stop();
    emit activeChanged();
}
void DashboardCalendar::selectDate(const QString &isoDate) {
    try {
        auto date = Date::parse(isoDate.toStdString());
        // Presentation labels use the common civil range; iiCalendar itself supports wider years.
        if (date.year() < 1 || date.year() > 9999) throw std::out_of_range("dashboard date must be between years 1 and 9999");
        m_selected = date; ++m_generation; clearDetails(); updateDays(); refresh();
    } catch (const std::exception &e) { fail(e); }
}
void DashboardCalendar::moveDay(int delta) {
    try { selectDate(string(m_selected.addDays(delta).toString())); } catch (const std::exception &e) { fail(e); }
}
void DashboardCalendar::moveMonth(int delta) {
    try {
        const auto &calendar = m_calendars.get(m_calendarSystem.toStdString());
        selectDate(string(Date::fromEpochDay(calendar.toEpochDay(calendar.addMonths(
            calendar.fromEpochDay(m_selected.epochDay()), delta, MonthOverflow::Clamp))).toString()));
    } catch (const std::exception &e) { fail(e); }
}
void DashboardCalendar::goToday() { selectDate(string(m_zone.at(Clock::sample().wallTime).local.date.toString())); }
QString DashboardCalendar::monthTitle() const {
    if (m_calendarSystem == "gregorian") return QLocale(QLocale::English).toString(
        QDate(m_selected.year(), m_selected.month(), m_selected.day()), "MMMM yyyy");
    const auto cd = m_calendars.get(m_calendarSystem.toStdString()).fromEpochDay(m_selected.epochDay());
    return tr("%1 · %2 / %3%4").arg(m_calendarSystem).arg(cd.year).arg(cd.month).arg(cd.leapMonth ? tr(" (leap)") : "");
}
QString DashboardCalendar::dayTitle() const {
    return QLocale(QLocale::English).toString(QDate(m_selected.year(), m_selected.month(), m_selected.day()), "ddd, MMM d");
}
QString DashboardCalendar::summary() const {
    return tr("%1 events · %2 open tasks · %3 files").arg(m_events.size()).arg(m_tasks.size() - m_completedTasks).arg(m_files.size());
}
void DashboardCalendar::refresh() {
    if (m_loading) { m_pending = true; return; }
    if (!m_containerPath.isEmpty() && QDir::isAbsolutePath(m_containerPath)) {
        const auto drive = iiSocietyContainer::SocietyDrive::open(m_containerPath);
        if (m_databasePath.isEmpty() || !drive || !drive->isReady() || drive->identifier() != m_driveIdentifier) {
            const auto path = m_containerPath;
            m_containerPath.clear();
            setContainerPath(path);
            return;
        }
    }
    if (m_databasePath.isEmpty()) { m_loading = false; emit changed(); return; }
    m_loading = true; m_error.clear(); emit changed();
    const auto path = m_databasePath.toStdString(), system = m_calendarSystem.toStdString();
    const auto date = m_selected; const auto zone = m_zone; const auto generation = m_generation;
    m_watcher.setFuture(QtConcurrent::run([path, system, date, zone, generation] {
        Snapshot result; result.generation = generation;
        try {
            CalendarRegistry registry;
            ViewRequest request; request.anchor = date; request.calendarSystem = system;
            request.today = zone.at(Clock::sample().wallTime).local.date;
            const auto view = makeView(request, registry);
            result.days = dayRows(view, date);
            Store store(path);
            const auto start = zone.dayRange(view.days.front().date).start;
            const auto end = zone.dayRange(view.days.back().date).end;
            const auto scheduled = store.scheduled({start, end});
            for (qsizetype i = 0; i < result.days.size(); ++i) {
                const auto range = zone.dayRange(view.days[static_cast<std::size_t>(i)].date);
                int count = 0;
                if (range.start != range.end)
                    for (const auto &item : scheduled) if (item.occurrence.range.overlaps(range)) ++count;
                auto cell = result.days[i].toMap(); cell["count"] = count; result.days[i] = cell;
            }
            const auto details = dayDetails(store, date, zone, 10000);
            result.events = rows(details.events, zone); result.tasks = rows(details.tasks, zone);
            result.activity = rows(details.activity, zone); result.notes = rows(details.notes, zone);
            result.reminders = rows(details.reminders, zone); result.completedTasks = static_cast<int>(details.completedTasks);
            for (const auto &file : details.files) result.files.append(QVariantMap{
                {"id", string(file.id)}, {"title", string(file.name)}, {"uri", string(file.uri)},
                {"subtitle", string(file.mediaType) + " · " + QLocale(QLocale::English).formattedDataSize(file.byteSize)},
                {"byteSize", QString::number(file.byteSize)}});
        } catch (const std::exception &e) { result.error = string(e.what()); }
        return result;
    }));
}
bool DashboardCalendar::setTaskCompleted(const QString &id, const QString &revision, bool completed) {
    try {
        if (m_databasePath.isEmpty()) throw std::runtime_error("Calendar storage is unavailable");
        const auto drive = iiSocietyContainer::SocietyDrive::open(m_containerPath);
        if (!drive || !drive->isReady() || drive->identifier() != m_driveIdentifier)
            throw std::runtime_error("The Society container has changed; refresh the calendar");
        bool ok = false; const auto version = revision.toULongLong(&ok);
        if (!ok) throw std::invalid_argument("Invalid task revision");
        Store store(m_databasePath.toStdString()); auto event = store.get(id.toStdString());
        if (!event || event->kind != EventKind::Task) throw std::invalid_argument("Task no longer exists");
        // The SDK currently has series-level status, not per-occurrence completion.
        if (event->recurrence.frequency != Frequency::None || !event->recurrence.additional.empty())
            throw std::invalid_argument("Repeating tasks cannot be completed per occurrence yet");
        event->status = completed ? EventStatus::Completed : EventStatus::Confirmed;
        event->percentComplete = completed ? 100 : 0; event->updated = Clock::sample().wallTime;
        event->completed = completed ? std::optional(event->updated) : std::nullopt;
        store.put(*event, version); ++m_generation; refresh(); return true;
    } catch (const std::exception &e) { fail(e); return false; }
}
QString DashboardCalendar::attachmentPath(const QString &uri) const {
    if (m_containerPath.isEmpty()) return {};
    const auto drive = iiSocietyContainer::SocietyDrive::open(m_containerPath);
    if (!drive || !drive->isReady()) return {};
    const QUrl url(uri);
    QString path;
    if (url.isLocalFile()) path = url.toLocalFile();
    else if (url.scheme().isEmpty()) path = QDir::isAbsolutePath(uri) ? uri : drive->resolvePath(uri);
    else return {};
    const QFileInfo file(path);
    if (!file.isFile() || file.isSymLink()) return {};
    const auto canonical = file.canonicalFilePath();
    if (canonical.isEmpty() || !drive->sectionForPath(canonical)) return {};
    return canonical;
}
