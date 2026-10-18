// Synthetic faults/Qt native mouse events. This does not physically unplug USB.
#include <QApplication>
#include <QTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QCheckBox>
#include <QToolTip>
#include <QHelpEvent>
#include <QTextStream>
#include <QScreen>
#include <cmath>
#include "mainwindow.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

struct app_window_test_access_t {
    static void stop_file_timer(MainWindow &w) { w.m_fileTimer->stop(); }
    static void synthetic_serial(MainWindow &w) {
        w.m_fileTimer->stop();w.m_inputMode="SERIAL";w.m_serialPort="INJECTED_NO_PHYSICAL_COM";
        app_serial_device_t device;device.port=w.m_serialPort;
        w.m_recovery.start(device,w.m_hostClock.elapsed());
    }
    static void feed(MainWindow &w,const QByteArray &bytes) {
        app_line_buffer_t lines;auto batch=lines.app_line_feed_bytes(bytes);w.app_consume_records(batch.records);
        const auto pending=lines.app_line_take_pending();
        if(!pending.isEmpty()) { app_line_record_t r;r.bytes=pending;w.app_consume_records({r}); }
        w.updatePlotRealTime();w.app_render_state();
    }
    static void error(MainWindow &w,QSerialPort::SerialPortError e) { w.app_serial_error(e); }
    static void tick(MainWindow &w) { w.app_serial_tick(); }
    static void pending(MainWindow &w,const QString &s) { w.m_pendingCommand=s; }
    static void requested_profile(MainWindow &w,int profile) { w.m_requestedCalibration=profile;w.m_calibrationConfirmed=false;w.m_effectiveCalibration=-1; }
    static QJsonArray events(MainWindow &w) { QJsonArray a;for(const auto &e:w.m_displayEvents) a.append(e);return a; }
    static void dense_events(MainWindow &w) {
        for(int n=0;n<8;++n) w.app_append_display_event(n%2?"RESUME_DISPLAY":"PAUSE_DISPLAY");
        w.app_render_plot_items();
    }
    static int visible_events(MainWindow &w) { return w.m_visibleEvents.size(); }
    static void render_events(MainWindow &w) { w.app_render_plot_items(); }
    static void unknown_event(MainWindow &w) { w.app_append_display_event("UNKNOWN_TIME_TEST",QJsonObject(),false);w.app_render_plot_items(); }
    static void trim_events(MainWindow &w) {
        for(int n=0;n<260;++n) w.app_append_display_event("CACHE_BOUND_TEST");
        w.app_render_plot_items();
    }
};

static int checks=0;static QJsonArray failures;
static void check(bool ok,const QString &label) { ++checks;if(!ok) failures.append(label); }
static void invoke(MainWindow &w,const char *name) { check(QMetaObject::invokeMethod(&w,name,Qt::DirectConnection),QString("slot ")+name); }
static QByteArray sample(quint32 ms,int profile=1,bool evidence=true) {
    // Explicit fixture, not a physical calibration/measurement.
    const int cal=profile;const double corrected=profile==0?26.125:27.025;
    QByteArray text;
    if(evidence) text=QString("[TEMP_CORR] raw=26.125C corrected=%1C offset=%2C raw_bits=41D10000 cal=%3 state=%4 t_ms=%5 profile=%6\r\n")
        .arg(corrected,0,'f',3).arg(corrected-26.125,0,'f',3).arg(cal).arg(cal?"PROVISIONAL":"UNCORRECTED").arg(ms).arg(profile).toLatin1();
    text+=QString("TH3,MIXED,%1,1,71,0,%2,16,%3,42.00,1.400,NA,NA,NA,11000,NA,NA,NA,REAL,SIM,1,1,2\r\n")
        .arg(ms).arg(cal).arg(corrected,0,'f',2).toLatin1();return text;
}
static void begin(MainWindow &w,const QString &name) {
    QFile f(name+".txt");check(f.open(QIODevice::WriteOnly|QIODevice::Truncate),"fixture creation");f.close();
    w.app_configure_input(QFileInfo(f).absoluteFilePath(),"REPLAY",QDir::current().filePath(name+"_sessions"));
    invoke(w,"slotDraw");
    app_window_test_access_t::stop_file_timer(w);
}
static QPoint point(QCustomPlot *plot,double x,double y,int series) {
    return QPoint(qRound(plot->xAxis->coordToPixel(x)),qRound(plot->graph(series)->valueAxis()->coordToPixel(y)));
}
static void click(QCustomPlot *plot,const QPoint &p) { QTest::mouseClick(plot,Qt::LeftButton,Qt::NoModifier,p); }

