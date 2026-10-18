#include "mainwindow.h"
#include "app_temperature_evidence.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QTest>
#include <QTextStream>
#include <QDir>
#include <QElapsedTimer>
#include <cmath>
#include <functional>

static QJsonArray checks,failures;
static void check(bool ok,const QString &name){checks.append(name);if(!ok)failures.append(name);}
static QByteArray frame(quint32 t,int id,double temp) {
    return QString("TH3,MIXED,%1,1,71,0,%2,16,%3,50.00,1.400,NA,NA,NA,11200,NA,NA,NA,REAL,SIM,1,1,1\r\n").arg(t).arg(id).arg(temp,0,'f',2).toLatin1();
}
static QByteArray evidence(quint32 t,int id,int profile,double corrected) {
    return QString("[TEMP_CORR] raw=26.125C corrected=%1C offset=%2C raw_bits=41D10000 cal=%3 state=%4 t_ms=%5 profile=%6\r\n")
        .arg(corrected,0,'f',3).arg(corrected-26.125,0,'f',3).arg(id).arg(id?"PROVISIONAL":"UNCORRECTED").arg(t).arg(profile).toLatin1();
}
static bool waitFor(const std::function<bool()> &ready,int limit=8000){QElapsedTimer timer;timer.start();while(timer.elapsed()<limit){QCoreApplication::processEvents();if(ready())return true;QTest::qWait(20);}return false;}
static void invoke(MainWindow &w,const char *name){check(QMetaObject::invokeMethod(&w,name,Qt::DirectConnection),QString("invoke ")+name);}

