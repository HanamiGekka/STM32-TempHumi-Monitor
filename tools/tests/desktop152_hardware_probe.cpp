// Actual UART with owned contention and explicitly labelled injected loss.
// Optional host-controlled SWD reset is separately identity gated; no Flash write.
// No physical USB/power-cycle or independent accuracy claim.
#include "mainwindow.h"
#include <QApplication>
#include <QTest>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCheckBox>
#include <QComboBox>
#include <QMessageBox>
#include <QPushButton>
#include <QTextStream>
#include <QSignalBlocker>
#include <functional>

struct app_window_test_access_t {
    static void event(MainWindow &w,const QString &name) {w.app_record_connection_event(name,{{"software_test_injection",true}});}
    static void loss(MainWindow &w) {w.app_serial_error(QSerialPort::ResourceError);}
    static bool confirmed(MainWindow &w) {return w.app_live_configuration_confirmed() &&w.m_mcuIdentityCurrent &&w.m_pendingCommand.isEmpty();}
    static void mismatch(MainWindow &w) {w.m_restoreTarget["firmware"]="TEST_DIFFERENT_PEER_NOT_ACTUAL_FIRMWARE";}
    static void mismatch_uid(MainWindow &w) {w.m_restoreTarget["mcu_identity_sha256"]="TEST_DIFFERENT_MCU_UID";}
    static void manual_y(MainWindow &w) {w.m_temperatureYManual=w.m_humidityYManual=true;}
    static void drain(MainWindow &w) {w.app_read_serial();}
    static bool fit_edited(MainWindow &w) {return w.m_calibrationChoiceEdited;}
    static int boots(MainWindow &w) {int n=0;for(const auto &e:w.m_displayEvents) if(e["event_type"]=="MCU_BOOT_OBSERVED") ++n;return n;}
};
static int checks=0;static QJsonArray failures;
static void check(bool value,const QString &label) {++checks;if(!value) failures.append(label);}
static bool waitFor(const std::function<bool()> &condition,int ms=16000) {
    QElapsedTimer timer;timer.start();
    while(!condition()) {if(timer.elapsed()>ms) return false;QTest::qWait(25);}return true;
}
static void invoke(MainWindow &w,const char *slot) {check(QMetaObject::invokeMethod(&w,slot,Qt::DirectConnection),slot);}
static QJsonObject restore(MainWindow &w) {return w.app_status_snapshot()["configuration_restore"].toObject();}
static QList<QByteArray> commands(const QString &path,qint64 offset) {
    QFile f(path);QList<QByteArray> result;
    if(!f.open(QIODevice::ReadOnly)||!f.seek(offset)) return result;
    while(!f.atEnd()) result.append(QByteArray::fromHex(QJsonDocument::fromJson(f.readLine()).object()["bytes_hex"].toString().toLatin1()));
    return result;
}
static bool onlyQueries(const QList<QByteArray> &tx) {for(const auto &b:tx) if(b.startsWith("CMD,SET,")||b.startsWith("CMD,CAL,")) return false;return !tx.isEmpty();}
static void answerExit(bool yes) {
    QTimer::singleShot(50,[yes](){for(auto top:QApplication::topLevelWidgets()) if(auto box=qobject_cast<QMessageBox*>(top)) {
        if(box->objectName()!="exitConfirmation") continue;
        check(box->defaultButton()&&box->defaultButton()->text()=="No, Continue","actual UART Exit defaults No");
        for(auto b:box->buttons()) if(b->text()==(yes?"Yes, Exit":"No, Continue")) {QTest::mouseClick(b,Qt::LeftButton);return;}
    }});
}
int main(int argc,char **argv) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    app.setProperty("appSettingsFile",QDir::current().filePath("preferences.ini"));
    if(argc!=4 &&argc!=5) return 2;
    QCoreApplication::setApplicationVersion(QString::fromLatin1(argv[2]));
    const QString port=QString::fromLatin1(argv[1]);
    const QString expectedFirmware=QString::fromLatin1(argv[3]);
    QJsonObject result{{"classification","ACTUAL UART / OWNED CONTENTION; labelled ResourceError and volatile-config test injections, NOT PHYSICAL USB PULL"},
      {"board_id","THM-BOARD-01"},{"firmware_flash_performed",false}};
    // Never steal a handle. Failure of this initial exclusive open stops work.
    QSerialPort guard;guard.setPortName(port);guard.setBaudRate(115200);
    if(!guard.open(QIODevice::ReadWrite)) {
        result["pass"]=false;result["blocked"]=true;result["reason"]="Selected port unavailable; no other process was stopped";
        QTextStream(stdout)<<QJsonDocument(result).toJson();return 3;
    }
    guard.close();MainWindow w;w.resize(1100,720);w.show();
    w.app_configure_serial(port,115200,QDir::current().filePath("sessions"));
    w.app_set_recovery_options(true,true,false);invoke(w,"slotDraw");
    const bool started=waitFor([&](){return app_window_test_access_t::confirmed(w);},18000);
    check(started,"actual fresh TH3/MODE/STATUS/CAL confirmed");
    if(started) {
        const auto initial=w.app_status_snapshot();result["initial"]=initial;
        const QString originalT=initial["frame_temperature_source"].toString(),originalH=initial["frame_humidity_source"].toString();
        const int originalFit=initial["calibration_effective"].toInt();
        result["actual_observed_firmware"]=initial["observed_firmware"];
        check(initial["observed_firmware"]==expectedFirmware,"actual MCU equals explicitly expected firmware");
        // An explicitly disabled contender must wait; new application defaults are ON.
        MainWindow contender;contender.app_configure_serial(port,115200,QDir::current().filePath("denied_sessions"));
        contender.app_set_recovery_options(false,false,false);invoke(contender,"slotDraw");
        auto denied=contender.app_status_snapshot();result["actual_owned_denial"]=denied;
        check(!denied["serial_open"].toBool()&&denied["running"].toBool()&&denied["recording"].toBool(),"owned PermissionError preserves empty contender session");
        check(denied["connection_state"]=="WAITING_FOR_USER","explicit-off denied contender waits for user");invoke(contender,"slotStop");
        check(w.app_status_snapshot()["serial_open"].toBool(),"second-open cannot steal actual handle");
        check(waitFor([&](){return w.app_request_sources("REAL","SIM");}),"explicit test REAL/SIM request");
        check(waitFor([&](){auto s=w.app_status_snapshot();return app_window_test_access_t::confirmed(w)&&s["frame_temperature_source"]=="REAL"&&s["frame_humidity_source"]=="SIM";}),"test REAL/SIM confirmed");
        check(waitFor([&](){return w.app_request_calibration(3);}),"explicit test Fit3 request");
        check(waitFor([&](){return app_window_test_access_t::confirmed(w)&&w.app_status_snapshot()["calibration_effective"].toInt()==3;}),"test Fit3 confirmed");
        QTest::qWait(400);check(restore(w)["last_confirmed"].toObject()["profile"].toInt()==3,"readback+TH3 captured confirmed fit");
        auto transport=w.findChild<QSerialPort*>("serialTransport");check(transport!=nullptr,"owned live UART transport found");
        if(transport) {
            const int beforeDelay=w.app_status_snapshot()["parsed_count"].toInt();
            app_window_test_access_t::event(w,"TEST_BUFFERED_RECEIVE_DELAY_BEGIN");
            {
                const QSignalBlocker blocked(transport);QTest::qWait(4100);
                check(w.app_status_snapshot()["state"]=="STALE"&&w.app_status_snapshot()["recording"].toBool(),"actual UART callback delay enters STALE without ending recorder");
                check(w.app_status_snapshot()["parsed_count"].toInt()==beforeDelay,"STALE adds no fabricated samples");
            }
            app_window_test_access_t::drain(w);
            check(waitFor([&](){return app_window_test_access_t::confirmed(w)&&restore(w)["state"]=="RESTORED"&&w.app_status_snapshot()["parsed_count"].toInt()>beforeDelay;}),"buffered actual frames resume and reconfirm without lost bytes");
            app_window_test_access_t::event(w,"TEST_BUFFERED_RECEIVE_DELAY_END");
        }
        if(argc==5 &&QString::fromLatin1(argv[4])=="--software-reset-test") {
            const auto beforeReset=w.app_status_snapshot();const int bootCount=app_window_test_access_t::boots(w);
            const QString resetTX=QDir(beforeReset["record_directory"].toString()).filePath("command_tx.jsonl");
            const qint64 resetOffset=QFileInfo(resetTX).size();
            app_window_test_access_t::event(w,"TEST_CONTROLLED_MCU_SOFTWARE_RESET");
            QFile readyFile("software_reset_ready.json");check(readyFile.open(QIODevice::WriteOnly|QIODevice::NewOnly),"controlled reset readiness marker");
            readyFile.write(QJsonDocument(beforeReset).toJson());readyFile.close();
            check(waitFor([&](){return app_window_test_access_t::boots(w)>bootCount&&restore(w)["state"]=="RESTORED"&&app_window_test_access_t::confirmed(w);},45000),"actual MCU BOOT followed by confirmed configuration restoration");
            const auto afterReset=w.app_status_snapshot();result["after_controlled_software_reset"]=afterReset;
            check(afterReset["frame_temperature_source"]=="REAL"&&afterReset["frame_humidity_source"]=="SIM"&&afterReset["calibration_effective"].toInt()==3,"actual reset defaults restored to confirmed REAL/SIM+Fit3");
            check(afterReset["record_directory"]==beforeReset["record_directory"]&&afterReset["ui_points"].toInt()>beforeReset["ui_points"].toInt(),"MCU reset retains same recorder and history");
            bool mode=false,status=false,cal=false,identity=false,ordered=true;int writes=0;
            for(const auto &b:commands(resetTX,resetOffset)) {
                if(b=="CMD,MODE?\r\n") mode=true;
                if(b=="CMD,STATUS?\r\n") status=true;
                if(b=="CMD,CAL?\r\n") cal=true;
                if(b=="CMD,ID?\r\n") identity=true;
                if(b.startsWith("CMD,SET,")||b.startsWith("CMD,CAL,")) {++writes;ordered=ordered&&mode&&status&&cal&&identity;}
            }
            check(ordered&&writes==2,"fresh MODE/STATUS/CAL precede the one SET and one CAL after real BOOT");
        }
        // Unapplied edits must never become the automatic restore target.
        auto temperatureEdit=w.findChild<QComboBox*>("temperatureSourceChoice");
        auto fitEdit=w.findChild<QComboBox*>("calibrationChoice");
        temperatureEdit->setFocus();QTest::keyClick(temperatureEdit,Qt::Key_Down);
        fitEdit->setFocus();QTest::keyClick(fitEdit,Qt::Key_Up);
        check(temperatureEdit->currentText()=="SIM"&&fitEdit->currentIndex()==2&&app_window_test_access_t::fit_edited(w),"native keyboard edits are unapplied user choices");
        w.findChild<QCheckBox*>("showHumidity")->setChecked(false);w.app_set_x_window("MANUAL",32);
        app_window_test_access_t::manual_y(w);invoke(w,"slotPauseDisplay");
        const auto before=w.app_status_snapshot();const QString session=before["record_directory"].toString();
        const QString tx=QDir(session).filePath("command_tx.jsonl");const qint64 offset=QFileInfo(tx).size();
        app_window_test_access_t::event(w,"TEST_RESOURCE_ERROR_WITH_OWNED_CONTENTION");app_window_test_access_t::loss(w);
        check(!w.app_status_snapshot()["serial_open"].toBool()&&w.app_status_snapshot()["recording"].toBool(),"injected loss releases only owned main handle");
        QSerialPort owned;owned.setPortName(port);owned.setBaudRate(115200);
        const bool opened=owned.open(QIODevice::ReadWrite);check(opened,"test acquires released actual port exclusively");
        QByteArray aux;
        if(opened) {
            // Explicit test setup of volatile defaults, not an MCU reboot.
            owned.write("CMD,SET,BOTH,REAL,REAL\r\n");owned.waitForBytesWritten(1000);QTest::qWait(1300);aux+=owned.readAll();
            owned.write("CMD,CAL,1\r\n");owned.waitForBytesWritten(1000);QTest::qWait(4100);aux+=owned.readAll();
            result["first_retry_denied"]=w.app_status_snapshot();
            check(w.app_status_snapshot()["connection_state"]=="BUSY_OR_DENIED","automatic retry receives actual Windows PermissionError");
            check(w.app_status_snapshot()["serial_recovery"].toObject()["attempts"].toInt()==1,"one delayed attempt while owned port is busy");
            owned.close();
        }
        QFile auxlog("owned_auxiliary_uart.log");check(auxlog.open(QIODevice::WriteOnly|QIODevice::NewOnly),"preserve explicit test auxiliary raw UART");auxlog.write(aux);auxlog.close();
        const bool restored=waitFor([&](){return restore(w)["state"]=="RESTORED";},25000);check(restored,"automatic recovery restores confirmed source+Fit3");
        const auto recovered=w.app_status_snapshot();result["restored"]=recovered;
        check(recovered["connection_state"].toString().startsWith("CONNECTED"),"sidebar agrees with confirmed recovery banner immediately");
        check(recovered["frame_temperature_source"]=="REAL"&&recovered["frame_humidity_source"]=="SIM"&&recovered["calibration_effective"].toInt()==3,"actual recovered TH3 reflects target, not unapplied edits");
        check(recovered["record_directory"]==before["record_directory"]&&recovered["ui_points"].toInt()>=before["ui_points"].toInt(),"same Session and existing history survive");
        check(recovered["segment_id"].toInt()>before["segment_id"].toInt(),"recovery/source transition adds a segment");
        check(recovered["display_paused"].toBool()&&recovered["x_axis_mode"]=="MANUAL"&&recovered["x_axis_span_s"].toInt()==32&&recovered["temperature_y_manual"].toBool()&&recovered["humidity_y_manual"].toBool(),"pause and X/Y settings survive");
        check(!w.findChild<QCheckBox*>("showHumidity")->isChecked(),"humidity visibility survives");
        check(temperatureEdit->currentText()=="SIM","unapplied source edit survives");
        check(fitEdit->currentIndex()==2,"unapplied fit edit survives");
        result["unapplied_edits_after_restore"]=QJsonObject{{"temperature",temperatureEdit->currentText()},{"profile",fitEdit->currentIndex()},{"fit_edited",app_window_test_access_t::fit_edited(w)}};
        w.grab().save("restored_before_user_reapply.png");
        const auto replay=commands(tx,offset);bool sawMode=false,sawStatus=false,sawCalQuery=false,ordered=true;int sets=0,cals=0;
        for(const auto &b:replay) {
            if(b=="CMD,MODE?\r\n") sawMode=true;
            if(b=="CMD,STATUS?\r\n") sawStatus=true;
            if(b=="CMD,CAL?\r\n") sawCalQuery=true;
            if(b.startsWith("CMD,SET,")) {++sets;ordered=ordered&&sawMode&&sawStatus&&sawCalQuery;}
            if(b=="CMD,CAL,3\r\n") {++cals;ordered=ordered&&sets==1;}
        }
        check(ordered&&sets==1&&cals==1,"queries precede single SET then CAL; no blind repeat");
        // Negative identity gate: change only the saved test target's firmware.
        const qint64 mismatchOffset=QFileInfo(tx).size();app_window_test_access_t::event(w,"TEST_EXPECTED_FIRMWARE_MISMATCH");app_window_test_access_t::loss(w);app_window_test_access_t::mismatch(w);
        check(waitFor([&](){return restore(w)["state"]=="UNSAFE_IDENTITY";},22000),"different expected firmware prevents restore");
        check(onlyQueries(commands(tx,mismatchOffset)),"unsafe expected peer sends queries only, no SET/CAL");
        const qint64 uidMismatchOffset=QFileInfo(tx).size();
        app_window_test_access_t::event(w,"TEST_EXPECTED_MCU_UID_MISMATCH");app_window_test_access_t::loss(w);app_window_test_access_t::mismatch_uid(w);
        check(waitFor([&](){return restore(w)["state"]=="UNSAFE_IDENTITY";},22000),"different saved MCU UID prevents restoration");
        check(onlyQueries(commands(tx,uidMismatchOffset)),"wrong MCU UID target receives read-only queries, no SET/CAL");
        // Reconnection with restore disabled is observational even if enabled recovery.
        w.app_set_recovery_options(true,false,false);const qint64 disabledOffset=QFileInfo(tx).size();
        app_window_test_access_t::event(w,"TEST_RESTORE_OPTION_DISABLED");app_window_test_access_t::loss(w);
        check(waitFor([&](){return restore(w)["state"]=="DISABLED";},22000),"restore disabled is explicit after actual reconnect");
        check(onlyQueries(commands(tx,disabledOffset)),"disabled restore sends no changing commands");
        // Real owned-port contention covers the entire new finite budget.
        // It is a host contention test, not USB removal or MCU power loss.
        app_window_test_access_t::event(w,"TEST_TWELVE_REAL_OWNED_PORT_DENIALS");app_window_test_access_t::loss(w);
        QSerialPort ceilingOwner;ceilingOwner.setPortName(port);ceilingOwner.setBaudRate(115200);
        const bool held=ceilingOwner.open(QIODevice::ReadWrite);check(held,"owned contention for the complete twelve-attempt budget");
        if(held) {
            const auto frozenCount=w.app_status_snapshot()["parsed_count"].toInt();
            check(waitFor([&](){return w.app_status_snapshot()["serial_recovery"].toObject()["state"]=="WAITING_FOR_USER";},68000),"twelve actual PermissionError attempts reach WAITING_FOR_USER");
            const auto ceiling=w.app_status_snapshot();result["real_retry_ceiling"]=ceiling;
            check(ceiling["serial_recovery"].toObject()["attempts"].toInt()==12,"exactly twelve actual owned-port attempts");
            check(ceiling["recording"].toBool()&&ceiling["record_directory"]==session&&ceiling["parsed_count"].toInt()==frozenCount,"no fabricated samples during real sixty-second port denial; same Session");
            ceilingOwner.close();QTest::qWait(5500);
            check(!w.app_status_snapshot()["serial_open"].toBool()&&w.app_status_snapshot()["serial_recovery"].toObject()["attempts"].toInt()==12,"port arrival does not bypass the exhausted manual-only state");
            check(w.app_reconnect_serial(),"explicit selected-port reconnect after exhausted budget");
            check(waitFor([&](){return app_window_test_access_t::confirmed(w)&&restore(w)["state"]=="DISABLED";},22000),"manual recovery rechecks MCU identity and live state in the same Session");
        }
        invoke(w,"slotPauseDisplay");w.findChild<QCheckBox*>("showHumidity")->setChecked(true);
        const int count=w.app_status_snapshot()["parsed_count"].toInt();answerExit(false);w.close();QTest::qWait(1500);
        check(w.app_status_snapshot()["serial_open"].toBool()&&w.app_status_snapshot()["recording"].toBool()&&w.app_status_snapshot()["parsed_count"].toInt()>count,"cancel Exit keeps actual COM and continuing frames");
        // Restore exactly the actual pre-test volatile configuration.
        check(waitFor([&](){return w.app_request_calibration(originalFit);}),"restore pretest fit request");
        check(waitFor([&](){return app_window_test_access_t::confirmed(w)&&w.app_status_snapshot()["calibration_effective"].toInt()==originalFit;}),"pretest fit confirmed");
        check(waitFor([&](){return w.app_request_sources(originalT,originalH);}),"restore pretest sources request");
        check(waitFor([&](){auto s=w.app_status_snapshot();return app_window_test_access_t::confirmed(w)&&s["frame_temperature_source"]==originalT&&s["frame_humidity_source"]==originalH;}),"pretest sources confirmed");
        result["pretest_configuration_restored"]=w.app_status_snapshot();w.grab().save("actual_recovery.png");
        answerExit(true);w.close();
    }
    if(w.app_status_snapshot()["running"].toBool()) invoke(w,"slotStop");
    const auto ended=w.app_status_snapshot();result["final"]=ended;
    check(!ended["serial_open"].toBool()&&!ended["recording"].toBool(),"normal End releases actual COM/files");
    check(guard.open(QIODevice::ReadWrite),"independent exclusive open proves COM released");guard.close();
    result["checks"]=checks;result["failures"]=failures;result["pass"]=failures.isEmpty();
    QTextStream output(stdout);output.setCodec("UTF-8");output<<QJsonDocument(result).toJson();return failures.isEmpty()?0:1;
}
