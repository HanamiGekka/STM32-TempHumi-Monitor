#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QJsonArray>
#include <QToolTip>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <limits>

static const double APP_INSPECTION_POINT_RADIUS_PX=10.0;
static const double APP_INSPECTION_EVENT_RADIUS_PX=6.0;

void MainWindow::app_hide_inspection()
{
    // Hover previews may disappear. A clicked snapshot is dismissed only by
    // explicit Inspector Close/Clear/replacement, never another plot action.
    QToolTip::hideText();
}

void MainWindow::app_show_inspector()
{
    if(!m_inspector) m_inspector=new app_inspector_t(this);
    m_inspector->update_events(m_displayEvents);m_inspector->show();m_inspector->raise();
}

void MainWindow::app_validate_inspection()
{
    if(!m_inspector ||m_inspector->point_snapshot().isEmpty()) return;
    const auto selected=m_inspector->point_snapshot();bool found=false;
    if(selected["session_id"].toString()!=QFileInfo(m_recorder.app_session_directory()).fileName()) {
        m_inspector->point_retained(false);return;
    }
    for(const auto &sample:m_dataBuf) if(sample.time==selected["plot_elapsed_s"].toDouble()
        &&sample.segment_id==quint64(selected["segment_id"].toDouble())
        &&sample.raw_source_timestamp_ms==quint32(selected["source_timestamp_ms"].toDouble())) { found=true;break; }
    m_inspector->point_retained(found);
}

bool MainWindow::app_inspect_plot(const QPoint &position,bool eventsOnly,bool cycle)
{
    auto plot=ui->customPlot;
    Q_UNUSED(cycle);
    if(m_measureMode ||!plot->axisRect()->rect().contains(position)) { app_hide_inspection();return false; }
    const auto range=plot->xAxis->range();
    struct event_hit_t { double distance;int index; };
    QVector<event_hit_t> hits;
    for(int i=0;i<m_visibleEvents.size();++i) {
        const auto &event=m_visibleEvents[i];
        const double x=event.contains("plot_elapsed_s")?event["plot_elapsed_s"].toDouble():event["source_elapsed_s"].toDouble();
        if(!event["source_time_known"].toBool() ||x<range.lower ||x>range.upper) continue;
        const double d=std::abs(plot->xAxis->coordToPixel(x)-position.x());
        if(d<=APP_INSPECTION_EVENT_RADIUS_PX) hits.append({d,i});
    }
    if(!hits.isEmpty()) {
        std::stable_sort(hits.begin(),hits.end(),[](const event_hit_t &a,const event_hit_t &b) { return a.distance<b.distance; });
        const auto event=m_visibleEvents[hits.first().index];
        if(eventsOnly) {
            const auto preview=app_ui_event_name(event)+"\n"+app_ui_local_time(event["host_event_timestamp"].toString())
              +"\nClick for persistent details and the event list.";
            QToolTip::showText(plot->mapToGlobal(position),preview,plot);return true;
        }
        m_inspection={{"kind","EVENT"},{"events",QJsonArray{event}},{"nearby_count",hits.size()}};
        app_show_inspector();m_inspector->select_event(event,hits.size());
        return true;
    }
    if(eventsOnly) { app_hide_inspection();return false; }
    int best=-1,series=-1;double distance=APP_INSPECTION_POINT_RADIUS_PX*APP_INSPECTION_POINT_RADIUS_PX;
    for(int i=0;i<m_visibleSamples.size();++i) {
        const auto &sample=m_visibleSamples[i];
        if(sample.time<range.lower ||sample.time>range.upper) continue;
        const double dx=plot->xAxis->coordToPixel(sample.time)-position.x();
        if(dx*dx>distance) continue;
        const double values[]={sample.raw_temp,sample.humi,sample.corrected_temp};
        for(int curve=0;curve<3;++curve) {
            if(!plot->graph(curve)->visible() ||!std::isfinite(values[curve])) continue;
            const double y=plot->graph(curve)->valueAxis()->coordToPixel(values[curve]);
            if(y<plot->axisRect()->rect().top() ||y>plot->axisRect()->rect().bottom()) continue;
            const double dy=y-position.y(),d=dx*dx+dy*dy;
            if(d<=distance) { distance=d;best=i;series=curve; }
        }
    }
    if(best<0) { app_hide_inspection();return false; }
    const auto &sample=m_visibleSamples[best];
    const auto numeric=[](double value) { return std::isfinite(value)?QJsonValue(value):QJsonValue(); };
    const QString names[]={"Raw temperature","Humidity","Corrected temperature"};
    const double raw=sample.original_paired?sample.raw_temp:std::numeric_limits<double>::quiet_NaN();
    const QString status=sample.calibration_id>0?"PROV":sample.configured_profile==0?"Off":sample.temperature_source=="SIM"?"SIM (not applied)":sample.calibration_profile>0?"BASE":"unknown";
    m_inspection={{"kind","POINT"},{"series",names[series]},{"series_index",series},
        {"source_elapsed_s",sample.source_elapsed_s},{"source_timestamp_ms",double(sample.raw_source_timestamp_ms)},
        {"unwrapped_source_timestamp_ms",double(sample.source_timestamp_ms)},
        {"plot_elapsed_s",sample.time},{"segment_id",double(sample.segment_id)},
        {"physical_temperature_c",numeric(raw)},{"corrected_temperature_c",numeric(sample.corrected_temp)},
        {"published_temperature_c",numeric(sample.temp)},{"humidity_rh",numeric(sample.humi)},
        {"temperature_source",sample.temperature_source},{"humidity_source",sample.humidity_source},
        {"original_pair_available",sample.original_paired},{"configured_profile",sample.configured_profile<0?QJsonValue():QJsonValue(sample.configured_profile)},
        {"profile_evidence",sample.original_paired?"MCU_FLOAT32_SIDECAR":sample.temperature_source=="SIM" &&sample.configured_profile>=0?"LIVE_CAL_READBACK_CONFIGURED_ONLY":"UNKNOWN"},
        {"effective_calibration_id",sample.calibration_id},{"correction_status",status},{"host_receive_time",sample.received_at}};
    const QString transport=sample.input_mode=="SERIAL"?"LIVE UART":sample.input_mode=="REPLAY"?"REPLAY / FIXTURE":
        sample.temperature_source=="SIM"&&sample.humidity_source=="SIM"?"SIMULATED FILE":"REPLAY / FIXTURE (file)";
    m_inspection["input_mode"]=sample.input_mode;m_inspection["transport_classification"]=transport;
    m_inspection["session_id"]=QFileInfo(m_recorder.app_session_directory()).fileName();
    app_show_inspector();m_inspector->select_point(m_inspection);
    return true;
}
