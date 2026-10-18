// Native Qt actions on explicit replay fixtures; no physical port is opened.
#include "mainwindow.h"
#include <QApplication>
#include <QTest>
#include <QScreen>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTextStream>
#include <QTextBrowser>
#include <QLineEdit>
#include <QListWidget>
#include <QTabWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QAction>
#include <QMessageBox>
#include <QTimer>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
struct app_window_test_access_t {
    static void feed(MainWindow &w,const QByteArray &bytes) {
        app_line_buffer_t lines;const auto records=lines.app_line_feed_bytes(bytes);w.app_consume_records(records.records);
        w.updatePlotRealTime();w.app_render_state();
    }
    static void stop_file(MainWindow &w) {w.m_fileTimer->stop();}
    static void events(MainWindow &w,int count) {
        for(int n=0;n<count;++n) w.app_append_display_event(n%3==0?"CORRECTION_PROFILE_CHANGED":n%3==1?"PAUSE_DISPLAY":"RESUME_DISPLAY",{{"fixture","NATIVE_REPLAY_TEST"}});
        w.app_render_plot_items();
    }
    static QVector<QJsonObject> event_cache(MainWindow &w) {return w.m_displayEvents;}
    static void unknown_event(MainWindow &w) {w.app_append_display_event("SOURCE_CHANGE",{{"reason","Fixture has no source anchor"}},false);}
    static void pending_recovery(MainWindow &w) {w.m_restorePending=true;w.m_restoreState="WAITING_LIVE_READBACK";w.m_serialTimer->start();}
    static bool work_cancelled(MainWindow &w) {return !w.m_restorePending &&!w.m_serialTimer->isActive() &&!w.m_commandTimer->isActive();}
    static bool metadata_commit(MainWindow &w) {return w.app_persist_mode_state();}
    static void refresh(MainWindow &w) {w.updatePlotRealTime();w.app_render_state();}
};
static int checks=0;static QJsonArray failures;
static void check(bool ok,const QString &label) {++checks;if(!ok) failures.append(label);}
static void invoke(MainWindow &w,const char *slot) {check(QMetaObject::invokeMethod(&w,slot,Qt::DirectConnection),QString("invoke ")+slot);}
static QByteArray sample(quint32 ms,int profile=1,bool paired=true) {
    QByteArray text;
    if(paired) text=QString("[TEMP_CORR] raw=26.125C corrected=27.025C offset=0.900C raw_bits=41D10000 cal=%1 state=PROVISIONAL t_ms=%2 profile=%3\r\n").arg(profile).arg(ms).arg(profile).toLatin1();
    text+=QString("TH3,MIXED,%1,1,71,0,%2,16,27.02,42.00,1.400,NA,NA,NA,11000,NA,NA,NA,REAL,SIM,1,1,2\r\n").arg(ms).arg(profile).toLatin1();return text;
}
static void begin(MainWindow &w,const QString &name) {
    QFile fixture(name+".txt");check(fixture.open(QIODevice::WriteOnly|QIODevice::NewOnly),"create fresh replay fixture");fixture.close();
    w.app_configure_input(QFileInfo(fixture).absoluteFilePath(),"REPLAY",QDir::current().filePath(name+"_sessions"));
    invoke(w,"slotDraw");app_window_test_access_t::stop_file(w);
}
static QPoint at(QCustomPlot *plot,double x,double y,int series) {
    return {qRound(plot->xAxis->coordToPixel(x)),qRound(plot->graph(series)->valueAxis()->coordToPixel(y))};
}
static void click(MainWindow &w,QCustomPlot *plot,const QPoint &p) {w.raise();w.activateWindow();QTest::mouseClick(plot,Qt::LeftButton,Qt::NoModifier,p);QTest::qWait(30);}
static app_inspector_t *inspector(MainWindow &w) {return static_cast<app_inspector_t*>(w.findChild<QDialog*>("persistentInspector"));}
static void answer_exit(bool yes) {
    QTimer::singleShot(40,[yes]() {
        for(auto widget:QApplication::topLevelWidgets()) if(auto box=qobject_cast<QMessageBox*>(widget)) {
            if(box->objectName()!="exitConfirmation") continue;
            check(box->defaultButton() &&box->defaultButton()->text()=="No, Continue","Exit defaults to No");
            for(auto button:box->buttons()) if(button->text()==(yes?"Yes, Exit":"No, Continue")) {QTest::mouseClick(button,Qt::LeftButton);return;}
        }
        check(false,"Exit confirmation was shown");
    });
}
int main(int argc,char **argv) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    app.setProperty("appSettingsFile",QDir::current().filePath("preferences.ini"));
    if(argc>1 &&QString::fromLocal8Bit(argv[1])=="--describe-scale") {QTextStream(stdout)<<QJsonDocument(QJsonObject{{"native_qt_dpr",QGuiApplication::primaryScreen()->devicePixelRatio()}}).toJson();return 0;}
    QCoreApplication::setApplicationVersion(argc>1?QString::fromLatin1(argv[1]):"UNSPECIFIED_TEST_VERSION");
    check(app_ui_local_time("2026-10-15T23:30:10.123Z",QTimeZone(0))=="2026-10-15 23:30:10.123 UTC+00:00","UTC exact milliseconds");
    check(app_ui_local_time("2026-10-15T23:30:10.123Z",QTimeZone(8*3600))=="2026-10-16 07:30:10.123 UTC+08:00","UTC+08 crosses day without changing original");
    check(app_ui_local_time("2026-10-15T23:30:10.123Z",QTimeZone(19800))=="2026-10-16 05:00:10.123 UTC+05:30","fractional offset conversion");
    check(app_ui_local_time("2026-10-15T03:30:10.123Z",QTimeZone(-7*3600))=="2026-10-14 20:30:10.123 UTC-07:00","negative offset crosses previous day");
    check(app_ui_local_time("invalid")=="N/A","invalid time is N/A");
    const QTimeZone ny("America/New_York");
    if(ny.isValid()) {
        check(app_ui_local_time("2026-03-08T06:59:00.000Z",ny).contains("01:59:00.000 UTC-05:00"),"DST before transition");
        check(app_ui_local_time("2026-03-08T07:00:00.000Z",ny).contains("03:00:00.000 UTC-04:00"),"DST after transition");
    }
    {
        auto settings=app_desktop_open_settings();settings->setValue("serial/automaticRecovery",false);
        settings->setValue("serial/restoreConfirmedConfiguration",false);settings->sync();
        MainWindow legacy;
        check(legacy.app_status_snapshot()["configuration_restore"].toObject()["automatic_recovery_enabled"].toBool(),"ambiguous legacy default OFF migrates to ON");
        check(legacy.app_status_snapshot()["recovery_defaults_migrated"].toBool(),"legacy migration is explicitly reported");
        legacy.app_set_recovery_options(false,false);
        legacy.app_close_for_automation();
        settings->setValue("serial/recoveryPolicyVersion",2);settings->sync();
        MainWindow explicitOff;
        check(!explicitOff.app_status_snapshot()["configuration_restore"].toObject()["automatic_recovery_enabled"].toBool(),"versioned explicit OFF survives restart");
        check(!explicitOff.app_status_snapshot()["recovery_defaults_migrated"].toBool(),"explicit choice is not re-migrated");
        explicitOff.app_close_for_automation();settings->clear();settings->sync();
    }
    MainWindow w;w.resize(1100,720);w.show();QTest::qWait(100);
    const auto defaultRecovery=w.app_status_snapshot()["configuration_restore"].toObject();
    check(defaultRecovery["automatic_recovery_enabled"].toBool() &&defaultRecovery["restore_confirmed_enabled"].toBool(),"new profile recovery and confirmed restoration default ON");
    check(w.findChild<QPushButton*>("reconnectSerial")->parent()!=w.findChild<QWidget*>("sidePanel"),"manual reconnect is outside scrolling sidebar");
    check(w.app_about_text().contains(QCoreApplication::applicationVersion()) &&w.app_about_text().contains("Not verified / Disconnected"),"desktop version does not claim observed MCU");
    begin(w,"inspection");QByteArray batch;for(int n=0;n<=20;++n) batch+=sample(1000+n*1000);
    app_window_test_access_t::feed(w,batch);check(w.app_status_snapshot()["parsed_count"].toInt()==21,"fixed input rows accepted");
    w.app_set_x_window("MANUAL",32);auto plot=w.findChild<QCustomPlot*>();
    const auto session=w.app_status_snapshot()["record_directory"].toString();
    for(int series:{0,1,2}) {
        click(w,plot,at(plot,10,series==0?26.125:series==1?42.0:27.02,series));
        auto box=inspector(w);check(box &&box->isVisible() &&!box->isModal(),"persistent nonmodal selection opened");
        const auto p=box?box->point_snapshot():QJsonObject();
        check(p["series_index"].toInt(-1)==series,"nearest visible series "+QString::number(series));
        check(p["source_timestamp_ms"].toInt()==11000 &&p["segment_id"].toInt()==1,"exact source timestamp and segment");
        check(p["physical_temperature_c"].toDouble()==26.125 &&p["published_temperature_c"].toDouble()==27.02,"copied original and published values");
        check(p["transport_classification"]=="REPLAY / FIXTURE","fixture classification is explicit");
        if(box) box->grab().save(QString("point_%1.png").arg(series));
    }
    auto box=inspector(w);if(!box) return 2;
    const auto saved=box->point_snapshot();const auto manualY=plot->yAxis->range();
    click(w,plot,plot->axisRect()->rect().topLeft()+QPoint(12,12));
    check(box->point_snapshot()==saved &&box->isVisible(),"blank click keeps selected snapshot");
    QTest::mouseClick(w.findChild<QCheckBox*>("showRawTemperature"),Qt::LeftButton);
    check(box->point_snapshot()==saved &&box->isVisible(),"visibility change does not dismiss snapshot");
    check(plot->yAxis->range().lower==manualY.lower &&plot->yAxis->range().upper==manualY.upper,"short inspector clicks preserve Y");
    QTest::qWait(12500);check(box->isVisible() &&box->point_snapshot()==saved,"selection survives former12-second tooltip timeout");
    invoke(w,"slotPauseDisplay");app_window_test_access_t::feed(w,sample(22000));
    const auto frozenSource=w.app_status_snapshot()["last_source_timestamp_ms"].toInt();
    click(w,plot,at(plot,10,42,1));
    check(box->point_snapshot()["source_timestamp_ms"].toInt()==11000 &&frozenSource==22000,"paused inspector selects displayed original snapshot");
    invoke(w,"slotPauseDisplay");
    w.findChild<QCheckBox*>("showRawTemperature")->setChecked(true);
    app_window_test_access_t::feed(w,sample(23000,1,false));
    click(w,plot,at(plot,22,27.02,2));
    check(box->point_snapshot()["physical_temperature_c"].isNull() &&box->snapshot()["point_text"].toString().contains("N/A"),"unpaired original never inferred");
    app_window_test_access_t::events(w,10);
    const double eventTime=w.app_status_snapshot()["last_plot_time_s"].toDouble();
    click(w,plot,QPoint(qRound(plot->xAxis->coordToPixel(eventTime)),plot->axisRect()->rect().center().y()));
    check(box->snapshot()["tab"].toInt()==1 &&box->snapshot()["event_list_count"].toInt()>=10,"dense events open direct list");
    auto list=box->findChild<QListWidget*>("inspectorEventList");list->setCurrentRow(list->count()-3);
    const int previousIndex=box->snapshot()["event"].toObject()["event_index"].toInt();
    QTest::mouseClick(box->findChild<QPushButton*>("nextInspectorEvent"),Qt::LeftButton);
    check(box->snapshot()["event"].toObject()["event_index"].toInt()==previousIndex+1,"next event is independently selectable");
    check(!box->snapshot()["event_text"].toString().contains("PAUSE_DISPLAY"),"primary event labels are readable");
    check(box->snapshot()["event_text"].toString().contains("UTC"),"event local time includes offset");
    app_window_test_access_t::unknown_event(w);list->setCurrentRow(list->count()-1);
    check(box->snapshot()["event_text"].toString().contains("Unknown (no source-time evidence)"),"unknown source time remains unknown");
    const auto oldEvent=box->snapshot()["event"].toObject();app_window_test_access_t::events(w,270);
    check(box->snapshot()["event_list_count"].toInt()==256 &&box->snapshot()["event"].toObject()==oldEvent,"bounded list retains selected trimmed event snapshot");
    box->grab().save("events_inspector.png");
    box->findChild<QTabWidget*>("inspectorTabs")->setCurrentIndex(0);
    batch.clear();for(int n=24;n<=1250;++n) batch+=sample(quint32(n*1000));app_window_test_access_t::feed(w,batch);
    check(box->snapshot()["point_historical"].toBool() &&!box->point_snapshot().isEmpty(),"retention trim keeps labeled historical selection");
    box->hide();w.findChild<QAction*>("openInspector")->trigger();
    check(box->isVisible() &&!box->point_snapshot().isEmpty(),"explicit reopen retains saved selection");
    QTest::mouseClick(box->findChild<QPushButton*>("clearInspectorSelection"),Qt::LeftButton);
    check(box->point_snapshot().isEmpty(),"only explicit Clear removes point selection");
    w.findChild<QAction*>("userGuideAction")->trigger();QTest::qWait(50);
    auto guide=w.findChild<QDialog*>("offlineUserGuide");auto content=guide?guide->findChild<QTextBrowser*>("userGuideContent"):nullptr;
    check(guide &&guide->isVisible() &&content &&content->document()->baseUrl().scheme()=="qrc","offline guide loads from EXE resource");
    check(w.app_status_snapshot()["help_resource_loaded"].toBool(),"Help readiness telemetry agrees with loaded resource content");
    if(content) {
        auto navigation=guide->findChild<QListWidget*>("guideChapters");
        check(navigation &&navigation->count()==18,"all embedded chapters in independent navigation");
        check(navigation &&!navigation->wordWrap(),"chapter navigation keeps single-line labels");
        if(navigation) {
            bool labelsFit=true;const QFontMetrics metrics(navigation->font());
            for(int row=0;row<navigation->count();++row) {
                const auto item=navigation->item(row);
                labelsFit=labelsFit &&!item->text().contains('\n')
                    &&metrics.horizontalAdvance(item->text())<=navigation->viewport()->width()-8
                    &&!item->toolTip().isEmpty() &&item->toolTip()==item->data(Qt::UserRole).toString();
            }
            check(labelsFit,"all compact chapter labels fit actual navigation viewport with full-title tooltips");
        }
        check(!content->toPlainText().contains("18. Current measurement limitations"),"only selected chapter is displayed");
        if(navigation) for(int chapter:{1,8,11,14,17}) {
            navigation->setCurrentRow(chapter);check(guide->property("currentGuideChapter").toInt()==chapter+1,"chapter selection changes content");
            check(content->toPlainText().startsWith(navigation->currentItem()->data(Qt::UserRole).toString()),"full heading matches selected compact chapter");
        }
        guide->findChild<QLineEdit*>("guideSearch")->setText("WAITING_FOR_USER");QTest::mouseClick(guide->findChild<QPushButton*>("guideFindNext"),Qt::LeftButton);
        check(content->textCursor().selectedText()=="WAITING_FOR_USER","embedded guide search crosses chapters offline");guide->grab().save("offline_help.png");
        guide->activateWindow();QTest::keyClick(guide,Qt::Key_Escape);
        check(w.app_status_snapshot()["recording"].toBool() &&w.isVisible(),"Help Esc does not exit or stop main acquisition");guide->hide();
    }
    const auto beforeCancel=w.app_status_snapshot();
    w.raise();w.activateWindow();QTest::qWait(100);answer_exit(false);QTest::keyClick(&w,Qt::Key_Escape);
    check(w.isVisible() &&w.app_status_snapshot()["recording"].toBool(),"main Esc opens confirmation and No continues recording");
    QTimer::singleShot(40,[](){if(auto modal=QApplication::activeModalWidget()) QTest::keyClick(modal,Qt::Key_Escape);});
    w.close();check(w.isVisible() &&w.app_status_snapshot()["recording"].toBool(),"Esc in exit dialog safely cancels");
    answer_exit(false);w.close();
    check(w.isVisible() &&w.app_status_snapshot()["recording"].toBool() &&w.app_status_snapshot()["record_directory"]==beforeCancel["record_directory"],"windowX No keeps same recorder");
    answer_exit(false);QTest::mouseClick(w.findChild<QPushButton*>("btn_exit"),Qt::LeftButton);
    check(w.app_status_snapshot()["recording"].toBool(),"Exit button No keeps acquisition");
    QAction *fileExit=nullptr;for(auto a:w.findChildren<QAction*>()) if(a->text()=="Exit") fileExit=a;
    check(fileExit!=nullptr,"File Exit action exists");answer_exit(false);if(fileExit) fileExit->trigger();
    check(w.app_status_snapshot()["recording"].toBool(),"menu Exit No keeps acquisition");
    const auto count=w.app_status_snapshot()["parsed_count"].toInt();app_window_test_access_t::feed(w,sample(1251000));
    check(w.app_status_snapshot()["parsed_count"].toInt()==count+1,"acquisition continues after cancelled exits");
    w.grab().save("native_window.png");const auto dpr=w.devicePixelRatioF();answer_exit(true);w.close();
    check(!w.isVisible() &&!w.app_status_snapshot()["recording"].toBool() &&!w.app_status_snapshot()["serial_open"].toBool(),"Yes ends session and closes window");
    check(w.app_status_snapshot()["record_summary"].toObject()["stop_reason"]=="end_session","Yes uses normal End Session completion");
    QFile eventFile(QDir(session).filePath("session_events.jsonl"));check(eventFile.open(QIODevice::ReadOnly),"full event sidecar readable");
    check(eventFile.readAll().count('\n')>256,"JSONL keeps events beyond Inspector cache");
    MainWindow cycle;cycle.resize(1100,720);cycle.show();begin(cycle,"cycle_first");
    app_window_test_access_t::feed(cycle,sample(1000)+sample(2000));
    auto cyclePlot=cycle.findChild<QCustomPlot*>();click(cycle,cyclePlot,at(cyclePlot,1,26.125,0));
    auto cycleBox=inspector(cycle);const auto priorSelection=cycleBox?cycleBox->point_snapshot():QJsonObject();
    check(!priorSelection.isEmpty(),"cross-session selection fixture captured");
    begin(cycle,"cycle_second");app_window_test_access_t::feed(cycle,sample(1000)+sample(2000));
    check(cycleBox &&cycleBox->point_snapshot()==priorSelection &&cycleBox->snapshot()["point_historical"].toBool(),"new Session with identical timestamp cannot relabel old selection as current");
    cycle.app_close_for_automation();
