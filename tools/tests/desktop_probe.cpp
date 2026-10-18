/* Production-object UI tests. All measurement bytes here are labelled fixtures. */
#include <QApplication>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QImage>
#include <QScreen>
#include <QWheelEvent>
#include <QTextStream>
#include <QTimer>
#include <cmath>
#include <algorithm>
#include <QFileInfo>
#include "mainwindow.h"
#include "app_desktop_settings.h"
#include "app_session_export.h"
static int count=0; static QJsonArray failures;
static void check(bool value,const QString &name) { ++count; if(!value) failures.append(name); }
static QByteArray fileBytes(const QString &path) { QFile f(path); if(!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
static void write(const QString &path,const QByteArray &bytes,bool append=false) { QFile f(path); if(!f.open(QIODevice::WriteOnly|(append?QIODevice::Append:QIODevice::Truncate)) ||f.write(bytes)!=bytes.size()) qFatal("fixture write"); }
static QByteArray sim(quint32 t,double T=29,double H=60) { return QString("TH3,SIM,%1,1,3,0,0,1,%2,%3,NA,NA,NA,NA,NA,NA,NA,NA,SIM,SIM,1,1,0\r\n").arg(t).arg(T,0,'f',2).arg(H,0,'f',2).toLatin1(); }
static void invoke(MainWindow &w,const char *slot) { if(!QMetaObject::invokeMethod(&w,slot,Qt::DirectConnection)) qFatal("slot"); }
static void poll(MainWindow &w) { invoke(w,"slotReadFileData"); QApplication::processEvents(); }
static bool close_value(double a,double b,double tolerance=1e-6) { return std::abs(a-b)<tolerance; }
int main(int argc,char **argv)
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling); QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc,argv); app.setApplicationName("TempHumi Monitor");
    if(argc<3) return 2;
    app.setApplicationVersion(QString::fromUtf8(fileBytes(QString::fromLocal8Bit(argv[1]))).trimmed());
    const QString mode=QString::fromLocal8Bit(argv[2]); app.setProperty("appSettingsFile",QDir::current().filePath("preferences.ini"));
    if(mode=="axis") {
        const int steps[]={5,10,20,30,60,120,300,600,1200};
        for(int i=0;i<8;++i) for(double delta:{-.001,0.,.001}) {
            app_plot_window_t model; const double t=steps[i]*2./3.+delta; const auto r=model.app_range_at(t);
            check(r.span==(delta<0?steps[i]:steps[i+1]),QString("AUTO threshold %1 delta%2").arg(steps[i]).arg(delta));
        }
        const double times[]={0,3,4,5,7,10,20,32,60,120,300,600,1080,1200,1300,2400};
        const int expected[]={5,5,10,10,20,20,60,60,120,300,600,1200,1200,1200,1200,1200};
        for(int i=0;i<16;++i) { app_plot_window_t model; const auto r=model.app_range_at(times[i]); check(r.span==expected[i],"AUTO jump "+QString::number(times[i])); }
        app_plot_window_t model; auto r=model.app_range_at(1300); check(close_value(r.lower,220)&&close_value(r.upper,1420),"AUTO 1300 right margin");
        r=model.app_range_at(1080); check(close_value(r.lower,0)&&close_value(r.upper,1200),"AUTO 1080");
        model.app_reset_segment(); check(model.app_range_at(0).span==5,"new segment AUTO reset");
        for(int span:{5,17,32,47,180,950,1200}) {
            check(model.app_set_mode(true,span),"manual accepts integer "+QString::number(span));
            for(double delta:{-.001,0.,.001}) { r=model.app_range_at(span*.9+delta); check(close_value(r.upper-r.lower,span) &&close_value(r.lower,std::max(0.,delta)),"manual threshold90"); }
            r=model.app_range_at(2400); check(close_value(r.lower,2400-span*.9)&&close_value(r.upper-r.lower,span),"manual long scroll");
        }
        check(!model.app_set_mode(true,4)&&!model.app_set_mode(true,1201),"manual rejects invalid range");
    } else if(mode=="preferences") {
        app_serial_device_t bluetooth; bluetooth.port="COM11";
        app_serial_device_t usb; usb.port="COM13"; usb.vendor=0x1A86; usb.product=0x7523; usb.has_vendor=usb.has_product=true; usb.serial_number="device-A";
        check(app_desktop_choose_serial({bluetooth,usb},QString(),QString()).port=="COM13","CH340 after Bluetooth");
        check(app_desktop_choose_serial({usb,bluetooth},QString(),QString()).port=="COM13","CH340 independent of enumeration order");
        auto other=usb; other.port="COM19"; other.serial_number="device-B";
        check(app_desktop_choose_serial({usb,other},QString(),QString()).needs_selection,"multiple CH340 asks selection");
        check(app_desktop_choose_serial({other,usb},"COM19",QString()).port=="COM19","restore chosen device");
        check(app_desktop_choose_serial({usb,other},"COM19","device-A").port=="COM13","USB identity before COM number");
        check(app_desktop_choose_serial({bluetooth},QString(),QString()).port.isEmpty(),"no CH340 does not select Bluetooth");
        check(app_desktop_choose_serial({},"COM13",QString()).port.isEmpty(),"offline preferred not invented");
        MainWindow w; w.show(); QApplication::processEvents();
        auto s=w.app_status_snapshot(); check(s["input_mode"]=="SERIAL"&&!s["serial_open"].toBool(),"normal GUI prepares serial only");
        check(s["command_tx_count"].toInt()==0,"no startup SET or COM open");
        check(w.size().width()<=app.primaryScreen()->availableGeometry().width(),"default within work area width");
        check(w.size().height()<=app.primaryScreen()->availableGeometry().height(),"default within work area height");
        w.grab().save("startup.png");
        w.app_configure_serial("COM13",115200,QDir::current().filePath("recording root"));
        SettingsDialog dialog(&w,"COM13",115200); dialog.show(); QApplication::processEvents();
        const auto before=w.app_status_snapshot(); dialog.reject(); check(w.app_status_snapshot()["selected_port"]==before["selected_port"],"cancel leaves configured port unchanged");
        dialog.show(); QApplication::processEvents(); dialog.grab().save("settings.png");
        check(dialog.width()<650&&dialog.height()<500,"compact settings dialog");
        dialog.reject();
        auto choice=dialog.findChild<QComboBox*>("cbSerialPort");
        check(choice &&choice->currentText()=="COM13","settings restores configured COM13 actual inventory");
        QTimer::singleShot(0,[&]() {
            for(auto widget:QApplication::topLevelWidgets()) if(auto active=qobject_cast<SettingsDialog*>(widget)) {
                active->findChild<QComboBox*>("cbSerialPort")->setCurrentText("COM13");
                active->findChild<QComboBox*>("cbBaudRate")->setCurrentText("19200");
                QMetaObject::invokeMethod(active,"onApply",Qt::DirectConnection);
            }
        });
        invoke(w,"slotConfig");
        check(w.app_status_snapshot()["selected_port"]=="COM13"&&w.findChild<QLabel*>("lbl_inputPath")->text().contains("19200"),"accepted settings update visible port/baud immediately");
        check(app_desktop_open_settings()->value("serial/port")=="COM13"&&app_desktop_open_settings()->value("serial/baud").toInt()==19200,"accepted selection persists without opening COM");
        const auto settingsBeforeCancel=fileBytes("preferences.ini");
        QTimer::singleShot(0,[&]() {
            for(auto widget:QApplication::topLevelWidgets()) if(auto active=qobject_cast<SettingsDialog*>(widget)) {
                active->findChild<QComboBox*>("cbBaudRate")->setCurrentText("9600"); active->reject();
            }
        });
        invoke(w,"slotConfig");
        check(fileBytes("preferences.ini")==settingsBeforeCancel&&w.findChild<QLabel*>("lbl_inputPath")->text().contains("19200"),"actual modal Cancel preserves configuration and settings bytes");
        w.app_set_recording_root(QDir::current().filePath("记录 空格"));
        MainWindow restarted; check(restarted.app_status_snapshot()["recording_root"]==w.app_status_snapshot()["recording_root"],"record root remembered");
        w.app_configure_serial("COM9999",115200,QDir::current().filePath("not present")); invoke(w,"slotDraw");
        check(w.app_status_snapshot()["state"]=="ERROR"&&!w.app_status_snapshot()["serial_open"].toBool(),"nonexistent COM explicit error");
        check(w.app_status_snapshot()["command_tx_count"].toInt()==0,"missing COM no commands");
        write("folder_is_a_file","blocked"); w.app_configure_input("input.txt","FILE_STREAM",QDir::current().filePath("folder_is_a_file")); invoke(w,"slotDraw");
        check(w.app_status_snapshot()["state"]=="ERROR"&&!w.app_status_snapshot()["recording"].toBool(),"invalid recording location no silent fallback");
    } else if(mode=="data_export") {
        MainWindow w; w.show(); write("fixture.txt",sim(0,20)+sim(1000,21)+sim(2000,22)+sim(3000,23));
        w.app_configure_input(QDir::current().filePath("fixture.txt"),"REPLAY",QDir::current().filePath("canonical")); invoke(w,"slotDraw"); poll(w);
        check(w.app_set_x_window("MANUAL",17),"manual GUI arbitrary integer");
        check(!w.app_set_x_window("MANUAL",4)&&!w.app_set_x_window("BOGUS",17),"invalid UI span rejected");
        w.app_set_x_window("AUTO",5); check(w.app_status_snapshot()["x_axis_span_s"].toInt()==5,"AUTO initial interval");
        auto result=w.app_analyze_selection(APP_SIGNAL_TEMPERATURE,0,3); check(result.selection_valid,"analysis cursor same segment");
        QDir().mkdir("中文 导出"); const QString json=QDir::current().filePath("中文 导出/analysis.json"),png=QDir::current().filePath("中文 导出/plot.png");
        check(w.app_save_analysis(json),"JSON Chinese/space directory"); check(w.app_export_plot(png),"PNG Chinese/space directory");
        check(!QImage(png).isNull(),"PNG decodes"); const auto saved=QJsonDocument::fromJson(fileBytes(json)).object();
        check(saved==w.app_status_snapshot()["selection"].toObject(),"JSON matches UI selection");
        check(w.app_status_snapshot()["last_export_directory"]==QFileInfo(png).absolutePath(),"export directory remembered");
        const auto source=w.app_status_snapshot()["latest_session"].toString(); QString exported;
        check(!w.app_export_session(QDir::currentPath(),&exported),"active session export refused");
        check(!w.app_save_analysis(source+"/metadata.json")&&!w.app_export_plot(source+"/command_tx.jsonl"),"authoritative evidence protected");
        invoke(w,"slotStop"); const auto originalMetadata=fileBytes(source+"/metadata.json");
        write(source+"/command_tx.jsonl","{\"fixture\":true,\"bytes_hex\":\"434D442C4D4F44453F0D0A\"}\r\n");
        check(w.app_export_session(QDir::current().filePath("中文 导出"),&exported),"complete session export");
        for(const auto &name:{QString("raw_input.log"),QString("parsed.csv"),QString("metadata.json"),QString("command_tx.jsonl")}) check(fileBytes(source+"/"+name)==fileBytes(exported+"/"+name),"export exact bytes "+name);
        check(fileBytes(source+"/metadata.json")==originalMetadata,"canonical metadata unchanged");
        QString again; check(w.app_export_session(QDir::current().filePath("中文 导出"),&again)&&again!=exported,"second export no overwrite");
        MainWindow restart; check(restart.app_status_snapshot()["latest_session"]==source,"latest real session restored");
        check(restart.app_status_snapshot()["last_export_directory"]==QDir::current().filePath("中文 导出"),"export folder restored");
        check(!w.app_export_session("missing folder"),"failed session export explicit");
        check(fileBytes(source+"/metadata.json")==originalMetadata,"failed export original retained");
        write("fixture2.txt","TH3,REAL,4000,0,205,48,0,16,26.50,NA,1.400,1.200,NA,NA,11200,9600,NA,NA,REAL,REAL,1,0,0\r\n");
        w.app_configure_input(QDir::current().filePath("fixture2.txt"),"REPLAY",QDir::current().filePath("canonical")); invoke(w,"slotDraw"); poll(w);
        auto s=w.app_status_snapshot(); check(s["state"]=="PARTIAL_VALID"&&s["temperature_valid"].toBool()&&!s["humidity_valid"].toBool(),"TH2 global0 retains T flag observation");
        auto plot=w.findChild<QCustomPlot*>("customPlot"); int finiteHumidity=0; for(auto it=plot->graph(1)->data()->constBegin();it!=plot->graph(1)->data()->constEnd();++it) if(std::isfinite(it->value)) ++finiteHumidity;
        check(finiteHumidity==0,"no old SIM blue curve after source/segment change");
        check(plot->graph(0)->valueAxis()==plot->yAxis&&plot->graph(1)->valueAxis()==plot->yAxis2,"temperature left humidity right");
        check(plot->axisRect()->rangeZoom()==Qt::Vertical,"wheel cannot zoom X");
        invoke(w,"slotStop");
    } else if(mode=="buffer") {
        MainWindow w; w.show(); QApplication::processEvents(); QByteArray bytes; for(int i=0;i<=13000;++i) bytes+=sim(quint32(i*100),29+std::sin(i*.01),60+std::cos(i*.01)); write("large.txt",bytes);
        w.app_configure_input(QDir::current().filePath("large.txt"),"FILE_STREAM",QDir::current().filePath("canonical")); invoke(w,"slotDraw");
        for(int i=0;i<1000 &&w.app_status_snapshot()["input_offset_bytes"].toDouble()<bytes.size();++i) poll(w);
        auto s=w.app_status_snapshot(); check(s["parsed_count"].toInt()==13001,"fast deterministic input all frames recorded");
        check(s["ui_points"].toInt()>=12000&&s["ui_points"].toInt()<=24000,"10Hz full1200s bounded");
        check(s["display_span_s"].toDouble()>=1199.9,"retained 1200 seconds");
        check(close_value(s["plot_x_lower_s"].toDouble(),220)&&close_value(s["plot_x_upper_s"].toDouble(),1420),"actual GUI AUTO at1300");
        w.app_set_x_window("MANUAL",47); s=w.app_status_snapshot(); check(close_value(s["plot_x_upper_s"].toDouble()-s["plot_x_lower_s"].toDouble(),47),"actual GUI manual47");
        w.app_set_x_window("AUTO",5); check(w.app_status_snapshot()["x_axis_span_s"].toInt()==1200,"return AUTO based on current source time");
        auto analysis=w.app_analyze_selection(APP_SIGNAL_TEMPERATURE,1200,1250); check(analysis.selection_valid&&analysis.sample_count==501,"cursor within retained 1200s");
        auto plot=w.findChild<QCustomPlot*>("customPlot"); plot->replot(); QWheelEvent event(QPointF(plot->axisRect()->rect().left()+5,plot->axisRect()->rect().center().y()),QPointF(0,0),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(plot,&event); const auto yr=plot->yAxis->range();
        write("large.txt",sim(1300100,100),true); poll(w); check(close_value(plot->yAxis->range().lower,yr.lower)&&close_value(plot->yAxis->range().upper,yr.upper),"manual Y retained during collection");
        w.app_reset_y_scale(); check(!w.app_status_snapshot()["temperature_y_manual"].toBool(),"Reset Y releases manual scale");
        w.resize(850,560); QApplication::processEvents(); w.grab().save("buffer_850.png"); w.resize(1100,720); QApplication::processEvents(); w.grab().save("buffer_1100.png");
        invoke(w,"slotStop");
        const auto meta=w.app_status_snapshot()["record_summary"].toObject(); check(meta["parsed_total_count"].toInt()==13002,"raw/CSV unaffected by display trimming");
    } else return 2;
    QJsonObject out; out["checks"]=count; out["failed"]=failures; out["pass"]=failures.isEmpty(); out["classification"]="OFFSCREEN SOFTWARE FIXTURE / NOT HARDWARE";
    QTextStream(stdout)<<QJsonDocument(out).toJson(QJsonDocument::Compact)<<"\n"; return failures.isEmpty()?0:1;
}
