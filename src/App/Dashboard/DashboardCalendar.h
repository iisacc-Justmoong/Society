#pragma once
#include <iiCalendar/iiCalendar.h>
#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Qt is confined to this presentation adapter; iiCalendar owns dates and storage.
class DashboardCalendar : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString containerPath READ containerPath WRITE setContainerPath NOTIFY containerPathChanged)
    Q_PROPERTY(QString databasePath READ databasePath NOTIFY containerPathChanged)
    Q_PROPERTY(QString timeZone READ timeZone WRITE setTimeZone NOTIFY configurationChanged)
    Q_PROPERTY(QString calendarSystem READ calendarSystem WRITE setCalendarSystem NOTIFY configurationChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
    Q_PROPERTY(QString selectedDate READ selectedDate NOTIFY changed)
    Q_PROPERTY(QString monthTitle READ monthTitle NOTIFY changed)
    Q_PROPERTY(QString dayTitle READ dayTitle NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QVariantList days READ days NOTIFY changed)
    Q_PROPERTY(QVariantList events READ events NOTIFY changed)
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY changed)
    Q_PROPERTY(QVariantList activity READ activity NOTIFY changed)
    Q_PROPERTY(QVariantList notes READ notes NOTIFY changed)
    Q_PROPERTY(QVariantList reminders READ reminders NOTIFY changed)
    Q_PROPERTY(QVariantList files READ files NOTIFY changed)
    Q_PROPERTY(int completedTasks READ completedTasks NOTIFY changed)
public:
    explicit DashboardCalendar(QObject *parent = nullptr);
    ~DashboardCalendar() override;
    QString containerPath() const { return m_containerPath; }
    QString databasePath() const { return m_databasePath; }
    QString timeZone() const { return QString::fromStdString(m_zone.id()); }
    QString calendarSystem() const { return m_calendarSystem; }
    bool active() const { return m_active; }
    bool loading() const { return m_loading; }
    QString errorString() const { return m_error; }
    QString selectedDate() const { return QString::fromStdString(m_selected.toString()); }
    QString monthTitle() const;
    QString dayTitle() const;
    QString summary() const;
    QVariantList days() const { return m_days; }
    QVariantList events() const { return m_events; }
    QVariantList tasks() const { return m_tasks; }
    QVariantList activity() const { return m_activity; }
    QVariantList notes() const { return m_notes; }
    QVariantList reminders() const { return m_reminders; }
    QVariantList files() const { return m_files; }
    int completedTasks() const { return m_completedTasks; }
    void setContainerPath(const QString &path);
    void setTimeZone(const QString &zone);
    void setCalendarSystem(const QString &system);
    void setActive(bool active);
    Q_INVOKABLE void selectDate(const QString &isoDate);
    Q_INVOKABLE void moveMonth(int delta);
    Q_INVOKABLE void moveDay(int delta);
    Q_INVOKABLE void goToday();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool setTaskCompleted(const QString &id, const QString &revision, bool completed);
    // Returns only an existing attachment inside the active Society drive.
    Q_INVOKABLE QString attachmentPath(const QString &uri) const;
signals:
    void containerPathChanged();
    void configurationChanged();
    void activeChanged();
    void changed();
private:
    struct Snapshot {
        QVariantList days, events, tasks, activity, notes, reminders, files;
        int completedTasks = 0;
        QString error;
        quint64 generation = 0;
    };
    void clearDetails();
    void updateDays();
    void fail(const std::exception &error);
    iiCalendar::CalendarRegistry m_calendars;
    iiCalendar::TimeZone m_zone;
    iiCalendar::Date m_selected;
    QString m_containerPath, m_databasePath, m_driveIdentifier, m_calendarSystem = "gregorian", m_error;
    QVariantList m_days, m_events, m_tasks, m_activity, m_notes, m_reminders, m_files;
    int m_completedTasks = 0;
    bool m_active = true, m_loading = false, m_pending = false;
    quint64 m_generation = 0;
    QFutureWatcher<Snapshot> m_watcher;
    QTimer m_timer;
};