int main(int argc,char **argv){
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc,argv);QCoreApplication::setApplicationName("TempHumi");
    QCoreApplication::setApplicationVersion(qEnvironmentVariable("TEMPHUMI_TEST_DESKTOP_VERSION","UNSPECIFIED_TEST_VERSION"));
    app.setProperty("appSettingsFile",QDir::current().filePath("preferences.ini"));QJsonObject result;
    app_temperature_evidence_t matcher;auto f=app_th3_parse_line(frame(1000,1,27.03));
    matcher.observe(evidence(1000,1,1,27.025));matcher.attach(f);
    check(f.physical_temperature_c.present&&f.physical_temperature_c.value==26.125&&f.correction_profile==1,"exact float32 matched once");
    auto reused=app_th3_parse_line(frame(1000,1,27.03));matcher.attach(reused);check(!reused.physical_temperature_c.present,"evidence cannot be reused");
    matcher.observe(evidence(1000,1,1,27.025));auto stale=app_th3_parse_line(frame(2000,1,27.03));matcher.attach(stale);check(!stale.physical_temperature_c.present,"timestamp mismatch stays missing");
    matcher.observe(evidence(1000,1,1,27.025));auto wrong=app_th3_parse_line(frame(1000,2,27.03));matcher.attach(wrong);check(!wrong.physical_temperature_c.present,"profile mismatch stays missing");
    matcher.observe(evidence(1000,1,1,28.));auto disagree=app_th3_parse_line(frame(1000,1,27.03));matcher.attach(disagree);check(!disagree.physical_temperature_c.present,"numeric disagreement stays missing");
    matcher.observe("[TEMP_CORR] malformed\n");auto bad=app_th3_parse_line(frame(1000,1,27.03));matcher.attach(bad);check(!bad.physical_temperature_c.present,"malformed evidence stays missing");
    const bool hardware=argc>1;MainWindow window;window.resize(1100,850);window.show();QTest::qWait(100);
    auto plot=window.findChild<QCustomPlot*>("customPlot");check(plot&&plot->graphCount()==3,"three separate data curves");
    check(plot->graph(0)->valueAxis()==plot->yAxis&&plot->graph(2)->valueAxis()==plot->yAxis,"both temperatures share left axis");
    check(plot->graph(1)->valueAxis()==plot->yAxis2,"humidity keeps right axis");
    check(plot->graph(0)->pen().color()==Qt::red&&plot->graph(2)->pen().color()==QColor(230,126,34),"raw red and corrected orange");
    QCheckBox *raw=window.findChild<QCheckBox*>("showRawTemperature"),*corrected=window.findChild<QCheckBox*>("showCorrectedTemperature"),*humidity=window.findChild<QCheckBox*>("showHumidity");
    check(raw&&corrected&&humidity,"three visibility controls exist");
    if(!hardware){
        QFile stream("profiles_stream.txt");check(stream.open(QIODevice::WriteOnly|QIODevice::Truncate),"fixture stream open");stream.close();
        window.app_configure_input(QFileInfo(stream).absoluteFilePath(),"FILE_STREAM",QDir::current().filePath("sessions"));invoke(window,"slotDraw");
        auto append=[&](quint32 t,int id,int profile,double value){QFile file(stream.fileName());file.open(QIODevice::WriteOnly|QIODevice::Append);file.write(evidence(t,id,profile,value)+frame(t,id,value));file.close();invoke(window,"slotReadFileData");};
        append(1000,1,1,27.025);auto s=window.app_status_snapshot();
        result["first_frame"]=s;
        check(s["temperature_raw_c"].toDouble()==26.125&&std::abs(s["temperature_corrected_c"].toDouble()-27.025)<.00551,"GUI raw and corrected values are independently retained");
        raw->setChecked(false);check(!plot->graph(0)->visible()&&plot->graph(2)->visible(),"hide raw independently");raw->setChecked(true);
        humidity->setChecked(false);check(!plot->graph(1)->visible()&&plot->graph(0)->visible(),"hide humidity independently");humidity->setChecked(true);
        corrected->setChecked(false);check(!plot->graph(2)->visible(),"hide corrected independently");corrected->setChecked(true);
        append(2000,0,0,26.125);check(!corrected->isEnabled()&&!corrected->isChecked()&&!plot->graph(2)->visible(),"off disables and unchecks corrected visibility");
        append(3000,2,2,27.05);append(4000,2,2,27.05);check(corrected->isEnabled()&&corrected->isChecked()&&plot->graph(2)->visible(),"active correction restores visibility preference");
        // Profile2 is configured but inactive outside support; output stays the
        // MCU baseline and the orange series remains selectable/observable.
        { QFile file(stream.fileName());file.open(QIODevice::WriteOnly|QIODevice::Append);
          file.write(QByteArray("[TEMP_CORR] raw=24.875C corrected=24.875C offset=0.000C raw_bits=41C70000 cal=0 state=UNCORRECTED t_ms=5000 profile=2\r\n")+frame(5000,0,24.875));file.close();invoke(window,"slotReadFileData"); }
        auto inactive=window.app_status_snapshot();
        check(!inactive["calibration_applied"].toBool()&&!inactive["temperature_corrected_c"].isNull()&&corrected->isEnabled(),"inactive fit preserves baseline output and visibility");
        check(inactive["plot_export_status"].toString().contains("Baseline (fit inactive)"),"inactive support is explicitly labeled");
        check(!window.app_request_calibration(2),"file/replay cannot command MCU");
        window.grab().save("profiles_three_curves.png");invoke(window,"slotStop");result["final"]=window.app_status_snapshot();
    }else{
        window.app_configure_serial(QString::fromLocal8Bit(argv[1]),115200,QDir::current().filePath("sessions"));invoke(window,"slotDraw");
        check(waitFor([&](){return window.app_status_snapshot()["mode_confirmed"].toBool();},15000),"physical MODE/TH3 ready");
        check(waitFor([&](){auto s=window.app_status_snapshot();return s["calibration_confirmed"].toBool()&&s["firmware_current_connection_verified"].toBool();},15000),"fresh CAL/STATUS ready");
        const auto original=window.app_status_snapshot();result["initial"]=original;
        check(original["observed_firmware"]==qEnvironmentVariable("TEMPHUMI_TEST_EXPECTED_FIRMWARE"),"actual MCU version equals independent expected version");
        check(window.app_request_sources("REAL","SIM"),"request explicit REAL/SIM");
        check(waitFor([&](){auto s=window.app_status_snapshot();return s["mode_confirmed"].toBool()&&s["frame_humidity_source"]=="SIM";}),"REAL/SIM effective");
        QJsonArray rounds;
        for(int round=0;round<3;++round)for(int profile:{0,1,2,3}){
            check(waitFor([&](){return window.app_request_calibration(profile);}),"fit request accepted by Qt");
            check(waitFor([&](){auto s=window.app_status_snapshot();return s["calibration_confirmed"].toBool()&&s["calibration_frame_profile"].toInt()==profile;}),"ACK/readback/MCU frame fit agreement");
            QTest::qWait(1100);auto s=window.app_status_snapshot();check(!s["temperature_raw_c"].isNull(),"physical raw window mean available");
            check(corrected->isEnabled()==(profile!=0),"physical off visibility enable state");rounds.append(s);
        }
        result["rounds"]=rounds;
        check(waitFor([&](){return window.app_request_calibration(2);}),"select LAD stability profile");
        check(waitFor([&](){return window.app_status_snapshot()["calibration_confirmed"].toBool();}),"LAD stability confirmed");
        window.grab().save("profiles_hardware_three_curves.png");
        const auto before=window.app_status_snapshot()["parsed_count"].toInt();QElapsedTimer stable;stable.start();
        const int duration=argc>2?QString::fromLocal8Bit(argv[2]).toInt():310;
        while(stable.elapsed()<duration*1000){QCoreApplication::processEvents();QTest::qWait(100);}
        auto s=window.app_status_snapshot();check(s["parsed_count"].toInt()-before>=int(duration*.85),"bounded actual profile stability");
        result["stability_duration_seconds"]=duration;
        check(s["malformed_count"].toInt()==0&&s["error_flags"].toInt()==0,"continuous TH3 and valid temperature status");result["stability_end"]=s;
        invoke(window,"slotPauseDisplay");auto frozen=plot->graph(0)->data()->size();QTest::qWait(2500);check(frozen==plot->graph(0)->data()->size(),"three-curve pause freezes plot only");invoke(window,"slotPauseDisplay");
        const int originalProfile=original["calibration_effective"].toInt();
        check(waitFor([&](){return window.app_request_calibration(originalProfile);}),"restore actual pretest fit");
        check(waitFor([&](){auto s=window.app_status_snapshot();return s["calibration_confirmed"].toBool()&&s["calibration_frame_profile"].toInt()==originalProfile;}),"pretest fit restored");
        const auto originalT=original["frame_temperature_source"].toString(),originalH=original["frame_humidity_source"].toString();
        check(waitFor([&](){return window.app_request_sources(originalT,originalH);}),"restore actual pretest sources");
        check(waitFor([&](){auto s=window.app_status_snapshot();return s["mode_confirmed"].toBool()&&s["frame_temperature_source"]==originalT&&s["frame_humidity_source"]==originalH;}),"pretest sources restored");
        invoke(window,"slotStop");result["final"]=window.app_status_snapshot();check(!result["final"].toObject()["serial_open"].toBool(),"End Session releases COM");
    }
    result["checks"]=checks;result["failures"]=failures;result["pass"]=failures.isEmpty();result["device_pixel_ratio"]=window.devicePixelRatioF();
    QTextStream out(stdout);out.setCodec("UTF-8");out<<QJsonDocument(result).toJson();return failures.isEmpty()?0:1;
}
