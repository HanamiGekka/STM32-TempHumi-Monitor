/* Qt keyboard/mouse acceptance on production objects. Inputs are software fixtures. */
#include <QApplication>
#include <QtTest/QtTest>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTextStream>
#include <QProcess>
#include <QElapsedTimer>
#include "app_desktop_settings.h"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "mainwindow.h"
static QJsonArray failures;
static int checks=0;
static void check(bool ok,const QString &name) { ++checks; if(!ok) failures.append(name); }
static void invoke(MainWindow &w,const char *slot) { if(!QMetaObject::invokeMethod(&w,slot,Qt::DirectConnection)) qFatal("slot"); }
static void write(const QByteArray &bytes) { QFile f("input.txt"); if(!f.open(QIODevice::WriteOnly|QIODevice::Append)||f.write(bytes)!=bytes.size()) qFatal("fixture"); }
static QByteArray frame(int t,int generation=1) { return QString("TH3,SIM,%1,1,3,0,0,1,29.00,60.00,NA,NA,NA,NA,NA,NA,NA,NA,SIM,SIM,1,1,%2\r\n").arg(t).arg(generation).toLatin1(); }
static QByteArray read(const QString &path) { QFile f(path); if(!f.open(QIODevice::ReadOnly)) qFatal("read"); return f.readAll(); }
int main(int argc,char **argv) {
 QApplication::setAttribute(Qt::AA_EnableHighDpiScaling); QApplication app(argc,argv);
 app.setApplicationName("TempHumi Monitor"); app.setApplicationVersion("v010.14.1");
 app.setProperty("appSettingsFile",QDir::current().filePath("settings.ini"));
 const bool baseline=argc>1 && QString::fromLocal8Bit(argv[1])=="baseline";
 MainWindow w; w.show(); QApplication::processEvents();
 w.app_configure_input(QDir::current().filePath("input.txt"),"FILE_STREAM",QDir::current().filePath("sessions"));
 write(frame(0)); invoke(w,"slotDraw"); invoke(w,"slotReadFileData");
 auto mode=w.findChild<QComboBox*>("axisMode"); auto edit=w.findChild<QLineEdit*>("axisSeconds");
 mode->setCurrentText("MANUAL"); QTest::mouseClick(edit,Qt::LeftButton); edit->selectAll(); QTest::keyClicks(edit,"17");
 write(frame(1000)); invoke(w,"slotReadFileData");
 QJsonObject evidence; evidence["entered"]="17"; evidence["after_refresh"]=edit->text(); evidence["applied_mode"]=w.app_status_snapshot()["x_axis_mode"];
 if(baseline) check(edit->text()!="17" && w.app_status_snapshot()["x_axis_mode"]=="AUTO","reproduced MANUAL overwrite");
 else check(edit->text()=="17" && w.app_status_snapshot()["x_axis_mode"]=="MANUAL","editing survives refresh");
 if(!baseline) {
  auto apply=w.findChild<QPushButton*>("applyAxisSpan");
  for(const QString &value:{QString("17"),QString("47"),QString("300"),QString("1200")}) {
   QTest::mouseClick(edit,Qt::LeftButton); edit->selectAll(); QTest::keyClick(edit,Qt::Key_Backspace);
   for(auto c:value) { QTest::keyClicks(edit,QString(c)); write(frame(2000+checks*1000)); invoke(w,"slotReadFileData"); }
   check(edit->text()==value,"each keyboard edit preserved "+value); QTest::mouseClick(apply,Qt::LeftButton);
   auto s=w.app_status_snapshot(); check(s["x_axis_span_s"].toInt()==value.toInt(),"applied "+value);
   check(qAbs(s["plot_x_upper_s"].toDouble()-s["plot_x_lower_s"].toDouble()-value.toInt())<1e-6,"plotted width "+value);
  }
  for(const QString &value:{QString("4"),QString("1201"),QString(),QString("abc")}) {
   edit->selectAll(); QTest::keyClick(edit,Qt::Key_Backspace); QTest::keyClicks(edit,value); QTest::mouseClick(apply,Qt::LeftButton);
   check(w.app_status_snapshot()["x_axis_span_s"].toInt()==1200,"reject invalid input "+value);
   write(frame(30000+checks*1000)); invoke(w,"slotReadFileData"); check(edit->text()==value,"invalid editing text retained "+value);
  }
  edit->selectAll(); QTest::keyClicks(edit,"47"); QTest::keyClick(edit,Qt::Key_Return);
  check(w.app_status_snapshot()["x_axis_span_s"].toInt()==47,"Enter submits width");
  auto plot=w.findChild<QCustomPlot*>("customPlot"); plot->replot();
  for(const QSize &size:{QSize(850,560),QSize(1100,720)}) {
   w.resize(size); QApplication::processEvents(); plot->replot(); const QRect r=plot->axisRect()->rect();
   for(int channel=0;channel<2;++channel) for(bool margin:{false,true}) {
    const QPoint p(channel?(margin?r.right()+15:r.center().x()+r.width()/4):(margin?r.left()-15:r.left()+r.width()/4),r.center().y());
    auto axis=channel?plot->yAxis2:plot->yAxis; auto other=channel?plot->yAxis:plot->yAxis2;
    const auto before=axis->range(),otherBefore=other->range(),xBefore=plot->xAxis->range();
    QWheelEvent wheel(p,plot->mapToGlobal(p),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(plot,&wheel);
    check(axis->range().size()<before.size() && other->range()==otherBefore &&plot->xAxis->range()==xBefore,"independent zoom channel "+QString::number(channel));
    const auto zoomed=axis->range(); QTest::mousePress(plot,Qt::LeftButton,Qt::NoModifier,p);
    QMouseEvent move(QEvent::MouseMove,p+QPoint(15,30),Qt::NoButton,Qt::LeftButton,Qt::NoModifier); QApplication::sendEvent(plot,&move);
    QTest::mouseRelease(plot,Qt::LeftButton,Qt::NoModifier,p+QPoint(15,30));
    check(axis->range().lower!=zoomed.lower &&qAbs(axis->range().size()-zoomed.size())<1e-6 &&other->range()==otherBefore &&plot->xAxis->range()==xBefore,"independent vertical drag channel "+QString::number(channel));
    const auto moved=axis->range(); write(frame(60000+checks*1000)); invoke(w,"slotReadFileData");
    check(axis->range()==moved,"manual Y survives refresh");
   }
   w.grab().save(QString("layout_%1.png").arg(size.width()));
  }
  w.app_reset_y_scale(); check(!w.app_status_snapshot()["temperature_y_manual"].toBool()&&!w.app_status_snapshot()["humidity_y_manual"].toBool(),"Reset Y both axes");
  mode->setCurrentText("AUTO"); check(w.app_status_snapshot()["x_axis_span_s"].toInt()>5,"AUTO recalculates current source elapsed");
 }
 if(!baseline && argc>1 &&QString::fromLocal8Bit(argv[1])!="plot") {
  auto plot=w.findChild<QCustomPlot*>("customPlot"); auto pause=w.findChild<QPushButton*>("btn_stop");
  w.app_set_x_window("MANUAL",47);
  for(int duration:{5,30,5}) {
   const auto before=w.app_status_snapshot(); const auto n=plot->graph(0)->data()->size(); const auto x=plot->xAxis->range();
   QTest::mouseClick(pause,Qt::LeftButton); check(w.app_status_snapshot()["display_paused"].toBool(),"pause button freezes");
   const auto source=before["last_source_timestamp_ms"].toInt();
   for(int i=1;i<=duration*10;++i) { write(frame(source+i*100)); invoke(w,"slotReadFileData"); }
   check(plot->graph(0)->data()->size()==n &&plot->xAxis->range()==x,"frozen curves and X");
   check(w.app_status_snapshot()["parsed_count"].toInt()==before["parsed_count"].toInt()+duration*10 &&w.app_status_snapshot()["recording"].toBool(),"recording continues during pause "+QString::number(duration));
   check(w.app_status_snapshot()["source_label"].toString().contains("DISPLAY PAUSED / RECORDING ON"),"pause classified");
   edit->selectAll(); QTest::keyClicks(edit,"17"); QTest::keyClick(edit,Qt::Key_Return);
   check(edit->text()=="17"&&plot->xAxis->range()==x,"manual edit while paused leaves frozen axis");
   QTest::mouseClick(pause,Qt::LeftButton); check(!w.app_status_snapshot()["display_paused"].toBool(),"resume");
   check(qAbs(plot->xAxis->range().size()-17)<1e-6 &&plot->graph(0)->data()->size()>n,"resume applies manual and recovered samples");
  }
  check(w.app_export_plot(QDir::current().filePath("markers.png")),"export visible pause/resume PNG");
  const auto frozen=plot->xAxis->range(); const auto n=plot->graph(0)->data()->size();
  QTest::mouseClick(pause,Qt::LeftButton); for(int i=0;i<11;++i) {write(frame(300000+i*100,2));invoke(w,"slotReadFileData");}
  check(plot->xAxis->range()==frozen&&plot->graph(0)->data()->size()==n,"generation changes while display remains frozen");
  QTest::mouseClick(pause,Qt::LeftButton); check(plot->graph(0)->data()->size()==11,"new generation resumes its own segment without bridging");
  QTest::mouseClick(pause,Qt::LeftButton); const auto finalPlot=plot->graph(0)->data()->size();
  invoke(w,"slotStop"); auto s=w.app_status_snapshot();
  check(!s["running"].toBool()&&!s["recording"].toBool()&&plot->graph(0)->data()->size()==finalPlot,"End Session while paused retains history");
  const auto folder=s["record_directory"].toString();const auto meta=QJsonDocument::fromJson(read(folder+"/metadata.json")).object();
  check(!meta["active"].toBool()&&!meta["session_end_wall_time"].toString().isEmpty()&&meta["recording_error"].toString().isEmpty(),"End metadata flush");
  check(read(folder+"/raw_input.log")==read("input.txt"),"canonical raw exact no markers or lost frames");
  const auto events=read(folder+"/session_events.jsonl").trimmed().split('\n');check(events.size()==9 &&meta["event_count"].toInt()==9,"event sidecar count");
  for(const auto &bytes:events) {auto e=QJsonDocument::fromJson(bytes).object();check(e["session_id"]==QFileInfo(folder).fileName()&&e["source_time_known"].toBool()&&!e["host_event_timestamp"].toString().isEmpty(),"traceable display event");}
  QString exported; check(w.app_export_session(QDir::currentPath(),&exported)&&read(exported+"/session_events.jsonl")==read(folder+"/session_events.jsonl"),"SHA session export includes events");
  check(!w.app_save_analysis(folder+"/session_events.jsonl")&&!w.app_export_plot(folder+"/session_events.jsonl"),"event evidence protected");
  invoke(w,"slotDraw");check(w.app_status_snapshot()["record_directory"].toString()!=folder&&!w.app_status_snapshot()["display_paused"].toBool(),"new session resets pause");
  QTest::mouseClick(pause,Qt::LeftButton);auto newFolder=w.app_status_snapshot()["record_directory"].toString();
  auto unknown=QJsonDocument::fromJson(read(newFolder+"/session_events.jsonl")).object();
  check(!unknown["source_time_known"].toBool()&&unknown["source_timestamp_ms"].isNull(),"pause before source records unknown time");
 }
 w.grab().save("keyboard.png"); invoke(w,"slotStop");
 evidence["device_pixel_ratio"]=w.devicePixelRatioF();
 if(argc>1 &&QString::fromLocal8Bit(argv[1])=="boundaries") {
  MainWindow limits;limits.show();const QString path=QDir::current().filePath("bound.txt");
  QFile big(path);check(big.open(QIODevice::WriteOnly),"bounded input open");for(int i=0;i<25000;++i)big.write(frame(0));big.close();
  limits.app_configure_input(path,"FILE_STREAM",QDir::current().filePath("bound_sessions"));invoke(limits,"slotDraw");invoke(limits,"slotPauseDisplay");
  while(limits.app_status_snapshot()["input_offset_bytes"].toDouble()<QFileInfo(path).size())invoke(limits,"slotReadFileData");
  check(limits.app_status_snapshot()["ui_points"].toInt()==24000 &&limits.app_status_snapshot()["plot_points"].toInt()==0,"long pause point cache capped without thaw");
  invoke(limits,"slotPauseDisplay");check(limits.app_status_snapshot()["plot_points"].toInt()==24000,"resume only bounded points");
  invoke(limits,"slotPauseDisplay");check(big.open(QIODevice::WriteOnly|QIODevice::Append),"long pause growing fixture");for(int i=1;i<=13000;++i)big.write(frame(i*100));big.close();
  while(limits.app_status_snapshot()["input_offset_bytes"].toDouble()<QFileInfo(path).size())invoke(limits,"slotReadFileData");
  check(limits.app_status_snapshot()["ui_points"].toInt()==12001 &&limits.app_status_snapshot()["display_oldest_elapsed_s"].toDouble()==100,"pause beyond horizon retains actual 1200 seconds");
  invoke(limits,"slotPauseDisplay");auto s=limits.app_status_snapshot();check(s["plot_points"].toInt()==12001 &&s["plot_x_lower_s"].toDouble()==220 &&s["plot_x_upper_s"].toDouble()==1420,"AUTO bounded resume source axis");
  check(big.open(QIODevice::WriteOnly|QIODevice::Append),"partial input append");big.write("TH3,SIM,1300100,1,3");big.close();invoke(limits,"slotReadFileData");
  invoke(limits,"slotStop");s=limits.app_status_snapshot();const auto folder=s["record_directory"].toString();
  check(s["parsed_count"].toInt()==38000&&read(folder+"/raw_input.log")==read(path),"raw all 38000 frames plus trailing fragment retained");
  check(s["record_summary"].toObject()["raw_fragment_count"].toInt()==1,"end records fragment explicitly");
  check(QFile::rename(folder+"/session_events.jsonl",folder+"/events_saved.jsonl"),"simulate missing referenced events");
  check(!limits.app_export_session(QDir::currentPath()),"incomplete sidecar export refused");
  check(QFile::rename(folder+"/events_saved.jsonl",folder+"/session_events.jsonl"),"restore owned test evidence");
  app_serial_device_t usb;usb.port="COM42";usb.description="USB-SERIAL CH340";
  check(app_desktop_choose_serial({usb},QString(),QString()).port=="COM42","CH340 metadata fallback without hard-coded COM");
  auto other=usb;other.port="COM43";check(app_desktop_choose_serial({usb,other},QString(),QString()).port.isEmpty(),"multiple CH340 never chosen arbitrarily");
  MainWindow ended;ended.app_configure_input(path,"FILE_STREAM",QDir::current().filePath("lock_sessions"));invoke(ended,"slotDraw");invoke(ended,"slotReadFileData");
#ifdef Q_OS_WIN
  const auto meta=ended.app_status_snapshot()["record_directory"].toString()+"/metadata.json";
  HANDLE lock=CreateFileW(reinterpret_cast<LPCWSTR>(meta.utf16()),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  check(lock!=INVALID_HANDLE_VALUE,"end metadata lock acquired");invoke(ended,"slotStop");
  check(ended.app_status_snapshot()["state"]=="ERROR"&&!ended.app_export_session(QDir::currentPath()),"End cannot claim success after metadata commit failure");
  if(lock!=INVALID_HANDLE_VALUE)CloseHandle(lock);
#endif
 }
 if(argc>1 &&QString::fromLocal8Bit(argv[1])=="wallclock") {
  if(argc<3) return 2;
  MainWindow live; live.show(); const QString input=QDir::current().filePath("paced.txt");
  QFile file(input);check(file.open(QIODevice::WriteOnly),"wallclock fixture file");
  QProcess producer; QByteArray original;
  const auto transfer=[&]() {const auto bytes=producer.readAllStandardOutput();original+=bytes;if(file.write(bytes)!=bytes.size()||!file.flush())qFatal("stream transfer");};
  QObject::connect(&producer,&QProcess::readyReadStandardOutput,transfer);
  producer.start(QString::fromLocal8Bit(argv[2]),{"--seed=0x12345678"});check(producer.waitForStarted(),"production C stream started");
  live.app_configure_input(input,"FILE_STREAM",QDir::current().filePath("wall_sessions"));invoke(live,"slotDraw");
  live.app_set_status_path(QDir::current().filePath("wall_status.json"));
  auto liveEdit=live.findChild<QLineEdit*>("axisSeconds");auto liveMode=live.findChild<QComboBox*>("axisMode");auto livePause=live.findChild<QPushButton*>("btn_stop");
  liveMode->setCurrentText("MANUAL");liveEdit->selectAll();QTest::keyClicks(liveEdit,"47");QTest::keyClick(liveEdit,Qt::Key_Return);
  const int actions[]={20000,25000,50000,80000,150000,155000,400000,430000,600000};int action=0;
  QElapsedTimer elapsed;elapsed.start();QCPRange frozen;int frozenCount=0;bool freezeChecked=false;
  while(elapsed.elapsed()<610000) {
   QTest::qWait(100);
   if(action<9 &&elapsed.elapsed()>=actions[action]) {
    QTest::mouseClick(livePause,Qt::LeftButton);auto plot=live.findChild<QCustomPlot*>("customPlot");
    if(live.app_status_snapshot()["display_paused"].toBool()) {frozen=plot->xAxis->range();frozenCount=plot->graph(0)->data()->size();}
    ++action;
   }
   auto snap=live.app_status_snapshot();check(snap["running"].toBool()&&snap["recording"].toBool(),"wallclock acquisition stays active");
   check(liveEdit->text()=="47"&&snap["x_axis_mode"]=="MANUAL"&&snap["x_axis_span_s"].toInt()==47,"wallclock MANUAL 47 retained");
   if(snap["display_paused"].toBool()) {auto plot=live.findChild<QCustomPlot*>("customPlot");check(plot->xAxis->range()==frozen&&plot->graph(0)->data()->size()==frozenCount,"wallclock freeze while C/UART-style file continues");freezeChecked=true;}
  }
  producer.terminate();if(!producer.waitForFinished(3000)){producer.kill();producer.waitForFinished(3000);}transfer();file.close();
  invoke(live,"slotStop");auto status=live.app_status_snapshot();const auto folder=status["record_directory"].toString();
  check(read(folder+"/raw_input.log")==original,"610-second C raw exact byte match");
  check(status["parsed_count"].toInt()>=6000&&!status["recording"].toBool()&&status["record_summary"].toObject()["event_count"].toInt()==9,"610-second counts/events/completion");
  check(freezeChecked&&producer.state()==QProcess::NotRunning,"no orphan producer");
  evidence["wall_duration_ms"]=double(elapsed.elapsed());evidence["wall_status"]=status;
  live.grab().save("wall_final.png");
 }
 if(argc>1 &&QString::fromLocal8Bit(argv[1])=="hardware") {
  if(argc<3)return 2;
  MainWindow board;board.show();const QString port=QString::fromLocal8Bit(argv[2]);
  const auto prepared=board.app_status_snapshot();
  check(prepared["selected_port"]==port&&!prepared["serial_open"].toBool()&&prepared["command_tx_count"].toInt()==0,"physical unique CH340 preselected without opening or SET");
  QJsonArray inventory;for(const auto &d:app_desktop_serial_devices()) {QJsonObject v;v["port"]=d.port;v["description"]=d.description;v["vendor"]=d.vendor;v["product"]=d.product;v["has_vendor"]=d.has_vendor;v["has_product"]=d.has_product;inventory.append(v);}
  evidence["serial_inventory"]=inventory;
  board.app_configure_serial(port,115200,QDir::current().filePath("board_sessions"));
  board.app_set_status_path(QDir::current().filePath("board_status.json"));invoke(board,"slotDraw");
  const auto waitFor=[&](const std::function<bool()> &predicate,int timeout=6000) {QElapsedTimer timer;timer.start();while(!predicate()&&timer.elapsed()<timeout)QTest::qWait(50);return predicate();};
  if(!waitFor([&](){return board.app_status_snapshot()["mode_readback_received"].toBool();})) {
   check(false,"physical MODE readback / port available");evidence["hardware_failure"]=board.app_status_snapshot();invoke(board,"slotStop");
  } else {
   const auto initial=board.app_status_snapshot();const QString initialT=initial["readback_temperature_source"].toString(),initialH=initial["readback_humidity_source"].toString();
   check(board.app_send_command("PING"),"physical PING submit");check(waitFor([&](){return board.app_status_snapshot()["command_state"]=="ACK,PING";}),"physical PING reply");
   check(board.app_send_command("STATUS?"),"physical STATUS submit");check(waitFor([&](){return board.app_status_snapshot()["command_state"].toString().startsWith("RSP,STATUS,1,");}),"physical STATUS reply");
   evidence["flashed_status_reply"]=board.app_status_snapshot()["command_state"];
   QTest::qWait(4000);evidence["hardware_startup"]=board.app_status_snapshot();
   const QStringList t={"REAL","REAL","SIM","SIM"},h={"REAL","SIM","SIM","REAL"};QJsonArray rounds;
   for(int round=0;round<3;++round) {
    for(int i=0;i<4;++i) {
     const int before=board.app_status_snapshot()["parsed_count"].toInt();
     check(board.app_request_sources(t[i],h[i]),QString("UART round %1 request %2/%3").arg(round+1).arg(t[i],h[i]));
     check(waitFor([&](){auto v=board.app_status_snapshot();return v["mode_confirmed"].toBool()&&v["frame_temperature_source"]==t[i]&&v["frame_humidity_source"]==h[i];}),"ACK/MODE/TH3 consistent generation/source");
     QTest::qWait(2100);auto snap=board.app_status_snapshot();
     check(snap["parsed_count"].toInt()>=before+2&&snap["serial_open"].toBool()&&snap["recording"].toBool(),"physical frames continue");
     if(t[i]=="SIM")check(snap["temperature_valid"].toBool(),"physical UART simulated temperature validity");
     if(h[i]=="SIM")check(snap["humidity_valid"].toBool(),"physical UART simulated humidity validity");
     snap["test_round"]=round+1;rounds.append(snap);
    }
   }
   evidence["hardware_source_rounds"]=rounds;
   check(board.app_request_sources(initialT,initialH),"restore initial physical source selection");
   check(waitFor([&](){auto v=board.app_status_snapshot();return v["mode_confirmed"].toBool()&&v["frame_temperature_source"]==initialT&&v["frame_humidity_source"]==initialH;}),"restored observed sources confirmed");
   auto entry=board.findChild<QLineEdit*>("axisSeconds");board.findChild<QComboBox*>("axisMode")->setCurrentText("MANUAL");entry->selectAll();QTest::keyClicks(entry,"47");QTest::keyClick(entry,Qt::Key_Return);
   for(int seconds:{5,30,5}) {
    auto plot=board.findChild<QCustomPlot*>("customPlot");const auto x=plot->xAxis->range();const int n=plot->graph(0)->data()->size(),before=board.app_status_snapshot()["parsed_count"].toInt();
    invoke(board,"slotPauseDisplay");QTest::qWait(seconds*1000);
    if(seconds==30) {
     const QString changedH=initialH=="REAL"?"SIM":"REAL";
     check(board.app_request_sources(initialT,changedH),"physical source change while paused requested");
     check(waitFor([&](){auto v=board.app_status_snapshot();return v["mode_confirmed"].toBool()&&v["frame_humidity_source"]==changedH;}),"physical source/generation remains correct while paused");
     check(board.app_request_sources(initialT,initialH),"restore source while still paused");
     check(waitFor([&](){auto v=board.app_status_snapshot();return v["mode_confirmed"].toBool()&&v["frame_humidity_source"]==initialH;}),"paused source restoration confirmed");
    }
    auto snap=board.app_status_snapshot();
    check(snap["serial_open"].toBool()&&snap["recording"].toBool()&&snap["parsed_count"].toInt()>=before+seconds-1,"physical Pause continues reception/recording");
    check(plot->xAxis->range()==x&&plot->graph(0)->data()->size()==n&&entry->text()=="47","physical frozen plot with MANUAL edit retained");
    invoke(board,"slotPauseDisplay");check(board.app_status_snapshot()["x_axis_span_s"].toInt()==47,"physical resume width");
   }
   check(board.app_export_plot(QDir::current().filePath("board_markers.png")),"physical PNG pause annotations");
   invoke(board,"slotPauseDisplay");invoke(board,"slotStop");auto stopped=board.app_status_snapshot();
   check(!stopped["serial_open"].toBool()&&!stopped["recording"].toBool()&&stopped["state"]=="STOPPED","physical End releases COM and finalizes recording");
   QString exported;check(board.app_export_session(QDir::currentPath(),&exported),"physical completed session SHA export");
   evidence["hardware_final"]=stopped;QJsonArray reconnects;
   for(int i=0;i<3;++i) {
    const auto tx=board.app_status_snapshot()["command_tx_count"].toInt();invoke(board,"slotDraw");
    check(waitFor([&](){return board.app_status_snapshot()["mode_confirmed"].toBool();}),"physical reconnect MODE/TH3 confirmation");
    QTest::qWait(2100);auto s=board.app_status_snapshot();
    check(s["command_tx_count"].toInt()==tx+1&&s["frame_temperature_source"]==initialT&&s["frame_humidity_source"]==initialH,"reconnect only MODE; no SET/source override");
    invoke(board,"slotStop");check(!board.app_status_snapshot()["serial_open"].toBool(),"reconnect End releases port");reconnects.append(board.app_status_snapshot());
   }
   evidence["hardware_reconnect_rounds"]=reconnects;board.grab().save("board_final.png");
   // A controlled software fault exercises disconnect handling on the live transport;
   // it is explicitly not evidence of physically unplugging the USB cable.
   invoke(board,"slotDraw");check(waitFor([&](){return board.app_status_snapshot()["mode_confirmed"].toBool();}),"fault-injection session connected");invoke(board,"slotPauseDisplay");
   auto transport=board.findChild<app_serial_input_t*>("serialTransport");
   check(transport!=nullptr,"owned serial transport available");
   if(transport)QMetaObject::invokeMethod(transport,"errorOccurred",Qt::DirectConnection,Q_ARG(QSerialPort::SerialPortError,QSerialPort::ResourceError));
   check(board.app_status_snapshot()["state"]=="ERROR"&&!board.app_status_snapshot()["serial_open"].toBool()&&!board.app_status_snapshot()["recording"].toBool(),"injected disconnect while paused closes and finalizes safely");
   evidence["software_disconnect_injection_on_uart"]=board.app_status_snapshot();
   invoke(board,"slotDraw");check(waitFor([&](){return board.app_status_snapshot()["mode_confirmed"].toBool();}),"recovery after injected disconnect");invoke(board,"slotStop");
   evidence["hardware_recovered_final"]=board.app_status_snapshot();
  }
 }
 evidence["checks"]=checks; evidence["failures"]=failures; evidence["pass"]=failures.isEmpty();
 QTextStream output(stdout);output.setCodec("UTF-8");output<<QString::fromUtf8(QJsonDocument(evidence).toJson());return failures.isEmpty()?0:1;
}