#ifdef Q_OS_WIN
    MainWindow disk;begin(disk,"metadata_failure");app_window_test_access_t::feed(disk,sample(1000));
    const auto metadataPath=QDir(disk.app_status_snapshot()["record_directory"].toString()).filePath("metadata.json");
    HANDLE locked=CreateFileW(reinterpret_cast<const wchar_t*>(metadataPath.utf16()),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(locked!=INVALID_HANDLE_VALUE,"owned metadata lock acquired");app_window_test_access_t::pending_recovery(disk);
    check(!app_window_test_access_t::metadata_commit(disk),"fatal metadata failure remains an error");
    check(app_window_test_access_t::work_cancelled(disk) &&!disk.app_status_snapshot()["running"].toBool(),"fatal recording cancels all recovery/restore work");
    if(locked!=INVALID_HANDLE_VALUE) CloseHandle(locked);
    disk.app_close_for_automation();
#endif
    QJsonObject result{{"pass",failures.isEmpty()},{"checks",checks},{"failures",failures},{"device_pixel_ratio",dpr},
      {"classification","NATIVE WINDOWS QT GUI / REPLAY FIXTURES; no physical USB pull or measurement accuracy proof"},
      {"system_time_zone",QString::fromUtf8(QTimeZone::systemTimeZone().id())},{"DST_zone_available",ny.isValid()},
      {"final",w.app_status_snapshot()}};
    QTextStream(stdout)<<QJsonDocument(result).toJson(QJsonDocument::Indented);return failures.isEmpty()?0:1;
}
