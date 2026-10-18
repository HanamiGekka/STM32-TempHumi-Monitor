#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <QTextStream>
#include <QAction>
#include <cmath>

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication a(argc, argv);
    QCoreApplication::setApplicationName("TempHumi Monitor");
    QCoreApplication::setApplicationVersion(QStringLiteral(APP_SOFTWARE_VERSION));
    QCommandLineParser parser;
    parser.setApplicationDescription("TempHumi Monitor: TH3 only; SERIAL, FILE_STREAM and REPLAY. Sensor accuracy is independently unverified.");
    parser.addOption({"serial", "Explicit serial port; use with input-mode SERIAL.", "port"});
    parser.addOption({"baud", "Serial baud rate (8N1/no flow).", "baud", "115200"});
    parser.addOption({"test-commands", "Explicitly send PING then STATUS? during a serial test."});
    parser.addOption({"exercise-hybrid", "Explicit serial test: four source combinations then restore REAL/REAL."});
    parser.addOption({"observe-real-acquisition", "Explicit physical test: REAL/SIM at 5 s, REAL/REAL at 135 s; no SET on reconnect."});
    parser.addOption({"capture-modes", "Optional directory for classified hybrid-test UI screenshots.", "path"});
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"input", "Explicit TH3 growing/replay file.", "path"});
    parser.addOption({"input-mode", "SERIAL, FILE_STREAM or REPLAY; ordinary GUI prepares SERIAL.", "mode"});
    parser.addOption({"settings-file", "Explicit isolated preference INI (ordinary GUI uses native user preferences).", "path"});
    parser.addOption({"user-guide", "Open the embedded offline User Guide at startup."});
    parser.addOption({"record-dir", "Parent directory of raw/CSV/metadata sessions.", "path"});
    parser.addOption({"autostart", "Explicitly start file consumption and recording."});
    parser.addOption({"status-file", "Optional machine-readable software test telemetry.", "path"});
    parser.addOption({"stop-file", "Exit cleanly when this external stop request appears.", "path"});
    parser.addOption({"screenshot", "Optional clearly classified screenshot at clean exit.", "path"});
    parser.addOption({"quit-after", "Optional software test duration in seconds.", "seconds"});
    parser.addOption({"restart-after", "Optional automated Stop/Draw regression after this many seconds.", "seconds"});
    parser.process(a);
    if(parser.isSet("settings-file")) a.setProperty("appSettingsFile",QFileInfo(parser.value("settings-file")).absoluteFilePath());
    const bool explicitInput=parser.isSet("input-mode") ||parser.isSet("input") ||parser.isSet("serial");
    const QString mode = parser.isSet("input-mode")?parser.value("input-mode"):(parser.isSet("serial")?"SERIAL":(parser.isSet("input")?"FILE_STREAM":"SERIAL"));
    if ((mode != "FILE_STREAM" && mode != "REPLAY" && mode != "SERIAL")
        || (explicitInput && mode=="SERIAL" && parser.value("serial").isEmpty())
        || (mode!="SERIAL" && (parser.isSet("serial") || parser.isSet("test-commands") || parser.isSet("exercise-hybrid") || parser.isSet("observe-real-acquisition")))) {
        QTextStream(stderr) << "Select FILE_STREAM/REPLAY, or SERIAL with an explicit --serial port\n";
        return 2;
    }
    bool baudOk=false; const int baud=parser.value("baud").toInt(&baudOk);
    if(!baudOk || baud<=0 || baud>1000000) return 2;
    bool validDuration = true;
    const double duration = parser.isSet("quit-after") ? parser.value("quit-after").toDouble(&validDuration) : 0;
    if (!validDuration || !std::isfinite(duration) || duration < 0 || duration > 86400) {
        QTextStream(stderr) << "quit-after must be between 0 and 86400 seconds\n";
        return 2;
    }
    const double restart = parser.isSet("restart-after") ? parser.value("restart-after").toDouble(&validDuration) : 0;
    if(parser.isSet("exercise-hybrid") && ((duration>0 && duration<50) ||parser.isSet("test-commands"))) return 2;
    if(parser.isSet("observe-real-acquisition") && (duration<250 ||parser.isSet("test-commands") ||parser.isSet("exercise-hybrid"))) return 2;
    if (!validDuration || !std::isfinite(restart) || restart < 0 || restart > 86400) {
        QTextStream(stderr) << "restart-after must be between 0 and 86400 seconds\n";
        return 2;
    }
    MainWindow w;
    if(explicitInput) {
        if(mode=="SERIAL") w.app_configure_serial(parser.value("serial"),baud,parser.value("record-dir"));
        else w.app_configure_input(parser.value("input"), mode, parser.value("record-dir"));
    } else if(parser.isSet("record-dir")) w.app_set_recording_root(parser.value("record-dir"),false);
    w.app_set_status_path(parser.value("status-file"));
    w.show();
    if(parser.isSet("user-guide")) QTimer::singleShot(0,&w,[&w]() {w.findChild<QAction*>("userGuideAction")->trigger();});
    const QString screenshot = parser.value("screenshot");
    const QString stopPath = parser.value("stop-file");
    QTimer stopTimer;
    QObject::connect(&stopTimer, &QTimer::timeout, [&]() {
        if (!stopPath.isEmpty() && QFileInfo::exists(stopPath)) w.app_close_for_automation();
    });
    stopTimer.start(100);
    if (duration > 0) QTimer::singleShot(int(duration * 1000), &w, &MainWindow::app_close_for_automation);
    if(parser.isSet("test-commands")) {
        QTimer::singleShot(5000,&w,[&]() { w.app_send_command("PING"); });
        QTimer::singleShot(10000,&w,[&]() { w.app_send_command("STATUS?"); });
    }
    if(parser.isSet("exercise-hybrid")) {
        const QStringList temperatures={"REAL","REAL","SIM","SIM","REAL"};
        const QStringList humidities={"REAL","SIM","REAL","SIM","REAL"};
        for(int i=0;i<5;++i) QTimer::singleShot(5000+i*10000,&w,[&,i,temperatures,humidities]() {
            w.app_request_sources(temperatures[i],humidities[i]);
        });
        const QString capture=parser.value("capture-modes");
        if(!capture.isEmpty()) {
            QDir().mkpath(capture);
            for(int i=0;i<5;++i) QTimer::singleShot(13000+i*10000,&w,[&,i,capture]() { w.grab().save(QDir(capture).filePath(QString("mode_%1.png").arg(i))); });
        }
    }
    if(parser.isSet("observe-real-acquisition")) {
        QTimer::singleShot(5000,&w,[&]() { w.app_request_sources("REAL","SIM"); });
        QTimer::singleShot(135000,&w,[&]() { w.app_request_sources("REAL","REAL"); });
        const QString capture=parser.value("capture-modes");
        if(!capture.isEmpty()) {
            QDir().mkpath(capture);
            for(int i=0;i<2;++i) QTimer::singleShot(120000+i*130000,&w,[&,i,capture]() { w.grab().save(QDir(capture).filePath(QString("real_%1.png").arg(i))); });
        }
    }
    if (restart > 0) QTimer::singleShot(int(restart * 1000), &w, [&]() {
        QMetaObject::invokeMethod(&w, "slotStop", Qt::DirectConnection);
        QTimer::singleShot(1000, &w, [&]() { QMetaObject::invokeMethod(&w, "slotDraw", Qt::DirectConnection); });
    });
    QObject::connect(&a, &QCoreApplication::aboutToQuit, [&]() {
        w.app_finish_session();
        if (!screenshot.isEmpty()) w.grab().save(screenshot);
    });
    if (parser.isSet("autostart")) QTimer::singleShot(0, &w, [&]() {
        QMetaObject::invokeMethod(&w, "slotDraw", Qt::DirectConnection);
    });
    return a.exec();
}
