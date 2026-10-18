/* Software-only acceptance: production MainWindow objects, no hardware. */
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QComboBox>
#include <QPushButton>
#include <QScrollArea>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QImage>
#include <cmath>
#include <QTextStream>
#include "mainwindow.h"

static void invoke(MainWindow &window, const char *slot)
{
    if (!QMetaObject::invokeMethod(&window, slot, Qt::DirectConnection)) qFatal("Missing production slot");
}
static void writeInput(const QByteArray &data, bool append=false)
{
    QFile file("probe_input.txt");
    if (!file.open(QIODevice::WriteOnly | (append ? QIODevice::Append : QIODevice::Truncate))) qFatal("Input write failed");
    if (file.write(data) != data.size()) qFatal("Short input write");
}
static QByteArray sim(quint32 timestamp, double temperature=29.0, double humidity=60.0)
{
    return QString("TH3,SIM,%1,1,3,0,0,1,%2,%3,NA,NA,NA,NA,NA,NA,NA,NA,SIM,SIM,1,1,0\r\n")
        .arg(timestamp).arg(temperature,0,'f',2).arg(humidity,0,'f',2).toLatin1();
}
static bool check(bool condition, const QString &name, QJsonObject &results)
{
    results[name] = condition;
    return condition;
}
int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc,argv); app.setProperty("appSettingsFile",QDir::current().filePath("preferences.ini"));
    QFile version(argc>3?QString::fromLocal8Bit(argv[3]):"missing-version");
    if(!version.open(QIODevice::ReadOnly)) return 2;
    QCoreApplication::setApplicationVersion(QString::fromUtf8(version.readAll()).trimmed());
    const QString mode = argc>1 ? QString::fromLocal8Bit(argv[1]) : "acceptance121";
    MainWindow window;
    window.app_configure_input(QDir::current().absoluteFilePath("probe_input.txt"), "REPLAY", QDir::current().absoluteFilePath("sessions"));
    window.show();
    app.processEvents();
    QJsonObject checks;
    bool pass = true;
    if (mode == "acceptance121") {
        QFile fixture(argc>2 ? QString::fromLocal8Bit(argv[2]) : "missing-fixture");
        if (!fixture.open(QIODevice::ReadOnly)) return 2;
        writeInput(fixture.readAll());
        invoke(window,"slotDraw");
        invoke(window,"slotReadFileData");
        const auto snapshot=window.app_status_snapshot();
        pass &= check(snapshot["parsed_count"].toInt()==121,"accepted_121",checks);
        pass &= check(snapshot["ui_points"].toInt()==121&&snapshot["display_span_s"].toDouble()==120,"curve_retains_last_60_source_seconds",checks);
        pass &= check(snapshot["record_summary"].toObject()["parsed_th3_count"].toInt()==121,"record_full_121",checks);
        pass &= check(snapshot["source_label"].toString().contains("SIMULATED"),"simulated_label",checks);
        pass &= check(snapshot["source_label"].toString().contains("REPLAY"),"replay_label",checks);
        pass &= check(snapshot["recording"].toBool(),"recording_on",checks);
        app.processEvents(); window.grab().save("simulated_replay_121.png");
    } else if (mode == "restart") {
        writeInput(sim(0,39,39)+sim(100,41,41));
        invoke(window,"slotDraw"); invoke(window,"slotReadFileData");
        pass &= check(window.findChild<QCustomPlot*>("customPlot")->itemCount()>3,"trigger_marks_exist",checks);
        invoke(window,"slotDraw");
        writeInput(sim(200,42,42),true); invoke(window,"slotReadFileData");
        pass &= check(window.app_status_snapshot()["parsed_count"].toInt()==3,"no_duplicate_on_restart",checks);
        pass &= check(window.app_status_snapshot()["ui_points"].toInt()==1,"new_display_segment",checks);
    } else if (mode == "empty_measure") {
        writeInput(""); invoke(window,"slotDraw"); invoke(window,"slotReadFileData"); invoke(window,"slotMeasure");
        QMouseEvent mouse(QEvent::MouseButtonPress,QPointF(200,200),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMetaObject::invokeMethod(&window,"customPlotMousePress",Qt::DirectConnection,Q_ARG(QMouseEvent*,&mouse));
        pass &= check(window.app_status_snapshot()["ui_points"].toInt()==0,"empty_stays_empty",checks);
        pass &= check(!window.app_analyze_selection(APP_SIGNAL_TEMPERATURE,0,1).selection_valid,"empty_analysis_na",checks);
    } else if (mode == "cursor_rollover") {
        QByteArray first;
        for (int i=0;i<200;++i) first+=sim(quint32(i*100),30+(i%2)*20);
        writeInput(first); invoke(window,"slotDraw"); invoke(window,"slotReadFileData"); invoke(window,"slotMeasure");
        QMouseEvent mouse(QEvent::MouseButtonPress,QPointF(200,200),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMetaObject::invokeMethod(&window,"customPlotMousePress",Qt::DirectConnection,Q_ARG(QMouseEvent*,&mouse));
        QByteArray next;
        for(int i=200;i<13001;++i) next+=sim(quint32(i*100),30+(i%2)*20);
        writeInput(next,true); while(window.app_status_snapshot()["parsed_count"].toInt()<13001) invoke(window,"slotReadFileData");
        pass &= check(!window.app_status_snapshot()["selection_pending"].toBool(),"expired_pending_cursor_cleared",checks);
        QMetaObject::invokeMethod(&window,"customPlotMousePress",Qt::DirectConnection,Q_ARG(QMouseEvent*,&mouse));
        pass &= check(window.app_status_snapshot()["ui_points"].toInt()>=12000 && window.app_status_snapshot()["ui_points"].toInt()<=12002,"horizon_points_cover_60s",checks);
        pass &= check(window.app_status_snapshot()["display_span_s"].toDouble()<=1200.001,"bounded_time_horizon",checks);
        pass &= check(window.app_status_snapshot()["selection_pending"].toBool(),"new_cursor_after_expiry_safe",checks);
        pass &= check(window.findChild<QCustomPlot*>("customPlot")->itemCount()<=4003,"bounded_trigger_items",checks);
        pass &= check(window.app_status_snapshot()["parsed_count"].toInt()==13001,"all_recorded",checks);
    } else if (mode == "duplicate_time_bound") {
        QByteArray input;
        for(int i=0;i<30000;++i) input+=sim(1000,(i%2)?41:39,(i%2)?41:39);
        writeInput(input); invoke(window,"slotDraw"); while(window.app_status_snapshot()["parsed_count"].toInt()<30000) invoke(window,"slotReadFileData");
        pass &= check(window.app_status_snapshot()["ui_points"].toInt()==24000,"bounded_duplicate_time_points",checks);
        pass &= check(window.findChild<QCustomPlot*>("customPlot")->itemCount()<=4003,"bounded_duplicate_time_marks",checks);
        pass &= check(window.app_status_snapshot()["parsed_count"].toInt()==30000,"duplicate_time_all_recorded",checks);
        pass &= check(!window.app_analyze_selection(APP_SIGNAL_TEMPERATURE,0,0).selection_valid,"duplicate_time_analysis_rejected",checks);
    } else if (mode == "stop_backlog") {
        QByteArray input;
        for(int i=0;i<9000;++i) input+=sim(quint32(i*100));
        writeInput(input); invoke(window,"slotDraw"); invoke(window,"slotStop");
        pass &= check(window.app_status_snapshot()["parsed_count"].toInt()==9000,"stop_drains_multiple_chunks",checks);
        pass &= check(window.app_status_snapshot()["record_summary"].toObject()["parsed_th3_count"].toInt()==9000,"stop_records_all",checks);
    } else if (mode == "semantics") {
        writeInput("TH3,REAL,10,1,239,32,0,16,-10.00,65.50,1.000,3.084,NA,65.50,8000,24672,NA,NA,REAL,REAL,1,1,0\r\n");
        invoke(window,"slotDraw"); invoke(window,"slotReadFileData");
        auto snapshot=window.app_status_snapshot();
        pass &= check(snapshot["state"].toString()=="VALID","valid_with_pwm_no_sample",checks);
        pass &= check(snapshot["value_label"].toString().contains("-10.00"),"negative_temperature",checks);
        pass &= check(snapshot["source_label"].toString().contains("REPLAY") && !snapshot["source_label"].toString().contains("LIVE REAL"),"real_fixture_replay",checks);
        writeInput("TH3,SIM,100,0,0,0,0,1,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,SIM,SIM,0,0,0\r\n",true); invoke(window,"slotReadFileData");
        snapshot=window.app_status_snapshot();
        pass &= check(snapshot["state"].toString()=="INVALID","invalid_state",checks);
        pass &= check(snapshot["value_label"].toString().contains("NA"),"missing_not_zero",checks);
        writeInput("TH3,SIM,200,1,3,0,0,1,bad,60,NA,NA,NA,NA,NA,NA,NA,NA,SIM,SIM,1,1,0\r\n",true); invoke(window,"slotReadFileData");
        snapshot=window.app_status_snapshot();
        pass &= check(snapshot["state"].toString()=="TRANSIENT" &&snapshot["recording"].toBool() &&snapshot["malformed_count"].toInt()==1,"malformed_retained_without_ending_session",checks);
        writeInput(sim(300),true);invoke(window,"slotReadFileData");
        pass &= check(window.app_status_snapshot()["state"]=="VALID" &&window.app_status_snapshot()["recording"].toBool(),"valid_frame_recovers_after_malformed_record",checks);
    } else if (mode == "waiting_resume") {
        QFile::remove("probe_input.txt"); invoke(window,"slotDraw"); invoke(window,"slotReadFileData");
        pass &= check(window.app_status_snapshot()["state"].toString()=="WAITING","missing_waiting",checks);
        writeInput(sim(0)); invoke(window,"slotReadFileData");
        pass &= check(window.app_status_snapshot()["parsed_count"].toInt()==1,"appeared_recovers",checks);
        QFile::remove("probe_input.txt"); invoke(window,"slotReadFileData");
        writeInput(sim(0,35)); invoke(window,"slotReadFileData");
        pass &= check(window.app_status_snapshot()["parsed_count"].toInt()==2,"reappeared_recovers",checks);
    } else if(mode=="measurement") {
        QByteArray input; for(int i=0;i<100;++i) input+=sim(quint32(i*100),i<50?20:30,i<50?40:60);
        writeInput(input); invoke(window,"slotDraw"); invoke(window,"slotReadFileData");
        window.app_set_analysis_channel(APP_SIGNAL_TEMPERATURE); invoke(window,"slotMeasure");
        pass &= check(window.app_select_cursor(0),"temperature_cursor1",checks);
        pass &= check(window.app_select_cursor(9.9),"temperature_cursor2",checks);
        auto result=window.app_analyze_selection(APP_SIGNAL_TEMPERATURE,0,9.9);
        pass &= check(result.selection_valid && result.sample_count==100,"temperature_selected_100",checks);
        pass &= check(result.mean.available&&std::abs(result.mean.value-25)<1e-10,"temperature_mean_25",checks);
        pass &= check(result.rms.available&&std::abs(result.rms.value-std::sqrt(650.0))<1e-9,"temperature_rms",checks);
        pass &= check(result.minimum.value==20&&result.maximum.value==30&&result.peak_to_peak.value==10,"temperature_minmax_p2p",checks);
        pass &= check(result.rise_time_10_90_s.available&&std::abs(result.rise_time_10_90_s.value-.08)<1e-8,"temperature_observed_step_rise",checks);
        window.app_set_analysis_channel(APP_SIGNAL_HUMIDITY);
        auto humidity=window.app_analyze_selection(APP_SIGNAL_HUMIDITY,0,9.9);
        pass &= check(humidity.selection_valid&&std::abs(humidity.mean.value-50)<1e-10,"humidity_mean_50",checks);
        pass &= check(std::abs(humidity.rms.value-std::sqrt(2600.0))<1e-8&&humidity.peak_to_peak.value==20,"humidity_rms_p2p",checks);
        pass &= check(humidity.rise_time_10_90_s.available&&std::abs(humidity.rise_time_10_90_s.value-.08)<1e-8,"humidity_observed_step_rise",checks);
        pass &= check(humidity.overshoot_pct.available&&std::abs(humidity.overshoot_pct.value)<1e-10,"humidity_step_no_overshoot",checks);
        pass &= check(humidity.settling_time_2pct_s.available&&humidity.settling_time_5pct_s.available,"humidity_step_settles",checks);
        pass &= check(window.app_status_snapshot()["cursor_1_visible"].toBool()&&window.app_status_snapshot()["cursor_2_visible"].toBool(),"two_visible_cursors",checks);
        pass &= check(window.app_status_snapshot()["analysis_summary"].toString().contains("Humidity")&&window.app_status_snapshot()["analysis_summary"].toString().contains("t1 0.000"),"channel_and_precise_time_visible",checks);
        app.processEvents();
        auto summaryLabel=window.findChild<QLabel*>("lbl_paramResult");
        pass &= check(summaryLabel->height()>=summaryLabel->heightForWidth(summaryLabel->width()),"analysis_full_text_has_layout_height",checks);
        window.grab().save("simulated_measurement_selection.png");
        window.resize(920,620); app.processEvents();
        pass &= check(summaryLabel->height()>=summaryLabel->heightForWidth(summaryLabel->width()),"analysis_minimum_resize_full_text_height",checks);
        window.grab().save("simulated_measurement_small_window.png");
        auto plot=window.findChild<QCustomPlot*>("customPlot");
        invoke(window,"slotMeasure");
        const double x=plot->xAxis->coordToPixel(.1);
        QMouseEvent mouse(QEvent::MouseButtonPress,QPointF(x,plot->axisRect()->rect().center().y()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMetaObject::invokeMethod(&window,"customPlotMousePress",Qt::DirectConnection,Q_ARG(QMouseEvent*,&mouse));
        pass &= check(window.app_status_snapshot()["selection_pending"].toBool(),"actual_mouse_uses_cursor_api",checks);
        pass &= check(window.app_select_cursor(9.8),"mouse_then_public_cursor2",checks);
        auto snapshot=window.app_status_snapshot();
        pass &= check(snapshot["selection"].toObject()["selection_valid"].toBool(),"actual_selection_analysis_valid",checks);
        invoke(window,"slotMeasure");
        pass &= check(window.app_select_cursor(9.9)&&window.app_select_cursor(0),"reverse_click_order_normalized",checks);
    } else if(mode=="analysis_gates") {
        QByteArray input; for(int i=0;i<100;++i) input+=(i%2)?sim(quint32(i*100)):QString("TH3,SIM,%1,0,0,0,0,1,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,SIM,SIM,0,0,0\r\n").arg(i*100).toLatin1();
        writeInput(input); invoke(window,"slotDraw"); invoke(window,"slotReadFileData");
        auto result=window.app_analyze_selection(APP_SIGNAL_TEMPERATURE,0,9.9);
        pass &= check(result.invalid_sample_count==50&&result.valid_sample_count==50&&result.mean.available,"na_not_zero",checks);
        pass &= check(!result.response_valid&&!result.rise_time_10_90_s.available&&!result.response_reason.isEmpty(),"na_heavy_response_na_reason",checks);
        pass &= check(!window.app_analyze_selection(APP_SIGNAL_TEMPERATURE,9,1).selection_valid,"reverse_window_rejected",checks);
        pass &= check(!window.app_analyze_selection(APP_SIGNAL_TEMPERATURE,-1,5).selection_valid,"expired_window_rejected",checks);
        const auto oldSegment=window.app_status_snapshot()["segment_id"].toDouble();
        writeInput(sim(0,50),true); invoke(window,"slotReadFileData");
        pass &= check(!window.app_status_snapshot()["selection_active"].toBool(),"segment_clears_selection",checks);
        pass &= check(!window.app_analyze_selection(APP_SIGNAL_TEMPERATURE,0,0,quint64(oldSegment)).selection_valid,"old_segment_rejected",checks);
    } else if(mode=="plot_polish") {
        QByteArray input; for(int i=0;i<800;++i) input+=sim(quint32(i*100),25+std::sin(i*.02),60);
        writeInput(input); invoke(window,"slotDraw"); invoke(window,"slotReadFileData");
        while(window.app_status_snapshot()["parsed_count"].toInt()<800) invoke(window,"slotReadFileData");
        auto snapshot=window.app_status_snapshot();
        pass &= check(snapshot["display_span_s"].toDouble()>=79.8&&snapshot["display_span_s"].toDouble()<=79.901,"full_60s_retained",checks);
        pass &= check(snapshot["plot_x_upper_s"].toDouble()-snapshot["plot_x_lower_s"].toDouble()==120,"recent_32s_actual",checks);
        pass &= check(snapshot["x_axis_mode"].toString()=="AUTO","recent_mode_text_matches",checks);
        window.app_set_recent_view(false); snapshot=window.app_status_snapshot();
        pass &= check(snapshot["view_mode"].toString()=="AUTO"&&snapshot["x_axis_span_s"].toInt()==120,"full_mode_text_matches",checks);
        pass &= check(std::abs(snapshot["plot_x_lower_s"].toDouble()-snapshot["display_oldest_elapsed_s"].toDouble())<1e-8,"full_buffer_starts_at_oldest",checks);
        pass &= check(snapshot["plot_temperature_lower"].toDouble()>15&&snapshot["plot_temperature_upper"].toDouble()<40,"temperature_autorange",checks);
        pass &= check(snapshot["plot_humidity_lower"].toDouble()==0&&snapshot["plot_humidity_upper"].toDouble()==100,"humidity_physical_range",checks);
        const double low=snapshot["plot_temperature_lower"].toDouble(),high=snapshot["plot_temperature_upper"].toDouble();
        writeInput(sim(80000,25.2,60),true); invoke(window,"slotReadFileData"); snapshot=window.app_status_snapshot();
        pass &= check(snapshot["plot_temperature_lower"].toDouble()==low&&snapshot["plot_temperature_upper"].toDouble()==high,"range_hysteresis_stable",checks);
        writeInput(sim(80100,-30,130)+QString("TH3,SIM,80200,0,0,0,0,1,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,SIM,SIM,0,0,0\r\n").toLatin1(),true); invoke(window,"slotReadFileData");
        auto plot=window.findChild<QCustomPlot*>("customPlot"); snapshot=window.app_status_snapshot();
        pass &= check(snapshot["plot_temperature_lower"].toDouble()<-30&&snapshot["plot_humidity_upper"].toDouble()>130,"abnormal_fixture_visible_unclamped",checks);
        pass &= check(std::isnan((plot->graph(0)->data()->constEnd()-1)->value)&&std::isnan((plot->graph(1)->data()->constEnd()-1)->value),"invalid_both_channel_gaps",checks);
        window.resize(920,620); app.processEvents();
        pass &= check(plot->width()>=430&&plot->height()>=360&&window.centralWidget()->rect().contains(plot->geometry()),"minimum_resize_plot_not_clipped",checks);
        for(const char *name:{"btn_draw","btn_stop","btn_measure","btn_exit","lbl_value"}) {
            auto widget=window.findChild<QWidget*>(name); const auto mapped=widget->mapTo(window.centralWidget(),QPoint(0,0));
            pass &= check(window.centralWidget()->rect().contains(QRect(mapped,widget->size())),QString("resize_visible_")+name,checks);
        }
        pass &= check(window.findChild<QScrollArea*>("sideScroll")->widgetResizable(),"sidebar_resize_safe_scroll",checks);
        pass &= check(window.windowTitle().contains(app.applicationVersion())&&snapshot["source_label"].toString().contains("TH3")&&snapshot["source_label"].toString().contains("NOT TESTED"),"status_version_protocol_visible",checks);
        window.grab().save("simulated_resize_plot.png");
    } else if(mode=="exports") {
        QByteArray input; for(int i=0;i<100;++i) input+=sim(quint32(i*100),20+i*.1,40+i*.2);
        writeInput(input); invoke(window,"slotDraw"); invoke(window,"slotReadFileData"); invoke(window,"slotStop");
        auto snapshot=window.app_status_snapshot(); const QString directory=snapshot["record_directory"].toString();
        auto bytes=[](const QString &path){QFile file(path); if(!file.open(QIODevice::ReadOnly)) qFatal("Evidence read failed"); return file.readAll();};
        const auto rawBefore=bytes(directory+"/raw_input.log"),csvBefore=bytes(directory+"/parsed.csv"),metaBefore=bytes(directory+"/metadata.json");
        pass &= check(!QFile::exists(directory+"/analysis.json"),"no_automatic_analysis_artifact",checks);
        window.app_analyze_selection(APP_SIGNAL_HUMIDITY,0,9.9);
        pass &= check(!QFile::exists(directory+"/analysis.json"),"analysis_without_save_creates_no_file",checks);
        pass &= check(window.app_save_analysis(directory+"/analysis.json"),"explicit_json_export",checks);
        const auto analysis=QJsonDocument::fromJson(bytes(directory+"/analysis.json")).object();
        pass &= check(analysis["software_version"].toString()==app.applicationVersion()&&analysis["classification"].toString().contains("SIMULATED"),"analysis_version_classification",checks);
        pass &= check(!window.app_save_analysis(directory+"/parsed.csv")&&!window.app_export_plot(directory+"/raw_input.log"),"record_files_protected_from_export",checks);
        pass &= check(window.app_export_plot("simulated_export.png"),"qcp_png_export",checks);
        const QImage image("simulated_export.png");
        pass &= check(!image.isNull()&&image.width()>=1000&&image.height()>=600,"export_png_dimensions",checks);
        pass &= check(snapshot["plot_export_title"].toString().contains(app.applicationVersion())&&snapshot["plot_export_status"].toString().contains("SIMULATED"),"plot_annotation_version_source",checks);
        pass &= check(bytes(directory+"/raw_input.log")==rawBefore&&bytes(directory+"/parsed.csv")==csvBefore&&bytes(directory+"/metadata.json")==metaBefore,"raw_csv_metadata_unchanged_by_exports",checks);
        app.processEvents();
        auto summaryLabel=window.findChild<QLabel*>("lbl_paramResult");
        pass &= check(summaryLabel->height()>=summaryLabel->heightForWidth(summaryLabel->width()),"na_reasons_full_text_has_layout_height",checks);
        window.grab().save("simulated_analysis_result.png");
    } else if(mode=="rapid_restart") {
        writeInput(sim(0)); invoke(window,"slotDraw"); invoke(window,"slotReadFileData");
        for(int i=1;i<=12;++i) { invoke(window,"slotStop"); invoke(window,"slotDraw"); writeInput(sim(quint32(i*100),20+i),true); invoke(window,"slotReadFileData"); }
        pass &= check(window.app_status_snapshot()["parsed_count"].toInt()==13,"rapid_restart_no_replay",checks);
        pass &= check(window.app_status_snapshot()["restart_count"].toInt()==12,"rapid_restart_count",checks);
    } else if(mode=="export_aliases") {
        const QByteArray input=sim(0,25,60)+sim(100,26,61);
        writeInput(input); invoke(window,"slotDraw"); invoke(window,"slotReadFileData"); invoke(window,"slotStop");
        window.app_analyze_selection(APP_SIGNAL_HUMIDITY,0,.1);
        const QString alias=QDir::current().absoluteFilePath("PROBE_INPUT.TXT");
        pass &= check(!window.app_save_analysis(alias),"json_case_alias_cannot_replace_input",checks);
        QFile checkInput("probe_input.txt"); checkInput.open(QIODevice::ReadOnly);
        pass &= check(checkInput.readAll()==input,"input_bytes_unchanged_after_json_alias",checks); checkInput.close();
        // Each RED operation uses this isolated synthetic input, restoring it
        // between scenarios; no formal or previous experiment bytes are touched.
        writeInput(input);
        pass &= check(!window.app_export_plot(alias),"png_case_alias_cannot_replace_input",checks);
        checkInput.open(QIODevice::ReadOnly);
        pass &= check(checkInput.readAll()==input,"input_bytes_unchanged_after_png_alias",checks); checkInput.close();
        writeInput(input);
        window.app_set_status_path(QDir::current().absoluteFilePath("telemetry.json"));
        pass &= check(!window.app_save_analysis(QDir::current().absoluteFilePath("TELEMETRY.JSON")),"status_output_case_alias_protected",checks);
    } else if(mode=="source_reconfigure") {
        writeInput(sim(0,25,60)+sim(100,26,61)); invoke(window,"slotDraw"); invoke(window,"slotReadFileData"); invoke(window,"slotStop");
        window.app_analyze_selection(APP_SIGNAL_TEMPERATURE,0,.1);
        pass &= check(window.app_status_snapshot()["ui_points"].toInt()==2&&window.app_status_snapshot()["selection_active"].toBool(),"old_source_had_data_and_analysis",checks);
        window.app_configure_input(QDir::current().absoluteFilePath("new_fixture.txt"),"REPLAY",QDir::current().absoluteFilePath("sessions"));
        const auto snapshot=window.app_status_snapshot();
        pass &= check(snapshot["ui_points"].toInt()==0&&snapshot["plot_points"].toInt()==0,"new_path_has_no_relabelled_old_curve",checks);
        pass &= check(!snapshot["selection_active"].toBool()&&!snapshot["cursor_1_visible"].toBool()&&!snapshot["cursor_2_visible"].toBool(),"new_path_clears_old_analysis_cursors",checks);
        pass &= check(snapshot["source"].toString()=="UNKNOWN"&&snapshot["value_label"].toString().contains("NA"),"new_input_waits_for_own_values",checks);
        pass &= check(!window.app_save_analysis("new_source_analysis.json"),"old_analysis_cannot_be_exported_as_new_source",checks);
        QFile next("new_fixture.txt"); next.open(QIODevice::WriteOnly); next.write(sim(0)); next.close();
        invoke(window,"slotDraw"); invoke(window,"slotReadFileData"); invoke(window,"slotStop");
        window.app_analyze_selection(APP_SIGNAL_HUMIDITY,0,0);
        window.app_configure_input(QDir::current().absoluteFilePath("new_fixture.txt"),"FILE_STREAM",QDir::current().absoluteFilePath("sessions"));
        pass &= check(window.app_status_snapshot()["ui_points"].toInt()==0&&!window.app_status_snapshot()["selection_active"].toBool(),"mode_only_change_also_clears_source_view",checks);
    } else return 2;
    invoke(window,"slotStop");
    pass &= check(!window.app_status_snapshot()["recording"].toBool(),"recording_off",checks);
    QJsonObject output=window.app_status_snapshot();
    const auto summaryLabel=window.findChild<QLabel*>("lbl_paramResult");
    output["analysis_label_height"]=summaryLabel->height();
    output["analysis_label_height_for_width"]=summaryLabel->heightForWidth(summaryLabel->width());
    output["analysis_label_minimum_height"]=summaryLabel->minimumHeight();
    output["case"]=mode; output["checks"]=checks; output["pass"]=pass;
    output["classification"]="SOFTWARE FIXTURE / REPLAY; NOT TESTED ON HARDWARE";
    QTextStream(stdout) << QJsonDocument(output).toJson(QJsonDocument::Compact) << '\n';
    return pass ? 0 : 1;
}