int main(int argc,char **argv) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc,argv);app.setProperty("appSettingsFile",QDir::current().filePath("preferences.ini"));
    if(argc>1 &&QString::fromLocal8Bit(argv[1])=="--describe-scale") {
        QTextStream(stdout)<<QJsonDocument(QJsonObject{{"native_qt_dpr",QGuiApplication::primaryScreen()->devicePixelRatio()}}).toJson();return 0;
    }
    QCoreApplication::setApplicationVersion("v010.15.1");
    QJsonObject result;result["classification"]="SYNTHETIC SERIAL FAULTS / NATIVE QT AUTOMATED GUI EVENTS; NOT PHYSICAL CABLE PULL";
    // Pure connection policy uses supplied clock and device catalog.
    app_serial_device_t expected;expected.port="COM_TEST_1";expected.serial_number="FIXTURE_UNIQUE";
    expected.has_vendor=expected.has_product=true;expected.vendor=0x1a86;expected.product=0x7523;
    app_serial_recovery_t policy;policy.start(expected,0);
    check(!policy.stale(3499)&&policy.stale(3500),"3.5 second stale deadline");
    check(policy.snapshot()["state"]=="STALE","stale is explicit");policy.frame(3600);
    for(auto e:{QSerialPort::NoError,QSerialPort::ParityError,QSerialPort::FramingError,QSerialPort::BreakConditionError,QSerialPort::TimeoutError}) {
        const auto action=policy.error(e,4000);check(action!=app_serial_recovery_t::RECOVERY_RELEASE_HANDLE,"noisy/timeout errors keep stream");
    }
    policy.error(QSerialPort::ResourceError,4000);check(!policy.due(4999)&&policy.due(5000),"bounded first backoff");
    auto wrong=expected;wrong.serial_number="OTHER_DEVICE";
    check(policy.candidate({wrong},5000).isEmpty()&&policy.attempts()==1,"never reconnect other CH340");
    check(!policy.due(7999)&&policy.due(8000),"second backoff");
    auto renamed=expected;renamed.port="COM_TEST_2";
    check(policy.candidate({renamed},8000)=="COM_TEST_2","unique unchanged identity may renumber");
    policy.open_failed(QSerialPort::DeviceNotFoundError,8000);
    check(policy.due(15000)&&policy.candidate({},15000).isEmpty()&&!policy.due(999999),"three attempts exhausted");
    policy.start(expected,0);policy.error(QSerialPort::ResourceError,0);
    check(policy.candidate({expected,renamed},1000).isEmpty()&&!policy.due(999999),"ambiguous USB identity requires manual selection");
    for(auto e:{QSerialPort::PermissionError,QSerialPort::OpenError,QSerialPort::UnsupportedOperationError}) {
        policy.start(expected,0);policy.error(e,0);check(!policy.due(999999),"no permission/ownership bypass");
    }
    app_serial_device_t anonymous;anonymous.port="COM_TEST_1";policy.start(anonymous,0);policy.error(QSerialPort::ResourceError,0);
    check(!policy.automatic_identity()&&!policy.due(999999),"missing serial identity disables automatic reopen");
    policy.start(expected,0);policy.error(QSerialPort::ResourceError,0);policy.stop();check(!policy.due(999999),"End Session cancels policy");
    policy.start(expected,0);policy.error(QSerialPort::ReadError,0);policy.candidate({expected},1000);policy.opened(1000);policy.frame(1500);
    policy.error(QSerialPort::ResourceError,2000);check(policy.attempts()==1,"one good frame does not replenish retry budget");

    MainWindow faults;begin(faults,"faults");app_window_test_access_t::synthetic_serial(faults);
    app_window_test_access_t::feed(faults,sample(1000));auto before=faults.app_status_snapshot();
    app_window_test_access_t::feed(faults,"TH3,broken\r\npartial_without_LF");
    check(faults.app_status_snapshot()["running"].toBool()&&faults.app_status_snapshot()["recording"].toBool(),"bad/incomplete records do not end session");
    check(faults.app_status_snapshot()["ui_points"]==before["ui_points"],"bad/incomplete bytes are not samples");
    for(auto e:{QSerialPort::FramingError,QSerialPort::TimeoutError}) app_window_test_access_t::error(faults,e);
    check(faults.app_status_snapshot()["running"].toBool(),"transient Qt errors retain acquisition");
    app_window_test_access_t::feed(faults,sample(2000));
    app_window_test_access_t::pending(faults,"MODE?");app_window_test_access_t::feed(faults,"RSP,MODE,1,2,REAL,SIM,2,REAL,SIM\r\n");
    app_window_test_access_t::feed(faults,sample(3000));check(faults.app_status_snapshot()["mode_confirmed"].toBool(),"live MODE and matching TH3 confirm mode");
    app_window_test_access_t::error(faults,QSerialPort::ResourceError);
    auto lost=faults.app_status_snapshot();check(lost["running"].toBool()&&lost["recording"].toBool()&&!lost["mode_confirmed"].toBool(),"disconnect retains recorder but invalidates confirmation");
    check(lost["ui_points"].toInt()==3&&lost["connection_state"]=="DISCONNECTED","anonymous disconnect preserves graph and requires manual reconnect");
    app_window_test_access_t::feed(faults,sample(7000));auto restored=faults.app_status_snapshot();
    check(restored["ui_points"].toInt()==4&&restored["segment_id"].toInt()>before["segment_id"].toInt(),"recovery creates new source segment without erasing history");
    check(std::abs(restored["last_plot_time_s"].toDouble()-6.0)<1e-9,"known MCU time delta defines gap, not buffered receive latency");
    bool gap=false;auto plot=faults.findChild<QCustomPlot*>("customPlot");
    for(auto it=plot->graph(0)->data()->constBegin();it!=plot->graph(0)->data()->constEnd();++it) if(!std::isfinite(it->value)) gap=true;
    check(gap,"plot geometry contains an explicit NaN segment gap");
    app_window_test_access_t::feed(faults,sample(100));check(faults.app_status_snapshot()["ui_points"].toInt()==5,"MCU timestamp reset retains preceding samples");
    app_window_test_access_t::error(faults,QSerialPort::PermissionError);
    check(faults.app_status_snapshot()["connection_state"]=="BUSY_OR_DENIED"&&faults.app_status_snapshot()["running"].toBool(),"permission is blocked, not silently successful");
    QTimer::singleShot(0,&faults,[&faults]() { app_window_test_access_t::tick(faults); });
    invoke(faults,"slotStop");QCoreApplication::processEvents();auto ended=faults.app_status_snapshot();
    check(!ended["running"].toBool()&&!ended["recording"].toBool()&&!ended["serial_open"].toBool(),"End Session wins queued recovery race");
    check(ended["record_summary"].toObject()["stop_reason"]=="end_session","explicit End Session finalizes once");
    result["fault_end"]=ended;result["fault_events"]=app_window_test_access_t::events(faults);
    faults.setWindowTitle("SYNTHETIC SERIAL TEST - no physical COM");faults.setAttribute(Qt::WA_ShowWithoutActivating);faults.resize(1100,720);faults.show();QTest::qWait(50);
    faults.app_set_x_window("MANUAL",32);plot->replot();
    click(plot,point(plot,1,26.125,0));auto historical=faults.app_status_snapshot()["inspection"].toObject();
    check(historical["kind"]=="POINT"&&historical["source_timestamp_ms"].toInt()==2000&&historical["segment_id"]==before["segment_id"],"historical segment inspection retains exact original source time");
    click(plot,point(plot,4.0,26.125,0));check(faults.app_status_snapshot()["inspection"].toObject().isEmpty(),"gap between segments has no invented sampled value");faults.hide();
    faults.app_analyze_selection(APP_SIGNAL_TEMPERATURE,6,6,quint64(restored["segment_id"].toDouble()));
    const auto segmentAnalysis=faults.app_status_snapshot()["selection"].toObject();
    check(segmentAnalysis["source_elapsed_start_s"].toDouble(-1)==0&&segmentAnalysis["source_segment_plot_origin_s"].toDouble()==6,
          "Measure maps later segment plot coordinates back to exact source elapsed time");
    MainWindow sim;begin(sim,"SIM_readback");app_window_test_access_t::synthetic_serial(sim);
    const QByteArray sim1="TH3,SIM,1000,1,3,0,0,16,26.13,42.00,NA,NA,NA,NA,NA,NA,NA,NA,SIM,SIM,1,1,3\r\n";
    app_window_test_access_t::feed(sim,sim1);
    app_window_test_access_t::pending(sim,"MODE?");app_window_test_access_t::feed(sim,"RSP,MODE,1,3,SIM,SIM,3,SIM,SIM\r\n");
    app_window_test_access_t::pending(sim,"CAL?");app_window_test_access_t::feed(sim,"RSP,CAL,1,3,3\r\n");
    app_window_test_access_t::feed(sim,QByteArray(sim1).replace("SIM,1000,","SIM,2000,"));auto simState=sim.app_status_snapshot();
    check(simState["mode_confirmed"].toBool()&&simState["calibration_confirmed"].toBool(),"SIM configuration confirmed from CAL readback without fabricated physical sidecar");
    check(!simState["calibration_applied"].toBool()&&simState["temperature_corrected_c"].isNull(),"SIM never receives empirical temperature correction");
    check(simState["calibration_state"].toString().contains("SIM: no empirical correction"),"configured versus applied SIM status is explicit");
    app_window_test_access_t::requested_profile(sim,0);
    app_window_test_access_t::pending(sim,"CAL?");app_window_test_access_t::feed(sim,"RSP,CAL,1,0,0\r\n");
    app_window_test_access_t::feed(sim,QByteArray(sim1).replace("SIM,1000,","SIM,3000,"));
    check(sim.app_status_snapshot()["calibration_confirmed"].toBool()&&!sim.app_status_snapshot()["corrected_temperature_checkbox_enabled"].toBool(),"Off readback disables corrected checkbox in SIM");
    invoke(sim,"slotStop");result["SIM_configuration"]=sim.app_status_snapshot();

    MainWindow gui;gui.setAttribute(Qt::WA_ShowWithoutActivating);gui.resize(1100,720);gui.show();QTest::qWait(80);
    begin(gui,"inspection");QByteArray records;for(int n=0;n<20;++n) records+=sample(1000+n*1000);
    app_window_test_access_t::feed(gui,records);QCoreApplication::processEvents();
    plot=gui.findChild<QCustomPlot*>("customPlot");gui.app_set_x_window("MANUAL",32);plot->replot();
    for(int curve=0;curve<3;++curve) {
        const auto p=point(plot,10,curve==0?26.125:curve==1?42:27.025,curve);click(plot,p);
        const auto inspected=gui.app_status_snapshot()["inspection"].toObject();
        check(inspected["kind"]=="POINT"&&inspected["series_index"].toInt()==curve,"native click selects nearest visible curve");
        check(inspected["source_elapsed_s"].toDouble()==10&&inspected["source_timestamp_ms"].toInt()==11000,"inspector exact sampled source time");
        check(inspected["physical_temperature_c"].toDouble()==26.125&&inspected["humidity_source"]=="SIM","inspector retains float32 original and SIM label");
        QElapsedTimer fade;fade.start();
        while(!QToolTip::isVisible() &&fade.elapsed()<750) QTest::qWait(10);
        QJsonObject tip{{"text",QToolTip::text()},{"visible",QToolTip::isVisible()},{"native_fade_wait_ms",double(fade.elapsed())}};
        result[QString("tooltip_state_%1").arg(curve)]=tip;
        check(QToolTip::text().contains("Raw")&&QToolTip::isVisible(),"native compact tooltip rendered");
        for(auto top:QApplication::topLevelWidgets()) if(top->inherits("QTipLabel")) top->grab().save(QString("point_%1_tooltip.png").arg(curve));
    }
    app_window_test_access_t::feed(gui,sample(20500));
    check(QToolTip::isVisible(),"live redraw retains an inspected sampled point instead of dismissing it every frame");
    auto rawCheck=gui.findChild<QCheckBox*>("showRawTemperature");rawCheck->setChecked(false);
    click(plot,point(plot,10,26.125,0));auto hidden=gui.app_status_snapshot()["inspection"].toObject();
    check(hidden.isEmpty()||hidden["series_index"].toInt()!=0,"hidden raw curve is not a target; nearby visible humidity is allowed");result["hidden_curve_click"]=hidden;
    auto humidityCheck=gui.findChild<QCheckBox*>("showHumidity");humidityCheck->setChecked(false);
    click(plot,point(plot,10,26.125,0));check(gui.app_status_snapshot()["inspection"].toObject().isEmpty(),"hidden curves with no other nearby sample produce no value");
    humidityCheck->setChecked(true);rawCheck->setChecked(true);
    click(plot,point(plot,10,20,0));check(gui.app_status_snapshot()["inspection"].toObject().isEmpty(),"empty space produces no value");
    click(plot,point(plot,10,plot->yAxis->range().lower+.25,0));
    check(gui.app_status_snapshot()["inspection"].toObject().isEmpty(),"in-plot empty space produces no value");
    check(!gui.app_status_snapshot()["temperature_y_manual"].toBool(),"short clicks do not change Y management");
    app_window_test_access_t::feed(gui,sample(21000,1,false));
    click(plot,point(plot,20,27.025,2));auto missing=gui.app_status_snapshot()["inspection"].toObject();
    check(missing["kind"]=="POINT"&&missing["physical_temperature_c"].isNull()&&!missing["original_pair_available"].toBool(),"missing original stays N/A, never inferred");
    invoke(gui,"slotPauseDisplay");invoke(gui,"slotPauseDisplay");app_window_test_access_t::dense_events(gui);plot->replot();
    const QPoint eventPoint(qRound(plot->xAxis->coordToPixel(20)),plot->axisRect()->rect().center().y());
    click(plot,eventPoint);auto event=gui.app_status_snapshot()["inspection"].toObject();
    check(event["kind"]=="EVENT"&&event["nearby_count"].toInt()==10,"events outrank points and aggregate dense lines");
    check(event["events"].toArray()[0].toObject()["host_event_timestamp"].toString().endsWith('Z'),"event tooltip uses recorded UTC time");
    click(plot,eventPoint);check(gui.app_status_snapshot()["inspection"].toObject()["page_offset"].toInt()==3,"repeat click cycles nearby events");
    QHelpEvent hover(QEvent::ToolTip,eventPoint,plot->mapToGlobal(eventPoint));QApplication::sendEvent(plot,&hover);
    check(gui.app_status_snapshot()["inspection"].toObject()["kind"]=="EVENT","native event hover inspection");
    for(auto top:QApplication::topLevelWidgets()) if(top->inherits("QTipLabel")) top->grab().save("events_tooltip.png");
    invoke(gui,"slotMeasure");click(plot,point(plot,5,26.125,0));
    check(gui.app_status_snapshot()["selection_pending"].toBool()&&gui.app_status_snapshot()["inspection"].toObject().isEmpty(),"Measure mode has priority over inspector");
    click(plot,point(plot,10,26.125,0));check(gui.app_status_snapshot()["selection_active"].toBool(),"two-cursor analysis still works");
    QPoint drag=plot->axisRect()->rect().center();QTest::mousePress(plot,Qt::LeftButton,Qt::NoModifier,drag);
    QMouseEvent move(QEvent::MouseMove,QPointF(drag+QPoint(0,30)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(plot,&move);
    QTest::mouseRelease(plot,Qt::LeftButton,Qt::NoModifier,drag+QPoint(0,30));
    check(gui.app_status_snapshot()["humidity_y_manual"].toBool()||gui.app_status_snapshot()["temperature_y_manual"].toBool(),"drag remains independent Y manipulation");
    gui.app_reset_y_scale();invoke(gui,"slotPauseDisplay");app_window_test_access_t::feed(gui,sample(22000));
    click(plot,point(plot,10,26.125,0));check(gui.app_status_snapshot()["inspection"].toObject()["source_timestamp_ms"].toInt()==11000,"paused inspector uses displayed immutable sample snapshot");
    gui.grab().save("native_inspection_window.png");invoke(gui,"slotPauseDisplay");invoke(gui,"slotStop");
    check(gui.app_about_text().contains("Not verified / Disconnected")&&!gui.app_about_text().contains("v0.6.1"),"About does not invent disconnected MCU version");
    result["native_gui_end"]=gui.app_status_snapshot();result["device_pixel_ratio"]=gui.devicePixelRatioF();
    // Separate visible-cache tests keep the complete append-only event sidecar.
    MainWindow bounded;bounded.setAttribute(Qt::WA_ShowWithoutActivating);bounded.resize(1100,720);bounded.show();begin(bounded,"bounded");
    app_window_test_access_t::feed(bounded,records);invoke(bounded,"slotPauseDisplay");invoke(bounded,"slotPauseDisplay");
    const int visibleBefore=app_window_test_access_t::visible_events(bounded);app_window_test_access_t::unknown_event(bounded);
    check(app_window_test_access_t::visible_events(bounded)==visibleBefore,"unknown source event not assigned a fabricated line");
    app_window_test_access_t::trim_events(bounded);auto bs=bounded.app_status_snapshot();
    check(bs["display_event_cache_count"].toInt()==256&&bs["record_summary"].toObject()["event_count"].toInt()==263,"trimmed annotation cache preserves full JSONL event history");
    auto bp=bounded.findChild<QCustomPlot*>("customPlot");bp->xAxis->setRange(0,5);app_window_test_access_t::render_events(bounded);
    check(app_window_test_access_t::visible_events(bounded)==0,"offscreen events do not remain clickable");
    bounded.app_set_x_window("MANUAL",32);app_window_test_access_t::feed(bounded,sample(2000000));
    check(app_window_test_access_t::visible_events(bounded)==0,"old events outside retained view are not stale click targets");invoke(bounded,"slotStop");bounded.hide();
#ifdef Q_OS_WIN
    MainWindow disk;begin(disk,"metadata_failure");app_window_test_access_t::synthetic_serial(disk);
    const QString metadata=disk.app_status_snapshot()["record_directory"].toString()+"/metadata.json";
    HANDLE locked=CreateFileW(reinterpret_cast<const wchar_t*>(metadata.utf16()),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    check(locked!=INVALID_HANDLE_VALUE,"owned metadata lock acquired");
    app_window_test_access_t::error(disk,QSerialPort::ResourceError);
    check(!disk.app_status_snapshot()["running"].toBool()&&!disk.app_status_snapshot()["recording"].toBool()&&disk.app_status_snapshot()["state"]=="ERROR","failed metadata commit remains fatal and disables recovery");
    if(locked!=INVALID_HANDLE_VALUE) CloseHandle(locked);
    app_window_test_access_t::tick(disk);check(!disk.app_status_snapshot()["running"].toBool(),"disk error cannot restart session");
    result["intentional_metadata_failure"]=disk.app_status_snapshot();
#endif
    result["checks"]=checks;result["failures"]=failures;result["pass"]=failures.isEmpty();
    QTextStream out(stdout);out.setCodec("UTF-8");out<<QJsonDocument(result).toJson();return failures.isEmpty()?0:1;
}
