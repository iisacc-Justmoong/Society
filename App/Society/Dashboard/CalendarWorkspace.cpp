#include "CalendarWorkspace.h"
#include <QCryptographicHash>
#include <QDate>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QUuid>
#include <QtConcurrentRun>
#include <SocietyDrive.h>
#include <algorithm>
#include <map>

using namespace iiCalendar;
namespace {
QString q(const std::string &s) { return QString::fromStdString(s); }
QString dateText(Date d, const char *format) {
  return QLocale(QLocale::English)
      .toString(QDate(d.year(), d.month(), d.day()),
                QString::fromLatin1(format));
}
QString clockText(Time t) {
  return QStringLiteral("%1:%2")
      .arg(t.hour(), 2, 10, QChar('0'))
      .arg(t.minute(), 2, 10, QChar('0'));
}
QString localText(Instant t, const TimeZone &z) {
  const auto v = z.at(t).local;
  return q(v.date.toString()) + " " + clockText(v.time);
}
QString repeatName(Frequency f) {
  switch (f) {
  case Frequency::Daily:
    return "daily";
  case Frequency::Weekly:
    return "weekly";
  case Frequency::Monthly:
    return "monthly";
  case Frequency::Yearly:
    return "yearly";
  default:
    return "none";
  }
}
Frequency frequency(const QString &v) {
  if (v == "daily")
    return Frequency::Daily;
  if (v == "weekly")
    return Frequency::Weekly;
  if (v == "monthly")
    return Frequency::Monthly;
  if (v == "yearly")
    return Frequency::Yearly;
  if (v == "none")
    return Frequency::None;
  throw std::invalid_argument("Unsupported repeat interval");
}
QString collectionName(const Event &e) {
  if (const auto it = e.properties.find("calendar.name");
      it != e.properties.end())
    return q(it->second);
  auto name = q(e.calendarId);
  if (name == "default" || name.isEmpty())
    return QObject::tr("Personal");
  name[0] = name[0].toUpper();
  return name;
}
QString eventColor(const Event &e) {
  const auto color = q(e.color);
  if (QStringList{"green", "purple", "blue", "orange"}.contains(color))
    return color;
  const auto id = q(e.calendarId).toLower();
  return id.contains("publish")   ? "purple"
         : id.contains("project") ? "blue"
         : id == "personal"       ? "orange"
                                  : "green";
}
QVariantMap metadata(const Event &e) {
  QVariantList guests, attachments;
  QStringList names;
  for (const auto &p : e.participants) {
    names << q(p.name.empty() ? p.address : p.name);
    guests.append(QVariantMap{
        {"id", q(p.id)},
        {"name", q(p.name.empty() ? p.address : p.name)},
        {"address", q(p.address)},
        {"response", p.response == Participation::Accepted    ? "Accepted"
                     : p.response == Participation::Declined  ? "Declined"
                     : p.response == Participation::Tentative ? "Tentative"
                                                              : "Pending"}});
  }
  for (const auto &a : e.attachments)
    attachments.append(
        QVariantMap{{"id", q(a.id)}, {"name", q(a.name)}, {"uri", q(a.uri)}});
  QVariantMap result{
      {"id", q(e.id)},
      {"revision", QString::number(e.revision)},
      {"title", q(e.title)},
      {"description", q(e.description)},
      {"calendarId", q(e.calendarId)},
      {"calendarName", collectionName(e)},
      {"colorKey", eventColor(e)},
      {"kind", e.kind == EventKind::Task ? "task" : "event"},
      {"location", q(e.location.name)},
      {"meetingUrl", q(e.location.uri.empty() ? e.url : e.location.uri)},
      {"organizer", e.ownerId.empty() ? QObject::tr("You") : q(e.ownerId)},
      {"invitees", names.join("; ")},
      {"guests", guests},
      {"attachments", attachments},
      {"repeat", repeatName(e.recurrence.frequency)},
      {"alarmMinutes", e.alarms.empty()
                           ? -1
                           : int(e.alarms.front().beforeStart.seconds() / 60)},
      {"busy", e.busy},
      {"timeZone", q(e.zone())}};
  if (const auto *t = std::get_if<TimedSchedule>(&e.schedule)) {
    const TimeZone zone(t->zone);
    result["startLocal"] = localText(t->range.start, zone);
    result["endLocal"] = localText(t->range.end, zone);
    result["allDay"] = false;
  } else if (const auto *d = std::get_if<AllDaySchedule>(&e.schedule)) {
    result["startLocal"] = q(d->start.toString());
    result["endLocal"] = q(d->end.addDays(-1).toString());
    result["allDay"] = true;
  } else if (e.due) {
    const TimeZone zone(e.zone());
    result["startLocal"] = localText(*e.due, zone);
    result["endLocal"] = localText(*e.due + Duration(3600), zone);
    result["allDay"] = false;
  }
  return result;
}
LocalDateTime parseLocal(QString text) {
  text = text.trimmed();
  const auto date = Date::parse(text.left(10).toStdString());
  const auto tail =
      text.size() > 10 ? text.mid(11).trimmed() : QString("09:00");
  return {date,
          Time::parse((tail.size() == 5 ? tail + ":00" : tail).toStdString())};
}
QVariantList cells(const CalendarView &view, Date selected) {
  QVariantList result;
  for (const auto &c : view.days)
    result.append(
        QVariantMap{{"date", q(c.date.toString())},
                    {"day", c.calendarDate.day},
                    {"weekday", int(c.weekday)},
                    {"weekdayName", dateText(c.date, "ddd").toUpper()},
                    {"inMonth", c.inPeriod},
                    {"today", c.today},
                    {"selected", c.date == selected}});
  return result;
}
QVariantList projected(const std::vector<ScheduledEvent> &scheduled,
                       const CalendarView &view, const TimeZone &zone) {
  QVariantList result;
  for (const auto &item : scheduled) {
    if (item.event.kind != EventKind::Event ||
        item.event.status == EventStatus::Cancelled)
      continue;
    const auto occurrence = item.occurrence.range;
    const auto base = metadata(item.event);
    const auto first = std::max<std::int64_t>(
        0, zone.at(occurrence.start).local.date.epochDay() -
               view.days.front().date.epochDay());
    const auto last =
        std::min<std::int64_t>(std::int64_t(view.days.size()) - 1,
                               zone.at(occurrence.end).local.date.epochDay() -
                                   view.days.front().date.epochDay());
    for (auto day = first; day <= last; ++day) {
      const auto d = view.days[day].date;
      const auto bounds = zone.dayRange(d);
      if (!occurrence.overlaps(bounds))
        continue;
      auto row = base;
      row["date"] = q(d.toString());
      row["dayIndex"] = int(day);
      row["start"] = q(occurrence.start.toString());
      row["end"] = q(occurrence.end.toString());
      row["occurrenceKey"] =
          q(item.event.id) + "@" + q(occurrence.start.toString());
      row["allDay"] = item.occurrence.allDay;
      const auto start = std::max(bounds.start, occurrence.start),
                 end = std::min(bounds.end, occurrence.end);
      const int first = int(zone.at(start).local.time.secondOfDay() / 60);
      const int last = end == bounds.end
                           ? 1440
                           : int(zone.at(end).local.time.secondOfDay() / 60);
      row["startMinute"] = first;
      row["endMinute"] = std::max(first + 1, last);
      row["time"] = item.occurrence.allDay
                        ? QObject::tr("All day")
                        : clockText(zone.at(occurrence.start).local.time) +
                              "–" +
                              clockText(zone.at(occurrence.end).local.time);
      row["when"] =
          dateText(zone.at(occurrence.start).local.date, "dddd, MMMM d, yyyy");
      result.append(row);
    }
  }
  return result;
}
} // namespace
CalendarWorkspace::CalendarWorkspace(QObject *parent) : QObject(parent) {
  m_debounce.setSingleShot(true);
  m_debounce.setInterval(10);
  connect(&m_debounce, &QTimer::timeout, this, &CalendarWorkspace::load);
  connect(&m_watcher, &QFutureWatcher<Snapshot>::finished, this, [this] {
    const auto data = m_watcher.result();
    m_loading = false;
    if (data.generation == m_generation && m_active) {
      m_events = data.events;
      m_upcoming = data.upcoming;
      m_calendarRows = data.calendars;
      m_loadError = data.error;
      updateSelection();
    }
    emit changed();
    if (m_pending) {
      m_pending = false;
      refresh();
    }
  });
}
CalendarWorkspace::~CalendarWorkspace() { m_watcher.waitForFinished(); }
void CalendarWorkspace::setCalendar(DashboardCalendar *calendar) {
  if (m_calendar == calendar)
    return;
  if (m_calendar)
    disconnect(m_calendar, nullptr, this, nullptr);
  m_calendar = calendar;
  ++m_generation;
  m_events.clear();
  m_upcoming.clear();
  m_calendarRows.clear();
  m_selected.clear();
  m_selectedId.clear();
  m_hidden.clear();
  m_error.clear();
  m_loadError.clear();
  if (calendar) {
    connect(calendar, &DashboardCalendar::changed, this, [this] {
      if (m_calendar && !m_calendar->loading())
        refresh();
      else
        emit changed();
    });
    connect(calendar, &DashboardCalendar::containerPathChanged, this, [this] {
      ++m_generation;
      m_events.clear();
      m_upcoming.clear();
      m_calendarRows.clear();
      m_selected.clear();
      m_selectedId.clear();
      m_hidden.clear();
      m_error.clear();
      m_loadError.clear();
      refresh();
    });
  }
  updateFrames();
  emit calendarChanged();
  emit changed();
  refresh();
}
void CalendarWorkspace::setActive(bool value) {
  if (m_active == value)
    return;
  m_active = value;
  ++m_generation;
  if (value)
    refresh();
  else
    m_debounce.stop();
  emit activeChanged();
}
void CalendarWorkspace::setViewMode(const QString &mode) {
  if (!QStringList{"day", "week", "month", "year"}.contains(mode) ||
      m_mode == mode)
    return;
  m_mode = mode;
  ++m_generation;
  updateFrames();
  emit changed();
  refresh();
}
void CalendarWorkspace::updateFrames() {
  if (!m_calendar)
    return;
  const auto date = Date::parse(m_calendar->selectedDate().toStdString());
  const TimeZone zone(m_calendar->timeZone().toStdString());
  CalendarRegistry registry;
  ViewRequest request;
  request.anchor = date;
  request.today = zone.at(Clock::sample().wallTime).local.date;
  const auto key =
      m_mode + q(date.toString()) + q(zone.id()) + q(request.today->toString());
  if (key == m_frameKey)
    return;
  m_frameKey = key;
  request.kind = m_mode == "day"    ? ViewKind::Day
                 : m_mode == "week" ? ViewKind::Week
                 : m_mode == "year" ? ViewKind::Year
                                    : ViewKind::Month;
  const auto view = makeView(request, registry);
  m_days = cells(view, date);
  const auto last = view.end.addDays(-1);
  const auto weekTitle =
      dateText(view.start,
               view.start.year() != last.year() ? "MMMM d, yyyy" : "MMMM d") +
      "–" +
      dateText(last,
               view.start.month() == last.month() ? "d, yyyy" : "MMMM d, yyyy");
  m_periodTitle = m_mode == "day"     ? dateText(date, "dddd, MMMM d")
                  : m_mode == "year"  ? QString::number(date.year())
                  : m_mode == "month" ? dateText(date, "MMMM yyyy")
                                      : weekTitle;
  m_yearMonths.clear();
  for (unsigned month = 1; m_mode == "year" && month <= 12; ++month) {
    request.kind = ViewKind::Month;
    request.anchor = Date(date.year(), month, 1);
    m_yearMonths.append(
        QVariantMap{{"title", dateText(request.anchor, "MMMM")},
                    {"date", q(request.anchor.toString())},
                    {"days", cells(makeView(request, registry), date)}});
  }
}
void CalendarWorkspace::refresh() {
  updateFrames();
  if (!m_active || !m_calendar)
    return;
  ++m_generation;
  if (m_loading) {
    m_pending = true;
    return;
  }
  m_debounce.start();
  emit changed();
}
void CalendarWorkspace::load() {
  if (!m_active || !m_calendar || m_calendar->loading())
    return;
  if (m_calendar->databasePath().isEmpty()) {
    m_loadError = m_calendar->errorString();
    emit changed();
    return;
  }
  const auto path = m_calendar->databasePath().toStdString(),
             dateTextValue = m_calendar->selectedDate().toStdString(),
             zoneName = m_calendar->timeZone().toStdString();
  const auto mode = m_mode;
  const auto generation = m_generation;
  m_loading = true;
  emit changed();
  m_watcher.setFuture(
      QtConcurrent::run([path, dateTextValue, zoneName, mode, generation] {
        Snapshot data;
        data.generation = generation;
        try {
          Store store(path);
          CalendarRegistry registry;
          const TimeZone zone(zoneName);
          const auto date = Date::parse(dateTextValue);
          ViewRequest request;
          request.anchor = date;
          request.kind = mode == "day"    ? ViewKind::Day
                         : mode == "week" ? ViewKind::Week
                         : mode == "year" ? ViewKind::Year
                                          : ViewKind::Month;
          const auto view = makeView(request, registry);
          data.events = projected(
              store.scheduled({zone.dayRange(view.days.front().date).start,
                               zone.dayRange(view.days.back().date).end}),
              view, zone);
          request.kind = ViewKind::Agenda;
          request.spanDays = 7;
          const auto upcoming = makeView(request, registry);
          const auto upcomingRows = projected(
              store.scheduled({zone.dayRange(upcoming.start).start,
                               zone.dayRange(upcoming.end.addDays(-1)).end}),
              upcoming, zone);
          QSet<QString> seen;
          for (const auto &v : upcomingRows) {
            const auto row = v.toMap();
            const auto key = row.value("occurrenceKey").toString();
            if (!seen.contains(key)) {
              seen.insert(key);
              data.upcoming.append(row);
            }
          }
          std::map<QString, QVariantMap> collections;
          collections.emplace("default",
                              QVariantMap{{"id", "default"},
                                          {"name", QObject::tr("Personal")},
                                          {"colorKey", "green"}});
          EventQuery query;
          query.limit = 10000;
          const auto all = store.query(query);
          if (all.size() == query.limit)
            throw std::runtime_error(
                "Calendar collection scan exceeds its 10000 item limit");
          for (const auto &e : all)
            collections[q(e.calendarId)] = {{"id", q(e.calendarId)},
                                            {"name", collectionName(e)},
                                            {"colorKey", eventColor(e)}};
          for (const auto &[id, value] : collections) {
            Q_UNUSED(id);
            data.calendars.append(value);
          }
        } catch (const std::exception &e) {
          data.error = q(e.what());
        }
        return data;
      }));
}
QVariantList CalendarWorkspace::visibleEvents() const {
  QVariantList result;
  for (const auto &v : m_events)
    if (!m_hidden.contains(v.toMap().value("calendarId").toString()))
      result.append(v);
  return result;
}
QVariantList CalendarWorkspace::upcoming() const {
  QVariantList result;
  for (const auto &v : m_upcoming)
    if (!m_hidden.contains(v.toMap().value("calendarId").toString()))
      result.append(v);
  return result;
}
QVariantList CalendarWorkspace::calendars() const {
  auto rows = m_calendarRows;
  if (rows.isEmpty())
    rows.append(QVariantMap{
        {"id", "default"}, {"name", tr("Personal")}, {"colorKey", "green"}});
  for (auto &v : rows) {
    auto row = v.toMap();
    row["visible"] = !m_hidden.contains(row.value("id").toString());
    v = row;
  }
  return rows;
}
void CalendarWorkspace::setCalendarVisible(const QString &id, bool visible) {
  if (visible)
    m_hidden.remove(id);
  else
    m_hidden.insert(id);
  updateSelection();
  emit changed();
}
void CalendarWorkspace::updateSelection() {
  const auto events = visibleEvents();
  m_selected.clear();
  for (const auto &v : events) {
    const auto row = v.toMap();
    if (row.value("id").toString() == m_selectedId &&
        (m_selectedStart.isEmpty() ||
         row.value("start").toString() == m_selectedStart)) {
      m_selected = row;
      break;
    }
  }
  // Upcoming can belong to the next period. Keep its selection while the
  // calendar's new date is being loaded instead of replacing it with an old
  // row.
  if (m_selected.isEmpty()) {
    for (const auto &v : upcoming()) {
      const auto row = v.toMap();
      if (row.value("id").toString() == m_selectedId &&
          (m_selectedStart.isEmpty() ||
           row.value("start").toString() == m_selectedStart)) {
        m_selected = row;
        break;
      }
    }
  }
  if (m_selected.isEmpty() && !events.isEmpty()) {
    for (const auto &v : events)
      if (v.toMap().value("date").toString() == m_calendar->selectedDate()) {
        m_selected = v.toMap();
        break;
      }
    if (m_selected.isEmpty())
      m_selected = events.first().toMap();
  }
  m_selectedId = m_selected.value("id").toString();
  m_selectedStart = m_selected.value("start").toString();
}
void CalendarWorkspace::selectEvent(const QString &id, const QString &start) {
  m_selectedId = id;
  m_selectedStart = start;
  updateSelection();
  emit changed();
}
void CalendarWorkspace::movePeriod(int delta) {
  if (!m_calendar)
    return;
  if (m_mode == "day")
    m_calendar->moveDay(delta);
  else if (m_mode == "week")
    m_calendar->moveDay(delta * 7);
  else
    m_calendar->moveMonth(delta * (m_mode == "year" ? 12 : 1));
  updateFrames();
  emit changed();
}
QString CalendarWorkspace::checkedDatabase() const {
  if (!m_calendar || m_calendar->databasePath().isEmpty())
    throw std::runtime_error("Calendar storage is unavailable");
  const auto drive =
      iiSocietyContainer::SocietyDrive::open(m_calendar->containerPath());
  const auto expected =
      drive ? QString::fromLatin1(
                  QCryptographicHash::hash(drive->identifier().toUtf8(),
                                           QCryptographicHash::Sha256)
                      .toHex()) +
                  ".sqlite"
            : QString();
  if (!drive || !drive->isReady() ||
      QFileInfo(m_calendar->databasePath()).fileName() != expected)
    throw std::runtime_error(
        "The Society container has changed; refresh the calendar");
  return m_calendar->databasePath();
}
QVariantMap CalendarWorkspace::eventDraft(const QString &id) const {
  if (id.isEmpty()) {
    const auto date = m_calendar ? m_calendar->selectedDate() : QString();
    return {{"storageKey",
             m_calendar ? QFileInfo(m_calendar->databasePath()).fileName()
                        : QString()},
            {"id", ""},
            {"revision", "0"},
            {"title", ""},
            {"startLocal", date + " 09:00"},
            {"endLocal", date + " 10:00"},
            {"timeZone", m_calendar ? m_calendar->timeZone() : "UTC"},
            {"calendarId", "default"},
            {"kind", "event"},
            {"allDay", false},
            {"busy", true},
            {"repeat", "none"},
            {"alarmMinutes", -1},
            {"description", ""},
            {"location", ""},
            {"invitees", ""},
            {"meetingUrl", ""}};
  }
  try {
    Store store(checkedDatabase().toStdString());
    const auto e = store.get(id.toStdString());
    auto draft = e ? metadata(*e) : QVariantMap();
    if (e)
      draft["storageKey"] = QFileInfo(m_calendar->databasePath()).fileName();
    return draft;
  } catch (...) {
    return {};
  }
}
QString CalendarWorkspace::todayDate() const {
  return q(TimeZone(m_calendar ? m_calendar->timeZone().toStdString() : "UTC")
               .at(Clock::sample().wallTime)
               .local.date.toString());
}
int CalendarWorkspace::currentMinute() const {
  return int(TimeZone(m_calendar ? m_calendar->timeZone().toStdString() : "UTC")
                 .at(Clock::sample().wallTime)
                 .local.time.secondOfDay() /
             60);
}
QString CalendarWorkspace::zoneOffset() const {
  const auto offset =
      TimeZone(m_calendar ? m_calendar->timeZone().toStdString() : "UTC")
          .at(Clock::sample().wallTime)
          .utcOffsetSeconds /
      60;
  return QString("GMT%1%2%3")
      .arg(offset < 0 ? "−" : "+")
      .arg(std::abs(offset) / 60)
      .arg(offset % 60
               ? QString(":%1").arg(std::abs(offset) % 60, 2, 10, QChar('0'))
               : "");
}
QVariantMap CalendarWorkspace::quickDraft(const QString &text) {
  try {
    if (!m_calendar)
      throw std::invalid_argument("Choose a calendar first");
    auto draft = eventDraft();
    auto title = text.trimmed();
    auto date = Date::parse(m_calendar->selectedDate().toStdString());
    const auto take = [&title](const QRegularExpression &pattern) {
      const auto match = pattern.match(title);
      if (match.hasMatch())
        title.remove(match.capturedStart(), match.capturedLength());
      return match;
    };
    const auto collection =
        take(QRegularExpression("/([\\p{L}\\p{N}_-]+)\\s*$"));
    if (collection.hasMatch()) {
      bool found = false;
      for (const auto &v : calendars()) {
        const auto row = v.toMap();
        if (row.value("name").toString().compare(collection.captured(1),
                                                 Qt::CaseInsensitive) == 0 ||
            row.value("id").toString().compare(collection.captured(1),
                                               Qt::CaseInsensitive) == 0) {
          draft["calendarId"] = row.value("id");
          found = true;
          break;
        }
      }
      if (!found)
        throw std::invalid_argument(
            "Choose an existing calendar after the slash");
    }
    const auto iso = take(QRegularExpression("\\b(\\d{4}-\\d{2}-\\d{2})\\b"));
    if (iso.hasMatch())
      date = Date::parse(iso.captured(1).toStdString());
    else {
      const auto day = take(QRegularExpression(
          "\\b(today|tomorrow|mon(?:day)?|tue(?:sday)?|wed(?:nesday)?|thu(?:"
          "rsday)?|fri(?:day)?|sat(?:urday)?|sun(?:day)?)\\b",
          QRegularExpression::CaseInsensitiveOption));
      if (day.hasMatch()) {
        const auto token = day.captured(1).toLower();
        if (token == "today" || token == "tomorrow")
          date = Date::parse(todayDate().toStdString())
                     .addDays(token == "tomorrow" ? 1 : 0);
        else {
          const QStringList names{"mon", "tue", "wed", "thu",
                                  "fri", "sat", "sun"};
          const int weekday = int(date.weekday()) - 1;
          date = date.addDays((names.indexOf(token.left(3)) - weekday + 7) % 7);
        }
      }
    }
    const auto time =
        take(QRegularExpression("\\b(\\d{1,2})(?::(\\d{2}))?\\s*(am|pm)?\\s*[-"
                                "–]\\s*(\\d{1,2})(?::(\\d{2}))?\\s*(am|pm)?\\b",
                                QRegularExpression::CaseInsensitiveOption));
    QString start = "09:00", end = "10:00";
    if (time.hasMatch()) {
      const auto clock = [](QString h, QString m, QString suffix) {
        int hour = h.toInt(), minute = m.isEmpty() ? 0 : m.toInt();
        suffix = suffix.toLower();
        if (!suffix.isEmpty()) {
          if (hour < 1 || hour > 12)
            throw std::invalid_argument("Use 1–12 with am or pm");
          hour = hour % 12 + (suffix == "pm" ? 12 : 0);
        }
        return clockText(Time(hour, minute));
      };
      start = clock(time.captured(1), time.captured(2),
                    time.captured(3).isEmpty() ? time.captured(6)
                                               : time.captured(3));
      end = clock(time.captured(4), time.captured(5), time.captured(6));
    }
    draft["title"] = title.simplified();
    draft["startLocal"] = q(date.toString()) + " " + start;
    draft["endLocal"] =
        q(date.addDays(end <= start ? 1 : 0).toString()) + " " + end;
    m_error.clear();
    emit changed();
    return draft;
  } catch (const std::exception &e) {
    m_error = q(e.what());
    emit changed();
    return {};
  }
}
QString CalendarWorkspace::saveEvent(const QVariantMap &draft) {
  try {
    const auto path = checkedDatabase();
    if (draft.contains("storageKey") &&
        draft.value("storageKey").toString() != QFileInfo(path).fileName())
      throw std::invalid_argument("The Society container changed; reopen this "
                                  "event in the current container");
    Store store(path.toStdString());
    const auto id = draft.value("id").toString();
    Event event;
    const bool editing = !id.isEmpty();
    qulonglong version = 0;
    if (editing) {
      bool ok = false;
      version = draft.value("revision").toString().toULongLong(&ok);
      if (!ok)
        throw std::invalid_argument("Invalid event revision");
      const auto old = store.get(id.toStdString());
      if (!old)
        throw std::invalid_argument("Event no longer exists");
      event = *old;
    } else {
      event.id =
          QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
      event.created = Clock::sample().wallTime;
    }
    event.title = draft.value("title").toString().trimmed().toStdString();
    if (event.title.empty())
      throw std::invalid_argument("Enter an event title");
    const auto start = parseLocal(draft.value("startLocal").toString()),
               end = parseLocal(draft.value("endLocal").toString());
    const TimeZone zone(draft.value("timeZone", m_calendar->timeZone())
                            .toString()
                            .toStdString());
    const auto before = metadata(event);
    const bool scheduleChanged =
        !editing || draft.value("startLocal") != before.value("startLocal") ||
        draft.value("endLocal") != before.value("endLocal") ||
        draft.value("timeZone") != before.value("timeZone") ||
        draft.value("allDay", false).toBool() !=
            before.value("allDay").toBool();
    if (scheduleChanged) {
      if (draft.value("allDay").toBool())
        event.schedule =
            AllDaySchedule{start.date, end.date.addDays(1), zone.id()};
      else
        event.schedule =
            TimedSchedule{{zone.resolve(start), zone.resolve(end)}, zone.id()};
    }
    const auto kind = draft.value("kind", "event").toString();
    if (kind != "task" && kind != "event")
      throw std::invalid_argument("Choose Event or Task");
    const auto previousKind = event.kind;
    event.kind = kind == "task" ? EventKind::Task : EventKind::Event;
    if (event.kind == EventKind::Task &&
        (scheduleChanged || previousKind != event.kind))
      event.due = zone.resolve(start);
    else if (event.kind != EventKind::Task)
      event.due.reset();
    event.calendarId =
        draft.value("calendarId", "default").toString().toStdString();
    if (event.calendarId.empty())
      event.calendarId = "default";
    event.description = draft.value("description").toString().toStdString();
    event.location.name = draft.value("location").toString().toStdString();
    event.location.uri = draft.value("meetingUrl").toString().toStdString();
    event.busy = draft.value("busy", true).toBool();
    if (draft.contains("colorKey"))
      event.color = draft.value("colorKey").toString().toStdString();
    const auto nextFrequency =
        frequency(draft.value("repeat", "none").toString());
    if (nextFrequency != event.recurrence.frequency) {
      event.recurrence = {};
      event.recurrence.frequency = nextFrequency;
    }
    const int alarm = draft.value("alarmMinutes", -1).toInt();
    if (alarm > 525600 || alarm < -1)
      throw std::invalid_argument("Invalid alert interval");
    if (!editing || alarm != before.value("alarmMinutes").toInt()) {
      event.alarms.clear();
      if (alarm >= 0)
        event.alarms.push_back(
            {Duration(qint64(alarm) * 60), "display", event.title});
    }
    const auto invitees =
        draft.value("invitees")
            .toString()
            .split(QRegularExpression("[;,·]"), Qt::SkipEmptyParts);
    std::vector<Participant> guests;
    for (const auto &raw : invitees) {
      const auto name = raw.trimmed();
      if (name.isEmpty())
        continue;
      auto old =
          std::find_if(event.participants.begin(), event.participants.end(),
                       [&](const Participant &p) {
                         return q(p.name) == name || q(p.address) == name;
                       });
      if (old != event.participants.end())
        guests.push_back(*old);
      else {
        Participant p;
        p.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        p.name = name.toStdString();
        if (name.contains('@'))
          p.address = p.name;
        guests.push_back(p);
      }
    }
    event.participants = std::move(guests);
    event.updated = Clock::sample().wallTime;
    const auto saved =
        store.put(event, editing ? std::optional<std::uint64_t>(version)
                                 : std::optional<std::uint64_t>(0));
    m_error.clear();
    m_selectedId = q(saved.id);
    m_selectedStart.clear();
    auto displayDate = start.date;
    const TimeZone displayZone(m_calendar->timeZone().toStdString());
    if (const auto range = saved.range())
      displayDate = displayZone.at(range->start).local.date;
    else if (saved.due)
      displayDate = displayZone.at(*saved.due).local.date;
    m_calendar->selectDate(q(displayDate.toString()));
    refresh();
    emit changed();
    return q(saved.id);
  } catch (const std::exception &e) {
    m_error = q(e.what());
    emit changed();
    return {};
  }
}
bool CalendarWorkspace::removeEvent(const QString &id,
                                    const QString &revision) {
  try {
    bool ok = false;
    const auto version = revision.toULongLong(&ok);
    if (!ok)
      throw std::invalid_argument("Invalid event revision");
    Store store(checkedDatabase().toStdString());
    if (!store.erase(id.toStdString(), version))
      throw std::invalid_argument("Event no longer exists");
    m_error.clear();
    if (m_selectedId == id) {
      m_selectedId.clear();
      m_selected.clear();
    }
    m_calendar->refresh();
    refresh();
    return true;
  } catch (const std::exception &e) {
    m_error = q(e.what());
    emit changed();
    return false;
  }
}
