// Actual UART and owned-handle conflict; labelled software delay/error injection.
#include <QApplication>
#include <QTest>
#include <QScreen>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalBlocker>
#include <QTextStream>
#include <functional>
#include "mainwindow.h"

struct app_window_test_access_t {
    static void event(MainWindow &w,const QString &type,const QJsonObject &detail) { w.app_record_connection_event(type,detail); }
    static void drain(MainWindow &w) { w.app_read_serial(); }
};
static int checks=0;static QJsonArray failures;
static void check(bool value,const QString &name) { ++checks;if(!value) failures.append(name); }
static bool waitFor(const std::function<bool()> &condition,int timeout=12000) {
    QElapsedTimer timer;timer.start();
    for(;;) { if(condition()) return true;if(timer.elapsed()>=timeout) return false;QTest::qWait(20); }
}
static void invoke(MainWindow &w,const char *slot) { check(QMetaObject::invokeMethod(&w,slot,Qt::DirectConnection),slot); }
static bool ready(MainWindow &w) {
    auto s=w.app_status_snapshot();return s["mode_confirmed"].toBool()&&s["calibration_confirmed"].toBool()
        &&s["firmware_current_connection_verified"].toBool();
}
int main(int argc,char **argv) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc,argv);app.setProperty("appSettingsFile",QDir::current().filePath("preferences.ini"));
    if(argc>1 &&QString::fromLocal8Bit(argv[1])=="--describe-scale") {
        QTextStream(stdout)<<QJsonDocument(QJsonObject{{"native_qt_dpr",QGuiApplication::primaryScreen()->devicePixelRatio()}}).toJson();return 0;
    }
    if(argc!=3) return 2;
    QCoreApplication::setApplicationVersion("v010.15.1");
    const QString port=QString::fromLocal8Bit(argv[1]);const int duration=QString::fromLocal8Bit(argv[2]).toInt();
    MainWindow w;w.setAttribute(Qt::WA_ShowWithoutActivating);w.show();
    w.app_configure_serial(port,115200,QDir::current().filePath("sessions"));invoke(w,"slotDraw");
    QJsonObject result{{"classification","ACTUAL UART; explicitly labelled software receive-delay/ResourceError injections; no physical cable pull"},
                       {"board_id","THM-BOARD-01"},{"device_pixel_ratio",w.devicePixelRatioF()}};
    const bool started=waitFor([&]() { return ready(w); },18000);check(started,"live TH3/MODE/STATUS/CAL readiness");
    if(started) {
        const auto initial=w.app_status_snapshot();result["initial"]=initial;
        const QString originalT=initial["frame_temperature_source"].toString(),originalH=initial["frame_humidity_source"].toString();
        const int originalCal=initial["calibration_effective"].toInt();
        // A second owned handle produces a real Windows exclusive-open denial;
        // the first application's port remains untouched throughout this case.
        MainWindow contender;contender.app_configure_serial(port,115200,QDir::current().filePath("owned_conflict_sessions"));invoke(contender,"slotDraw");
        const auto denied=contender.app_status_snapshot();result["actual_owned_handle_denial"]=denied;
        check(!denied["serial_open"].toBool()&&denied["connection_state"]=="BUSY_OR_DENIED","actual owned second-open PermissionError is blocked");
        check(w.app_status_snapshot()["serial_open"].toBool()&&w.app_status_snapshot()["recording"].toBool(),"denied second handle cannot steal first recorder");
        check(waitFor([&]() { return w.app_request_sources("REAL","SIM"); }),"explicit REAL/SIM request");
        check(waitFor([&]() { auto s=w.app_status_snapshot();return s["mode_confirmed"].toBool()&&s["frame_temperature_source"]=="REAL"&&s["frame_humidity_source"]=="SIM"; }),"explicit REAL/SIM observed");
        QJsonArray profiles;
        for(int p:{0,1,2,3}) {
            check(waitFor([&]() { return w.app_request_calibration(p); }),"CAL request accepted");
            check(waitFor([&]() { auto s=w.app_status_snapshot();return s["calibration_confirmed"].toBool()&&s["calibration_frame_profile"].toInt()==p; }),"CAL effective readback and original sidecar agree");
            profiles.append(w.app_status_snapshot());
        }
        result["actual_profiles"]=profiles;
        auto transport=static_cast<app_serial_input_t*>(w.findChild<QSerialPort*>("serialTransport"));
        check(transport!=nullptr,"actual QSerialPort transport found");
        const int beforeDelay=w.app_status_snapshot()["parsed_count"].toInt();
        app_window_test_access_t::event(w,"TEST_RECEIVE_DELAY_BEGIN",{{"software_injection",true},{"mechanism","Block Qt serial callbacks for 4 seconds; device remains attached and buffers data"}});
        {
            const QSignalBlocker blocked(transport);QTest::qWait(4100);
            result["stale_during_injected_receive_delay"]=w.app_status_snapshot();
            check(w.app_status_snapshot()["state"]=="STALE"&&w.app_status_snapshot()["recording"].toBool(),"actual port retains recording during injected no-read interval");
            check(w.app_status_snapshot()["parsed_count"].toInt()==beforeDelay,"stale interval adds no fabricated frames");
        }
        app_window_test_access_t::drain(w);
        check(waitFor([&]() { return ready(w)&&w.app_status_snapshot()["parsed_count"].toInt()>beforeDelay; }),"buffered actual frames resume with new segment and readback");
        app_window_test_access_t::event(w,"TEST_RECEIVE_DELAY_END",{{"software_injection",true}});
        const QString sameSession=w.app_status_snapshot()["record_directory"].toString();const int retained=w.app_status_snapshot()["ui_points"].toInt();
        const QString commandPath=QDir(sameSession).filePath("command_tx.jsonl");const qint64 beforeReconnectTX=QFileInfo(commandPath).size();
        app_window_test_access_t::event(w,"TEST_RESOURCE_ERROR_INJECTION",{{"software_injection",true},{"mechanism","Explicit errorOccurred signal; not physical USB removal"}});
        check(QMetaObject::invokeMethod(transport,"errorOccurred",Qt::DirectConnection,Q_ARG(QSerialPort::SerialPortError,QSerialPort::ResourceError)),"explicit synthetic ResourceError signal delivered");
        auto disconnected=w.app_status_snapshot();result["injected_disconnect"]=disconnected;
        check(disconnected["running"].toBool()&&disconnected["recording"].toBool()&&!disconnected["serial_open"].toBool(),"injected lost handle preserves actual acquisition session");
        check(!disconnected["mode_confirmed"].toBool()&&!disconnected["calibration_confirmed"].toBool(),"old connection confirmation invalidated");
        check(w.app_reconnect_serial(),"explicit manual reconnect to same selected port");
        check(waitFor([&]() { return ready(w); },18000),"actual reopen independently reconfirms MODE/STATUS/CAL/TH3");
        auto recovered=w.app_status_snapshot();result["actual_manual_recovery"]=recovered;
        check(recovered["record_directory"].toString()==sameSession&&recovered["ui_points"].toInt()>=retained,"one recorder and preceding graph retained across recovery");
        QFile commandLog(commandPath);check(commandLog.open(QIODevice::ReadOnly)&&commandLog.seek(beforeReconnectTX),"recovery TX evidence readable");
        bool queriesOnly=true;
        while(!commandLog.atEnd()) {
            const auto entry=QJsonDocument::fromJson(commandLog.readLine()).object();const auto bytes=QByteArray::fromHex(entry["bytes_hex"].toString().toLatin1());
            if(bytes.startsWith("CMD,SET,") ||bytes.startsWith("CMD,CAL,")) queriesOnly=false;
        }
        check(queriesOnly,"reconnect only observes actual configuration; never replays SET/CAL");
        if(recovered["frame_temperature_source"]!="REAL" ||recovered["frame_humidity_source"]!="SIM") {
            check(waitFor([&]() { return w.app_request_sources("REAL","SIM"); }),"explicit post-recovery REAL/SIM stability request");
            check(waitFor([&]() { auto s=w.app_status_snapshot();return s["mode_confirmed"].toBool()&&s["frame_temperature_source"]=="REAL"&&s["frame_humidity_source"]=="SIM"; }),"explicit stability source observed");
        }
        const int before=w.app_status_snapshot()["parsed_count"].toInt();QTest::qWait(duration*1000);
        const auto stable=w.app_status_snapshot();result["stability_end"]=stable;result["stability_wall_seconds"]=duration;
        check(stable["parsed_count"].toInt()-before>=int(duration*.85),"actual bounded stability frame count");
        check(stable["temperature_valid"].toBool()&&stable["frame_temperature_source"]=="REAL","actual REAL temperature remains explicitly valid");
        check(waitFor([&]() { return w.app_request_calibration(originalCal); }),"restore pretest calibration request");
        check(waitFor([&]() { auto s=w.app_status_snapshot();return s["calibration_confirmed"].toBool()&&s["calibration_effective"].toInt()==originalCal; }),"pretest calibration restored");
        check(waitFor([&]() { return w.app_request_sources(originalT,originalH); }),"restore pretest source request");
        check(waitFor([&]() { auto s=w.app_status_snapshot();return s["mode_confirmed"].toBool()&&s["frame_temperature_source"]==originalT&&s["frame_humidity_source"]==originalH; }),"pretest source restored");
        w.grab().save("actual_UART_recovery.png");
    }
    invoke(w,"slotStop");QTest::qWait(300);result["final"]=w.app_status_snapshot();
    check(!w.app_status_snapshot()["serial_open"].toBool()&&!w.app_status_snapshot()["recording"].toBool(),"End Session releases actual COM and files");
    result["checks"]=checks;result["failures"]=failures;result["pass"]=failures.isEmpty();
    QTextStream out(stdout);out.setCodec("UTF-8");out<<QJsonDocument(result).toJson();return failures.isEmpty()?0:1;
}
