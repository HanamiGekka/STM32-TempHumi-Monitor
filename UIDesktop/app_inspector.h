#ifndef APP_INSPECTOR_H
#define APP_INSPECTOR_H
#include <QDialog>
#include <QJsonObject>
#include <QVector>
#include <QTimeZone>
class QTabWidget;
class QTextBrowser;
class QListWidget;
class QLabel;
class QPlainTextEdit;

QString app_ui_local_time(const QString &utc,const QTimeZone &zone=QTimeZone::systemTimeZone());
QString app_ui_event_name(const QJsonObject &event);
QString app_ui_fit_name(int profile);

// One copied point and one copied event, plus a bounded list. No pointers into
// the plot/sample vectors survive trimming, session changes or redraws.
class app_inspector_t : public QDialog
{
public:
    explicit app_inspector_t(QWidget *parent=nullptr);
    void select_point(const QJsonObject &point);
    void select_event(const QJsonObject &event,int nearby=1);
    void update_events(const QVector<QJsonObject> &events);
    void point_retained(bool retained);
    QJsonObject point_snapshot() const { return m_point; }
    QJsonObject current_selection() const;
    QJsonObject snapshot() const;
private:
    void render_point();
    void render_event();
    void show_selection(int tab);
    QTabWidget *m_tabs;
    QTextBrowser *m_pointText,*m_eventText;
    QPlainTextEdit *m_pointRaw,*m_eventRaw;
    QListWidget *m_eventList;
    QLabel *m_pointState,*m_eventState;
    QJsonObject m_point,m_event;
    QVector<QJsonObject> m_events;
    bool m_historical=false;
    int m_nearby=1;
};
#endif
