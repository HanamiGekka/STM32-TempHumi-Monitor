#include "app_inspector.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextBrowser>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QLabel>
#include <QTabWidget>
#include <QToolButton>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QSignalBlocker>
#include <cmath>

QString app_ui_local_time(const QString &utc,const QTimeZone &zone)
{
    auto date=QDateTime::fromString(utc,Qt::ISODateWithMs);
    if(!date.isValid() ||!zone.isValid()) return "N/A";
    date=date.toTimeZone(zone);
    const int offset=date.offsetFromUtc(),seconds=std::abs(offset);
    QString suffix=QString("UTC%1%2:%3").arg(offset<0?"-":"+")
        .arg(seconds/3600,2,10,QChar('0')).arg((seconds/60)%60,2,10,QChar('0'));
    if(seconds%60) suffix+=QString(":%1").arg(seconds%60,2,10,QChar('0'));
    return date.toString("yyyy-MM-dd HH:mm:ss.zzz")+" "+suffix;
}
QString app_ui_fit_name(int profile)
{
    switch(profile) {
    case 0:return "0 — Off";
    case 1:return "1 — Ambient offset";
    case 2:return "2 — Guarded two-segment LAD";
    case 3:return "3 — Guarded six-segment Huber";
    default:return "Not confirmed";
    }
}
QString app_ui_event_name(const QJsonObject &event)
{
    const auto type=event["event_type"].toString();
    if(type=="CORRECTION_PROFILE_CHANGED") return "Temperature fit changed";
    if(type=="PAUSE_DISPLAY") return "Display paused; recording continues";
    if(type=="RESUME_DISPLAY") return "Display resumed";
    if(type=="SEGMENT_STARTED") return event["details"].toObject()["reason"]=="SOURCE_CHANGE"?"Measurement source changed":"New acquisition segment";
    if(type=="SOURCE_CHANGE") return "Measurement source changed";
    if(type=="SERIAL_DISCONNECTED") return "Serial connection lost";
    if(type=="SERIAL_REOPENED") return "Serial connection reopened";
    if(type=="SERIAL_OPENED") return "Serial connection opened";
    if(type=="SERIAL_RECONNECT_ATTEMPT") return "Reconnecting selected adapter";
    if(type=="SERIAL_RECONNECT_FAILED") return "Reconnect attempt failed";
    if(type=="SERIAL_RECOVERY_WAIT") return "Waiting for the trusted adapter";
    if(type=="SERIAL_STALE") return "Waiting for fresh measurements";
    if(type=="SERIAL_STREAM_TIMEOUT") return "No TH3 stream; recovering connection";
    if(type=="CONFIGURATION_RESTORED") return "Confirmed sources and fit restored";
    if(type.startsWith("CONFIGURATION_RESTORE")) return "Configuration recovery update";
    if(type=="MCU_BOOT_OBSERVED") return "MCU startup observed";
    if(type=="SIM_CAL_CONFIGURATION_READBACK") return "Fit configuration read; SIM is uncorrected";
    QString words=type.toLower();words.replace('_',' ');
    if(!words.isEmpty()) words[0]=words[0].toUpper();
    return words;
}
static QString app_inspector_value(const QJsonValue &value,const QString &unit=QString())
{
    return value.isDouble() &&std::isfinite(value.toDouble())?QString::number(value.toDouble(),'f',2)+unit:QString("N/A");
}
static QString app_inspector_rows(const QVector<QPair<QString,QString>> &rows)
{
    QString html="<table cellpadding='4'>";
    for(const auto &row:rows) html+="<tr><th align='left'>"+row.first.toHtmlEscaped()+"</th><td>"+row.second.toHtmlEscaped()+"</td></tr>";
    return html+"</table>";
}
app_inspector_t::app_inspector_t(QWidget *parent):QDialog(parent)
{
    setObjectName("persistentInspector");setWindowTitle("Recorded measurement and event details");
    setWindowFlags(windowFlags()|Qt::Tool);setAttribute(Qt::WA_QuitOnClose,false);
    setAttribute(Qt::WA_ShowWithoutActivating,true);setModal(false);resize(660,560);
    auto layout=new QVBoxLayout(this);m_tabs=new QTabWidget(this);m_tabs->setObjectName("inspectorTabs");layout->addWidget(m_tabs);
    auto pointPage=new QWidget(this),eventPage=new QWidget(this);m_tabs->addTab(pointPage,"Data point");m_tabs->addTab(eventPage,"Events");
    auto pointLayout=new QVBoxLayout(pointPage),eventLayout=new QVBoxLayout(eventPage);
    m_pointState=new QLabel(pointPage);m_pointState->setObjectName("pointSelectionState");m_pointState->setWordWrap(true);pointLayout->addWidget(m_pointState);
    m_pointText=new QTextBrowser(pointPage);m_pointText->setObjectName("pointDetails");pointLayout->addWidget(m_pointText);
    m_eventState=new QLabel(eventPage);m_eventState->setWordWrap(true);eventLayout->addWidget(m_eventState);
    m_eventList=new QListWidget(eventPage);m_eventList->setObjectName("inspectorEventList");m_eventList->setMaximumHeight(150);eventLayout->addWidget(m_eventList);
    auto navigation=new QHBoxLayout;auto previous=new QPushButton("Previous event",eventPage),next=new QPushButton("Next event",eventPage);
    previous->setObjectName("previousInspectorEvent");next->setObjectName("nextInspectorEvent");navigation->addWidget(previous);navigation->addWidget(next);eventLayout->addLayout(navigation);
    connect(previous,&QPushButton::clicked,this,[this]() { if(m_eventList->count()) m_eventList->setCurrentRow(qMax(0,m_eventList->currentRow()-1)); });
    connect(next,&QPushButton::clicked,this,[this]() { if(m_eventList->count()) m_eventList->setCurrentRow(qMin(m_eventList->count()-1,m_eventList->currentRow()+1)); });
    m_eventText=new QTextBrowser(eventPage);m_eventText->setObjectName("eventDetails");eventLayout->addWidget(m_eventText);
    const auto advanced=[this](QVBoxLayout *area,QWidget *page,const QString &name) {
        auto button=new QToolButton(page);button->setText("Advanced details (original UTC / raw fields)");button->setCheckable(true);
        auto raw=new QPlainTextEdit(page);raw->setObjectName(name);raw->setReadOnly(true);raw->setMaximumHeight(160);raw->hide();
        area->addWidget(button);area->addWidget(raw);connect(button,&QToolButton::toggled,raw,&QWidget::setVisible);return raw;
    };
    m_pointRaw=advanced(pointLayout,pointPage,"pointAdvanced");m_eventRaw=advanced(eventLayout,eventPage,"eventAdvanced");
    auto buttons=new QDialogButtonBox(this);auto clear=buttons->addButton("Clear selection",QDialogButtonBox::ResetRole);
    clear->setObjectName("clearInspectorSelection");buttons->addButton(QDialogButtonBox::Close);layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::hide);
    connect(clear,&QPushButton::clicked,this,[this]() { if(m_tabs->currentIndex()==0) {m_point=QJsonObject();render_point();} else {m_event=QJsonObject();render_event();} });
    connect(m_eventList,&QListWidget::currentRowChanged,this,[this](int row) {
        if(row>=0 &&row<m_events.size()) {m_event=m_events[row];render_event();}
    });
    render_point();render_event();
}
void app_inspector_t::show_selection(int tab) { m_tabs->setCurrentIndex(tab);show();raise(); }
void app_inspector_t::select_point(const QJsonObject &point) { m_point=point;m_historical=false;render_point();show_selection(0); }
void app_inspector_t::select_event(const QJsonObject &event,int nearby)
{
    m_event=event;m_nearby=nearby;const QSignalBlocker blocked(m_eventList);
    const int row=m_events.indexOf(event);m_eventList->setCurrentRow(row);
    if(row>=0) m_eventList->scrollToItem(m_eventList->item(row));
    render_event();show_selection(1);
}
void app_inspector_t::update_events(const QVector<QJsonObject> &events)
{
    if(events==m_events) return;
    m_events=events;while(m_events.size()>256) m_events.removeFirst();
    const QSignalBlocker blocked(m_eventList);m_eventList->clear();
    for(const auto &e:m_events) m_eventList->addItem(app_ui_local_time(e["host_event_timestamp"].toString())+" — "+app_ui_event_name(e));
    m_eventList->setCurrentRow(m_events.indexOf(m_event));render_event();
}
void app_inspector_t::point_retained(bool retained)
{
    if(m_historical==!retained) return;
    m_historical=!retained;render_point();
}
void app_inspector_t::render_point()
{
    if(m_point.isEmpty()) {m_pointState->setText("Click a visible sampled point. Details stay until you clear or replace them.");m_pointText->clear();m_pointRaw->clear();return;}
    m_pointState->setText(m_historical?"Saved historical selection — outside the retained graph; original snapshot preserved.":"Saved selection — live updates and other controls do not dismiss these details.");
    const auto p=m_point;
    m_pointText->setHtml(app_inspector_rows({{"Type",p["series"].toString()},{"Data source",p["transport_classification"].toString()+" | T:"+p["temperature_source"].toString()+" / H:"+p["humidity_source"].toString()},
      {"Received locally",app_ui_local_time(p["host_receive_time"].toString())},{"MCU timestamp",QString::number(p["source_timestamp_ms"].toDouble(),'f',0)+" ms"},
      {"Source elapsed",QString::number(p["source_elapsed_s"].toDouble(),'f',3)+" s"},{"Segment",QString::number(p["segment_id"].toDouble(),'f',0)},
      {"Original temperature",app_inspector_value(p["physical_temperature_c"]," °C")},{"Corrected temperature",app_inspector_value(p["corrected_temperature_c"]," °C")},
      {"MCU output",app_inspector_value(p["published_temperature_c"]," °C")},{"Humidity",app_inspector_value(p["humidity_rh"]," %RH")},
      {"Temperature fit",app_ui_fit_name(p["configured_profile"].isDouble()?p["configured_profile"].toInt():-1)},
      {"Correction / validity",p["correction_status"].toString()+"; missing/invalid fields are N/A"}}));
    m_pointRaw->setPlainText(QString::fromUtf8(QJsonDocument(p).toJson(QJsonDocument::Indented)));
}
void app_inspector_t::render_event()
{
    if(m_event.isEmpty()) {m_eventState->setText("Select an event line or a row. Latest256 events are listed; full JSONL remains recorded.");m_eventText->clear();m_eventRaw->clear();return;}
    const auto e=m_event,details=e["details"].toObject();const int index=m_events.indexOf(e);
    m_eventState->setText(QString("%1 | %2 nearby. Select a row or Previous/Next; full JSONL is unchanged.")
      .arg(index<0?QString("Saved historical event"):QString("Event %1 of %2").arg(index+1).arg(m_events.size())).arg(m_nearby));
    QString reason=details["reason"].toString();
    if(reason=="SOURCE_CHANGE") reason="An effective measurement source changed at this acquisition boundary.";
    else if(reason=="SERIAL_RECOVERY_OR_STALE") reason="Collection resumed after a connection/stale interval; missing time is a gap.";
    else if(reason.isEmpty()) reason=details["qt_error_text"].toString(details["trigger"].toString());
    const QString source=e["source_time_known"].toBool()?QString::number(e["source_elapsed_s"].toDouble(),'f',3)+" s":"Unknown (no source-time evidence)";
    m_eventText->setHtml(app_inspector_rows({{"Event",app_ui_event_name(e)},{"Occurred locally",app_ui_local_time(e["host_event_timestamp"].toString())},
      {"Explanation",reason.isEmpty()?app_ui_event_name(e):reason},{"Source elapsed",source},
      {"Segment",e["segment_id"].isDouble()?QString::number(e["segment_id"].toDouble(),'f',0):QString("Unknown")},
      {"Temperature fit",app_ui_fit_name(e["correction_profile"].isDouble()?e["correction_profile"].toInt():-1)},
      {"Connection at event",details["connection"].toObject()["state"].toString("Not recorded for this event")},
      {"Session",e["session_id"].toString()}}));
    m_eventRaw->setPlainText(QString::fromUtf8(QJsonDocument(e).toJson(QJsonDocument::Indented)));
}
QJsonObject app_inspector_t::current_selection() const
{
    if(m_tabs->currentIndex()==0) return m_point;
    return m_event.isEmpty()?QJsonObject():QJsonObject{{"kind","EVENT"},{"events",QJsonArray{m_event}}};
}
QJsonObject app_inspector_t::snapshot() const
{
    return {{"visible",isVisible()},{"point",m_point},{"event",m_event},{"point_historical",m_historical},
      {"event_list_count",m_events.size()},{"tab",m_tabs->currentIndex()},{"point_text",m_pointText->toPlainText()},
      {"event_text",m_eventText->toPlainText()},{"system_time_zone",QString::fromUtf8(QTimeZone::systemTimeZone().id())}};
}
