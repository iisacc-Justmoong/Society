#pragma once
#include "DashboardCalendar.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVariantMap>

// GUI projection only. Dates, recurrence, validation and durable writes belong
// to iiCalendar.
class CalendarWorkspace : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(DashboardCalendar *calendar READ calendar WRITE setCalendar NOTIFY
                 calendarChanged)
  Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
  Q_PROPERTY(QString viewMode READ viewMode WRITE setViewMode NOTIFY changed)
  Q_PROPERTY(QString periodTitle READ periodTitle NOTIFY changed)
  Q_PROPERTY(QVariantList periodDays READ periodDays NOTIFY changed)
  Q_PROPERTY(QVariantList yearMonths READ yearMonths NOTIFY changed)
  Q_PROPERTY(QVariantList visibleEvents READ visibleEvents NOTIFY changed)
  Q_PROPERTY(QVariantList upcoming READ upcoming NOTIFY changed)
  Q_PROPERTY(QVariantList calendars READ calendars NOTIFY changed)
  Q_PROPERTY(QVariantMap selectedEvent READ selectedEvent NOTIFY changed)
  Q_PROPERTY(bool loading READ loading NOTIFY changed)
  Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
  Q_PROPERTY(QString todayDate READ todayDate NOTIFY changed)
  Q_PROPERTY(int currentMinute READ currentMinute NOTIFY changed)
  Q_PROPERTY(QString zoneOffset READ zoneOffset NOTIFY changed)
public:
  explicit CalendarWorkspace(QObject *parent = nullptr);
  ~CalendarWorkspace() override;
  DashboardCalendar *calendar() const { return m_calendar; }
  void setCalendar(DashboardCalendar *calendar);
  bool active() const { return m_active; }
  void setActive(bool active);
  QString viewMode() const { return m_mode; }
  void setViewMode(const QString &mode);
  QString periodTitle() const { return m_periodTitle; }
  QVariantList periodDays() const { return m_days; }
  QVariantList yearMonths() const { return m_yearMonths; }
  QVariantList visibleEvents() const;
  QVariantList upcoming() const;
  QVariantList calendars() const;
  QVariantMap selectedEvent() const { return m_selected; }
  bool loading() const {
    return m_loading || m_debounce.isActive() || m_pending ||
           (m_active && m_calendar && m_calendar->loading());
  }
  QString errorString() const {
    return m_error.isEmpty() ? m_loadError : m_error;
  }
  QString todayDate() const;
  int currentMinute() const;
  QString zoneOffset() const;
  Q_INVOKABLE void refresh();
  Q_INVOKABLE void movePeriod(int delta);
  Q_INVOKABLE void setCalendarVisible(const QString &id, bool visible);
  Q_INVOKABLE void selectEvent(const QString &id,
                               const QString &start = QString());
  Q_INVOKABLE QVariantMap eventDraft(const QString &id = QString()) const;
  Q_INVOKABLE QVariantMap quickDraft(const QString &text);
  Q_INVOKABLE QString saveEvent(const QVariantMap &draft);
  Q_INVOKABLE bool removeEvent(const QString &id, const QString &revision);
signals:
  void calendarChanged();
  void activeChanged();
  void changed();

private:
  struct Snapshot {
    QVariantList events, upcoming, calendars;
    QString error;
    quint64 generation = 0;
  };
  void load();
  void updateFrames();
  void updateSelection();
  QString checkedDatabase() const;
  QPointer<DashboardCalendar> m_calendar;
  QTimer m_debounce;
  QFutureWatcher<Snapshot> m_watcher;
  QVariantList m_days, m_yearMonths, m_events, m_upcoming, m_calendarRows;
  QVariantMap m_selected;
  QSet<QString> m_hidden;
  QString m_mode = QStringLiteral("week"), m_periodTitle, m_frameKey, m_error,
          m_loadError, m_selectedId, m_selectedStart;
  bool m_active = false, m_loading = false, m_pending = false;
  quint64 m_generation = 0;
};
