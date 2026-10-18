#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QCloseEvent>
#include <QShortcut>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QScreen>
#include <QGuiApplication>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QIcon>
#include <QMenuBar>
#include <QMessageBox>
#include <QRegularExpression>
#include <QApplication>
#include <QHelpEvent>
#include <QTextBrowser>
#include "app_session_export.h"
#include "app_desktop_settings.h"
#include <cmath>
#include <limits>

#ifndef APP_GIT_HEAD
#define APP_GIT_HEAD "UNKNOWN"
#endif

// 12001 points cover 1200s at 10Hz including both endpoints. The absolute
// 24000 bound also caps duplicate timestamps; raw/CSV are never downsampled.
static const int APP_DISPLAY_MAX_POINTS=24000;
static const int APP_DISPLAY_MAX_TRIGGER_MARKS=256;
static const double APP_DISPLAY_HORIZON_S=1200.0;
static const double APP_RECENT_WINDOW_S=32.0;
// Current 16-group UART windows are about1015ms. A missing whole window must
// create a segment gap; uint32 serial ordering still distinguishes wraps.
static const quint32 APP_SERIAL_SOURCE_GAP_MS=1500;

static QString app_metric_text(const app_signal_metric_t &metric)
{
    return metric.available ? QString::number(metric.value,'g',7)+(metric.unit.isEmpty()?QString():" "+metric.unit)
                            : "N/A ("+metric.reason+")";
}

static QString app_default_input_path()
{
    const QString relative="tools/qt_sim_package/output/realtime_th2.txt";
    // Locate the repository from either the executable or cwd; file existence
    // is not required because the explicit SIM launcher may create it later.
    for (const QString &start : {QCoreApplication::applicationDirPath(),QDir::currentPath()}) {
        QDir dir(start);
        for(int depth=0;depth<8;++depth) {
            if (QFileInfo::exists(dir.filePath("UIDesktop/UIDesktop.pro")) && QDir(dir.filePath("tools/qt_sim_package")).exists())
                return dir.absoluteFilePath(relative);
            if (!dir.cdUp()) break;
        }
    }
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(relative);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),ui(new Ui::MainWindow),m_fileTimer(new QTimer(this)),
      m_lastTemp(std::numeric_limits<double>::quiet_NaN()),m_lastHumi(std::numeric_limits<double>::quiet_NaN())
{
    ui->setupUi(this);
    m_serial.setParent(this); m_serial.setObjectName("serialTransport");
    m_hostClock.start();
    setWindowTitle("TempHumi Monitor "+QCoreApplication::applicationVersion()+" | TH3 only");
    setWindowIcon(QIcon(":/icons/temphumi.png"));
    auto fileMenu=menuBar()->addMenu("File"); auto settingsMenu=menuBar()->addMenu("Settings");
    auto advancedMenu=menuBar()->addMenu("Advanced");
    auto inspectorAction=advancedMenu->addAction("Recorded point / event Inspector");
    inspectorAction->setObjectName("openInspector");inspectorAction->setShortcut(QKeySequence("Ctrl+I"));
    connect(inspectorAction,&QAction::triggered,this,&MainWindow::app_show_inspector);
    auto helpMenu=menuBar()->addMenu("Help");
    auto guideAction=helpMenu->addAction("User Guide");guideAction->setObjectName("userGuideAction");guideAction->setShortcut(QKeySequence::HelpContents);
    connect(guideAction,&QAction::triggered,this,&MainWindow::app_show_user_guide);
    auto about=helpMenu->addAction("About TempHumi");
    connect(about,&QAction::triggered,this,[this]() {
        QMessageBox::about(this,"About TempHumi",app_about_text());
    });
    ui->btn_config->setText("UART Settings");
    connect(ui->btn_draw,&QPushButton::clicked,this,&MainWindow::slotDraw);
    ui->btn_stop->setText("Pause Display");
    connect(ui->btn_stop,&QPushButton::clicked,this,&MainWindow::slotPauseDisplay);
    connect(ui->btn_measure,&QPushButton::clicked,this,&MainWindow::slotMeasure);
    connect(ui->btn_exit,&QPushButton::clicked,this,&MainWindow::slotExit);
    auto escapeExit=new QShortcut(QKeySequence(Qt::Key_Escape),this);escapeExit->setObjectName("mainWindowEscapeExit");
    escapeExit->setContext(Qt::WindowShortcut);escapeExit->setAutoRepeat(false);
    connect(escapeExit,&QShortcut::activated,this,[this]() {
        if(QApplication::activeWindow()==this &&!QApplication::activeModalWidget()) slotExit();
    });
    connect(ui->btn_config,&QPushButton::clicked,this,&MainWindow::slotConfig);
    connect(settingsMenu->addAction("UART Settings"),&QAction::triggered,this,&MainWindow::slotConfig);
    connect(fileMenu->addAction("Export Plot PNG…"),&QAction::triggered,ui->btn_exportPlot,&QPushButton::click);
    connect(fileMenu->addAction("Save Analysis JSON…"),&QAction::triggered,ui->btn_saveAnalysis,&QPushButton::click);
    connect(fileMenu->addAction("End Session"),&QAction::triggered,this,&MainWindow::slotStop);
    connect(fileMenu->addAction("Exit"),&QAction::triggered,this,&MainWindow::slotExit);
    connect(ui->btn_mode,&QPushButton::clicked,this,&MainWindow::slotSwitchMode);
    connect(ui->cb_analysisChannel,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int index) {
        app_set_analysis_channel(index==1?APP_SIGNAL_HUMIDITY:APP_SIGNAL_TEMPERATURE);
    });
    connect(ui->btn_details,&QPushButton::clicked,this,[this]() {
        QDialog dialog(this); dialog.setWindowTitle("Selected-window analysis details"); dialog.resize(650,620);
        auto layout=new QVBoxLayout(&dialog); auto text=new QPlainTextEdit(&dialog); text->setReadOnly(true);
        text->setPlainText(m_hasAnalysis?QString::fromUtf8(QJsonDocument(m_analysisJson).toJson(QJsonDocument::Indented)):"N/A: NO_SELECTION");
        layout->addWidget(text); auto buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog); layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject); dialog.exec();
    });
    connect(ui->btn_saveAnalysis,&QPushButton::clicked,this,[this]() {
        if(!m_hasAnalysis) { ui->statusbar->showMessage("N/A: NO_SELECTION"); return; }
        const auto path=QFileDialog::getSaveFileName(this,"Save selected-window analysis",QDir(app_desktop_export_directory()).filePath("analysis.json"),"JSON (*.json)");
        if(!path.isEmpty()&&!app_save_analysis(path)) ui->statusbar->showMessage(m_exportError,8000);
    });
    connect(ui->btn_exportPlot,&QPushButton::clicked,this,[this]() {
        const auto path=QFileDialog::getSaveFileName(this,"Export classified plot",QDir(app_desktop_png_directory()).filePath("plot.png"),"PNG (*.png)");
        if(!path.isEmpty()&&!app_export_plot(path)) ui->statusbar->showMessage(m_exportError,8000);
    });
    connect(ui->spin_trigLevel,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,&MainWindow::slotTrigLevelChanged);
    connect(m_fileTimer,&QTimer::timeout,this,&MainWindow::slotReadFileData);
    ui->customPlot->addGraph(); ui->customPlot->addGraph(); ui->customPlot->addGraph();
    ui->customPlot->graph(0)->setName("Temperature raw"); ui->customPlot->graph(1)->setName("Humidity");
    ui->customPlot->graph(0)->setPen(QPen(Qt::red,2)); ui->customPlot->graph(1)->setPen(QPen(Qt::blue,2));
    ui->customPlot->yAxis2->setVisible(true);
    ui->customPlot->graph(0)->setValueAxis(ui->customPlot->yAxis);
    ui->customPlot->graph(1)->setValueAxis(ui->customPlot->yAxis2);
    ui->customPlot->graph(2)->setName("Temperature corrected");
    ui->customPlot->graph(2)->setPen(QPen(QColor(230,126,34),2));
    ui->customPlot->graph(2)->setValueAxis(ui->customPlot->yAxis);
    ui->customPlot->legend->setVisible(true);
    ui->customPlot->legend->setFont(QFont("Arial",9));
    ui->customPlot->xAxis->setLabel("Source session time (s)");
    ui->customPlot->yAxis->setLabel("Temperature (deg C)");
    ui->customPlot->yAxis2->setLabel("Humidity (%RH)");
    ui->customPlot->xAxis->setRange(0,5);
    ui->customPlot->yAxis->setRange(15,35); ui->customPlot->yAxis2->setRange(0,100);
    ui->customPlot->axisRect()->setMinimumMargins(QMargins(55,10,55,40));
    for(auto axis : {ui->customPlot->xAxis,ui->customPlot->yAxis,ui->customPlot->yAxis2}) {
        axis->setLabelFont(QFont("Arial",10)); axis->setTickLabelFont(QFont("Arial",9));
        axis->grid()->setPen(QPen(QColor(215,220,225),1,Qt::DotLine));
    }
    ui->customPlot->plotLayout()->insertRow(0);
    m_plotTitle=new QCPTextElement(ui->customPlot,"TempHumi "+QCoreApplication::applicationVersion(),QFont("Arial",12,QFont::Bold));
    ui->customPlot->plotLayout()->addElement(0,0,m_plotTitle);
    ui->customPlot->plotLayout()->insertRow(1);
    m_plotStatus=new QCPTextElement(ui->customPlot,"UNKNOWN source | TH3 | SENSOR ACCURACY NOT VERIFIED",QFont("Arial",8));
    ui->customPlot->plotLayout()->addElement(1,0,m_plotStatus);
    ui->customPlot->legend->setVisible(true);
    ui->customPlot->legend->setFont(QFont("Arial",9));
    ui->customPlot->legend->setBrush(QColor(255,255,255,210));
    ui->customPlot->axisRect()->insetLayout()->setInsetAlignment(0,Qt::AlignTop|Qt::AlignRight);
    ui->customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    ui->customPlot->axisRect()->setRangeZoom(Qt::Vertical);
    ui->customPlot->axisRect()->setRangeDrag(Qt::Vertical);
    m_trigLine=new QCPItemStraightLine(ui->customPlot);
    m_trigLine->point1->setCoords(0,m_trigLevel); m_trigLine->point2->setCoords(100,m_trigLevel);
    m_trigLine->setPen(QPen(QColor(120,120,120),1,Qt::DashLine));
    m_trigLine->setVisible(false); // A trigger guide must not resemble a valid blue humidity curve.
    m_line1=new QCPItemLine(ui->customPlot); m_line2=new QCPItemLine(ui->customPlot);
    for(auto line : {m_line1,m_line2}) {
        line->setPen(QPen(QColor(0,130,90),2,Qt::DashLine)); line->setVisible(false);
        line->start->setTypeY(QCPItemPosition::ptAxisRectRatio); line->end->setTypeY(QCPItemPosition::ptAxisRectRatio);
        line->start->setCoords(0,0); line->end->setCoords(0,1);
    }
    connect(ui->customPlot,&QCustomPlot::mousePress,this,&MainWindow::customPlotMousePress);
    connect(ui->customPlot,&QCustomPlot::mouseWheel,this,&MainWindow::customPlotMouseWheel);
    ui->customPlot->installEventFilter(this);
    ui->customPlot->setToolTip("Left half / left axis: temperature Y. Right half / right axis: humidity Y. Wheel to zoom; drag vertically to move. Measure selects two time cursors.");
    ui->lbl_paramResult->setText("Choose channel, then Measure and two source-time cursors.\nN/A: NO_SELECTION");
    ui->lbl_paramResult->setWordWrap(true);
    // Dynamic multiline results must grow the scroll content instead of being
    // compressed to QLabel's short minimum-size hint by the surrounding layout.
    ui->lbl_paramResult->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Minimum);
    ui->sideLayout->setSizeConstraint(QLayout::SetMinimumSize);
    ui->sideLayout->setSpacing(5);
    ui->contentSplitter->setChildrenCollapsible(false);
    ui->contentSplitter->setStretchFactor(0,1); ui->contentSplitter->setStretchFactor(1,0);
    ui->contentSplitter->setSizes({780,300});
    ui->radio_rise->setChecked(true);
    ui->lbl_value->setText("Temperature NA deg C | Humidity NA %RH");
    ui->spin_trigLevel->setRange(-1000,1000); ui->spin_trigLevel->setValue(m_trigLevel);
    m_sourceLabel=new QLabel(ui->sidePanel);
    m_sourceLabel->setObjectName("lbl_sourceState"); ui->statusLayout->addWidget(m_sourceLabel);
    m_sourceLabel->setFont(QFont("Arial",9)); m_sourceLabel->setWordWrap(true);
    m_sourceLabel->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Minimum);
    m_pathLabel=new QLabel(ui->centralwidget);
    m_pathLabel->setObjectName("lbl_inputPath"); ui->inputLayout->addWidget(m_pathLabel,1);
    m_pathLabel->setFont(QFont("Arial",9)); m_pathLabel->setWordWrap(true);
    m_pathLabel->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Minimum);
    auto select=new QPushButton("Open Replay…",ui->centralwidget);
    select->setObjectName("btn_selectReplay"); ui->inputLayout->insertWidget(0,select);
    select->hide(); connect(advancedMenu->addAction("Open Replay…"),&QAction::triggered,select,&QPushButton::click);
    ui->btn_saveAnalysis->setEnabled(false);
    connect(select,&QPushButton::clicked,this,[this]() {
        const QString path=QFileDialog::getOpenFileName(this,"TH3 Replay",m_input.app_file_path(),"Text files (*.txt *.log);;All files (*)");
        if(!path.isEmpty()) { slotStop(); app_configure_input(path,"REPLAY",m_recordDirectory); }
    });
    auto stream=new QPushButton("Open File Stream…",ui->centralwidget); stream->setObjectName("btn_selectStream");
    ui->inputLayout->insertWidget(1,stream);
    stream->hide(); connect(advancedMenu->addAction("Open File Stream…"),&QAction::triggered,stream,&QPushButton::click);
    connect(stream,&QPushButton::clicked,this,[this]() {
        const QString path=QFileDialog::getOpenFileName(this,"TH3 Growing File",m_input.app_file_path(),"Text files (*.txt *.log);;All files (*)");
        if(!path.isEmpty()) app_configure_input(path,"FILE_STREAM",m_recordDirectory);
    });
    const auto settings=app_desktop_open_settings();
    // Legacy code persisted the default false without recording user intent.
    // Preserve versioned explicit choices; migrate ambiguous legacy defaults
    // visibly rather than pretending the old boolean proves an intentional OFF.
    const bool explicitPolicy=settings->value("serial/recoveryPolicyVersion",0).toInt()>=2;
    m_recoveryMigrationNotice=settings->value("serial/recoveryMigrationNoticePending",false).toBool()
        ||(!explicitPolicy &&settings->contains("serial/automaticRecovery"));
    m_autoRecovery=explicitPolicy?settings->value("serial/automaticRecovery",true).toBool():true;
    m_restoreConfirmedOption=m_autoRecovery &&(explicitPolicy?settings->value("serial/restoreConfirmedConfiguration",true).toBool():true);
    if(!explicitPolicy) {
        settings->setValue("serial/recoveryPolicyVersion",2);
        settings->setValue("serial/recoveryPreferenceOrigin",m_recoveryMigrationNotice?"migrated_legacy_unknown":"new_default");
        settings->setValue("serial/automaticRecovery",m_autoRecovery);
        settings->setValue("serial/restoreConfirmedConfiguration",m_restoreConfirmedOption);
        settings->setValue("serial/recoveryMigrationNoticePending",m_recoveryMigrationNotice);settings->sync();
    }
    m_boundMcuIdentityHash=settings->value("serial/confirmedMcuIdentityHash").toString();
    m_recovery.configure(m_autoRecovery);
    const auto device=app_desktop_choose_serial(app_desktop_serial_devices(),settings->value("serial/lastSuccessfulPort",settings->value("serial/port")).toString(),settings->value("serial/serialNumber").toString());
    m_deviceSelectionReason=device.reason;
    if(m_recoveryMigrationNotice) m_deviceSelectionReason+=" | Recovery defaults upgraded to ON; review UART Settings (legacy OFF intent was not recorded)";
    app_configure_serial(device.port,settings->value("serial/baud",115200).toInt(),app_desktop_recording_root());
    m_latestSession=settings->value("latestSession/path").toString();
    m_latestSessionIdentity=settings->value("latestSession/identity").toString();
    QString sessionError;
    if(!app_session_validate_directory(m_latestSession,&sessionError) ||m_latestSessionIdentity!=QFileInfo(m_latestSession).fileName()) m_latestSession.clear();
    m_latestExport=settings->value("latestExport").toString();
    connect(&m_serial,&QSerialPort::readyRead,this,&MainWindow::app_read_serial);
    connect(&m_serial,QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::errorOccurred),this,&MainWindow::app_serial_error);
    m_serialTimer=new QTimer(this);m_serialTimer->setInterval(250);
    connect(m_serialTimer,&QTimer::timeout,this,&MainWindow::app_serial_tick);
    auto reconnect=new QPushButton("Reconnect Selected Port",ui->centralwidget);
    reconnect->setObjectName("reconnectSerial");ui->inputLayout->addWidget(reconnect);
    auto recoveryBanner=new QLabel(ui->centralwidget);recoveryBanner->setObjectName("recoveryBanner");
    recoveryBanner->setWordWrap(true);recoveryBanner->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Minimum);
    ui->mainLayout->insertWidget(2,recoveryBanner);
    connect(reconnect,&QPushButton::clicked,this,[this]() { app_reconnect_serial(); });
    m_commandTimer=new QTimer(this); m_commandTimer->setSingleShot(true);
    connect(m_commandTimer,&QTimer::timeout,this,[this]() {
        m_commandState="COMMAND_TIMEOUT: "+m_pendingCommand; m_pendingCommand.clear();
        if(m_modeAwaiting) { m_modeAwaiting=false; m_modeConfirmed=false; m_modeState="MODE_CHANGE_UNCONFIRMED"; }
        app_persist_mode_state(); app_render_state(); app_write_status();
    });
    for(const QString &command : {QString("PING"),QString("STATUS?")}) {
        auto button=new QPushButton(command=="PING"?"Send PING":"Query MCU STATUS",ui->sidePanel);
        ui->statusLayout->addWidget(button);
        button->hide(); connect(advancedMenu->addAction(button->text()),&QAction::triggered,button,&QPushButton::click);
        connect(button,&QPushButton::clicked,this,[this,command]() {
            if(!app_send_command(command)) ui->statusbar->showMessage("Command unavailable: connect serial and wait for prior reply");
        });
    }
    auto temperatureChoice=new QComboBox(ui->sidePanel);
    auto humidityChoice=new QComboBox(ui->sidePanel);
    temperatureChoice->setObjectName("temperatureSourceChoice"); humidityChoice->setObjectName("humiditySourceChoice");
    for(auto choice : {temperatureChoice,humidityChoice}) choice->addItems({"REAL","SIM"});
    for(auto choice : {temperatureChoice,humidityChoice}) connect(choice,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]() { m_sourceChoicesEdited=true; });
    ui->statusLayout->addWidget(new QLabel("Choose temperature source",ui->sidePanel)); ui->statusLayout->addWidget(temperatureChoice);
    ui->statusLayout->addWidget(new QLabel("Choose humidity source",ui->sidePanel)); ui->statusLayout->addWidget(humidityChoice);
    auto applySources=new QPushButton("Apply Sources",ui->sidePanel); applySources->setObjectName("applySources"); ui->statusLayout->addWidget(applySources);
    connect(applySources,&QPushButton::clicked,this,[this,temperatureChoice,humidityChoice]() {
        if(!app_request_sources(temperatureChoice->currentText(),humidityChoice->currentText()))
            ui->statusbar->showMessage("Mode request unavailable: connect serial and finish pending request");
    });
    auto calibrationChoice=new QComboBox(ui->sidePanel);calibrationChoice->setObjectName("calibrationChoice");
    calibrationChoice->addItems({"0 Off / original","1 Ambient offset","2 Segmented LAD","3 Segmented Huber"});
    calibrationChoice->setCurrentIndex(1);
    connect(calibrationChoice,QOverload<int>::of(&QComboBox::activated),this,[this]() { m_calibrationChoiceEdited=true; });
    calibrationChoice->setToolTip("Provisional fits; independent accuracy not verified. Supported raw range 25.2–35.6 deg C; no empirical correction outside.");
    ui->statusLayout->addWidget(calibrationChoice);
    auto curveVisibility=new QWidget(ui->sidePanel);auto curveLayout=new QVBoxLayout(curveVisibility);
    curveLayout->setContentsMargins(0,0,0,0);curveLayout->setSpacing(0);
    for(const auto &entry:{qMakePair(QString("showRawTemperature"),QString("Raw temperature")),
          qMakePair(QString("showCorrectedTemperature"),QString("Corrected temperature")),
          qMakePair(QString("showHumidity"),QString("Humidity"))}) {
        auto check=new QCheckBox(entry.second,curveVisibility);check->setObjectName(entry.first);check->setChecked(true);curveLayout->addWidget(check);
        int graph=entry.first=="showRawTemperature"?0:entry.first=="showHumidity"?1:2;
        connect(check,&QCheckBox::toggled,this,[this,graph](bool visible) {
            ui->customPlot->graph(graph)->setVisible(visible);
            if(graph==2) m_correctedVisiblePreference=visible;
            updatePlotRealTime();ui->customPlot->replot();
        });
    }
    ui->statusLayout->addWidget(curveVisibility);
    auto applyCalibration=new QPushButton("Apply Temperature Fit",ui->sidePanel);applyCalibration->setObjectName("applyCalibration");
    ui->statusLayout->addWidget(applyCalibration);
    connect(applyCalibration,&QPushButton::clicked,this,[this,calibrationChoice]() {
        if(!app_request_calibration(calibrationChoice->currentIndex()))
            ui->statusbar->showMessage("Connect the board and wait for the pending command; fit was not applied.",7000);
    });
    auto queryMode=new QPushButton("Query Effective Mode",ui->sidePanel); ui->statusLayout->addWidget(queryMode);
    queryMode->hide(); connect(advancedMenu->addAction(queryMode->text()),&QAction::triggered,queryMode,&QPushButton::click);
    connect(queryMode,&QPushButton::clicked,this,[this]() { app_send_command("MODE?"); });
    auto statusTimer=new QTimer(this);
    connect(statusTimer,&QTimer::timeout,this,[this]() {
        // Enumeration may lag application startup. Prepare a unique detected device,
        // without opening it or overriding any explicit/manual selection.
        if(m_inputMode=="SERIAL" &&!m_running &&m_serialPort.isEmpty()) {
            const auto s=app_desktop_open_settings();
            const auto chosen=app_desktop_choose_serial(app_desktop_serial_devices(),s->value("serial/lastSuccessfulPort",s->value("serial/port")).toString(),s->value("serial/serialNumber").toString());
            m_deviceSelectionReason=chosen.reason;
            if(!chosen.port.isEmpty()) app_configure_serial(chosen.port,m_serialBaud,m_recordDirectory);
        }
        app_write_status();
    }); statusTimer->start(1000);
    ui->btn_mode->hide(); // Compatibility slot retained; AUTO/MANUAL replaces the old 32/60s UI.
    auto axisBox=new QWidget(ui->sidePanel); auto axisRow=new QHBoxLayout(axisBox); axisRow->setContentsMargins(0,0,0,0);
    auto axisMode=new QComboBox(axisBox); axisMode->setObjectName("axisMode"); axisMode->addItems({"AUTO","MANUAL"});
    auto seconds=new QLineEdit("5",axisBox); seconds->setObjectName("axisSeconds"); seconds->setMaximumWidth(65); seconds->setReadOnly(true); seconds->setToolTip("Integer seconds: 5 to 1200");
    auto applySpan=new QPushButton("Apply",axisBox); applySpan->setObjectName("applyAxisSpan");
    axisRow->addWidget(axisMode); axisRow->addWidget(seconds); axisRow->addWidget(applySpan);
    ui->statusLayout->insertWidget(0,new QLabel("X window (5–1200 seconds)",ui->sidePanel)); ui->statusLayout->insertWidget(1,axisBox);
    const auto applyAxis=[this,axisMode,seconds]() {
        bool ok=false; const int value=seconds->text().toInt(&ok);
        if(!ok ||!app_set_x_window(axisMode->currentText(),value)) {
            seconds->setToolTip("Enter an integer between 5 and 1200 seconds");
            ui->statusbar->showMessage("X window: enter an integer between 5 and 1200 seconds",6000);
        }
    };
    connect(applySpan,&QPushButton::clicked,this,applyAxis);
    connect(seconds,&QLineEdit::returnPressed,this,applyAxis);
    connect(axisMode,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this,axisMode,seconds]() {
        if(axisMode->currentText()=="AUTO") app_set_x_window("AUTO",5);
        else app_set_x_window("MANUAL",m_plotWindow.app_span());
    });
    auto resetY=new QPushButton("Reset Y Scale",ui->sidePanel); resetY->setObjectName("resetYScale"); ui->statusLayout->insertWidget(2,resetY);
    connect(resetY,&QPushButton::clicked,this,&MainWindow::app_reset_y_scale);
    auto folderLabel=new QLabel(ui->sidePanel); folderLabel->setObjectName("recordingRootLabel"); folderLabel->setWordWrap(true); folderLabel->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Minimum);
    ui->statusLayout->addWidget(folderLabel);
    auto endSession=new QPushButton("End Session",ui->sidePanel); endSession->setObjectName("endSession"); ui->statusLayout->addWidget(endSession);
    connect(endSession,&QPushButton::clicked,this,&MainWindow::slotStop);
    for(const auto &name:{QString("Recording Folder"),QString("Choose Recording Location…"),QString("Open Latest Session"),QString("Export Session…")}) {
        auto button=new QPushButton(name,ui->sidePanel); ui->statusLayout->addWidget(button);
        if(name=="Choose Recording Location…") { button->hide(); connect(settingsMenu->addAction(name),&QAction::triggered,button,&QPushButton::click); }
        if(name=="Export Session…") { button->hide(); connect(fileMenu->addAction(name),&QAction::triggered,button,&QPushButton::click); }
        if(name=="Open Latest Session") {
            button->setObjectName("openLatestSession"); connect(button,&QPushButton::clicked,this,[this]() { if(!app_open_latest_session()) ui->statusbar->showMessage(m_exportError,8000); });
        } else if(name=="Choose Recording Location…") connect(button,&QPushButton::clicked,this,[this]() {
            const auto path=QFileDialog::getExistingDirectory(this,"Choose Recording Location",m_recordDirectory);
            if(!path.isEmpty()) app_set_recording_root(path);
        });
        else if(name=="Export Session…") {
            button->setObjectName("exportLatestSession"); connect(button,&QPushButton::clicked,this,[this]() {
                const auto path=QFileDialog::getExistingDirectory(this,"Export stopped session to folder",app_desktop_export_directory());
                if(!path.isEmpty() &&!app_export_session(path)) ui->statusbar->showMessage(m_exportError,8000);
            });
        } else if(name=="Recording Folder") connect(button,&QPushButton::clicked,this,[this]() {
            if(QDir(m_recordDirectory).exists()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_recordDirectory));
            else ui->statusbar->showMessage("Recording folder will be created on Start: "+m_recordDirectory,8000);
        });
        else connect(button,&QPushButton::clicked,this,[this]() {
            if(QFileInfo::exists(m_latestExport)) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_latestExport).absolutePath()));
            else ui->statusbar->showMessage("No existing export yet",5000);
        });
    }
    ui->triggerGroup->hide();
    connect(advancedMenu->addAction("Display Trigger Settings…"),&QAction::triggered,this,[this]() {
        QDialog dialog(this); dialog.setWindowTitle("Display trigger (selected analysis channel)");
        auto row=new QVBoxLayout(&dialog);
        row->addWidget(new QLabel("Visual crossing marks only; response metrics use the selected data window.\nThis is unrelated to MCU PWM triggering.",&dialog));
        auto channel=new QComboBox(&dialog); channel->addItems({"Temperature (deg C)","Humidity (%RH)"}); channel->setCurrentIndex(ui->cb_analysisChannel->currentIndex()); row->addWidget(channel);
        auto edge=new QComboBox(&dialog); edge->addItems({"Rising edge","Falling edge"}); edge->setCurrentIndex(ui->radio_rise->isChecked()?0:1); row->addWidget(edge);
        auto level=new QDoubleSpinBox(&dialog); level->setRange(-1000,1000); level->setDecimals(2); level->setValue(m_trigLevel); row->addWidget(level);
        auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog); row->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()==QDialog::Accepted) { ui->cb_analysisChannel->setCurrentIndex(channel->currentIndex()); ui->radio_rise->setChecked(edge->currentIndex()==0); ui->radio_fall->setChecked(edge->currentIndex()==1); ui->spin_trigLevel->setValue(level->value()); }
    });
    app_render_state();
    if(auto screen=QGuiApplication::primaryScreen()) {
        const QRect area=screen->availableGeometry();
        setMinimumSize(qMin(720,int(area.width()*.9)),qMin(480,int(area.height()*.8)));
        resize(int(area.width()*.85),int(area.height()*.85));
        move(area.center()-rect().center());
    }
}

MainWindow::~MainWindow() { app_finish_session(); delete ui; }

void MainWindow::app_configure_input(const QString &path,const QString &mode,const QString &recordDirectory)
{
    if(m_running) slotStop();
    const QString resolved=path.isEmpty() ? app_default_input_path() : QFileInfo(path).absoluteFilePath();
    const QString inputMode=mode=="REPLAY"?"REPLAY":"FILE_STREAM";
    const bool changed=resolved!=m_input.app_file_path() || inputMode!=m_inputMode;
    if(resolved != m_input.app_file_path()) m_input.app_file_set_path(resolved);
    if(changed) {
        // A newly selected transport/path must not relabel old samples or old
        // analysis as belonging to the new input while waiting for Draw.
        app_clear_plot_segment(); m_timeline.app_th3_reset_segment("input_configuration_changed");
        m_lastTime=app_th3_time_t(); m_lastTimestamp=0; m_validFlags=m_errorFlags=0;
        m_temperatureEvidence.reset();
    m_frameTemperature=m_frameHumidity="UNKNOWN"; m_temperatureValid=m_humidityValid=false;
        m_frameProtocol="TH3"; m_frameGeneration=0; m_modeConfirmed=false; m_modeState="EFFECTIVE MODE UNKNOWN";
        ui->lbl_value->setText("Temperature NA deg C | Humidity NA %RH");
    }
    m_inputMode=inputMode;
    m_recordDirectory=recordDirectory.isEmpty()?app_desktop_recording_root():QFileInfo(recordDirectory).absoluteFilePath();
    m_source="UNKNOWN"; m_state="STOPPED"; m_diagnostic.clear();
    m_temperatureEvidence.reset();m_requestedCalibration=m_effectiveCalibration=m_frameCalibration=-1;
    m_calibrationConfirmed=false;m_calibrationApplied=false;m_calibrationState="Correction profile unknown";
    m_pathLabel->setText("Input file: " + resolved); app_render_state();
}

void MainWindow::app_set_status_path(const QString &path) { m_statusPath=path; app_write_status(); }

void MainWindow::app_configure_serial(const QString &port,int baud,const QString &recordDirectory)
{
    if(m_running &&m_inputMode=="SERIAL" &&!m_serial.isOpen()) {
        m_serialPort=port;m_serialBaud=baud;
        m_deviceSelectionReason="Manual selection during recovery; click Reconnect Selected Port";
        app_record_connection_event("SERIAL_MANUAL_SELECTION");app_render_state();return;
    }
    app_configure_input(QString(),"FILE_STREAM",recordDirectory);
    m_inputMode="SERIAL"; m_serialPort=port; m_serialBaud=baud;
    m_connectionState="DISCONNECTED";
    m_pathLabel->setText(QString("SERIAL | %1 | %2 | 8N1 | Disconnected").arg(port.isEmpty()?"No device selected":port).arg(baud));
    app_render_state();
}

void MainWindow::app_set_recording_root(const QString &path,bool persist)
{
    if(m_running) slotStop();
    m_recordDirectory=QFileInfo(path).absoluteFilePath();
    if(persist) { const auto s=app_desktop_open_settings(); s->setValue("recordingRoot",m_recordDirectory); s->sync(); }
    app_render_state(); app_write_status();
}

bool MainWindow::app_send_command(const QString &command)
{
    const bool calibrationCommand=command=="CAL,0" ||command=="CAL,1" ||command=="CAL,2" ||command=="CAL,3";
    const bool setCommand=command=="SET,TEMP,REAL" || command=="SET,TEMP,SIM" || command=="SET,HUM,REAL" || command=="SET,HUM,SIM"
        ||command=="SET,BOTH,REAL,REAL" ||command=="SET,BOTH,REAL,SIM" ||command=="SET,BOTH,SIM,REAL" ||command=="SET,BOTH,SIM,SIM";
    if(!m_running ||m_stopping ||m_serialIoBusy ||m_inputMode!="SERIAL" || !m_serial.isOpen() || !m_pendingCommand.isEmpty()
       || (command!="PING" &&command!="ID?" && command!="STATUS?" && command!="MODE?" &&command!="CAL?" &&!calibrationCommand && !setCommand) || m_serial.bytesToWrite()!=0
       ||!m_connectionHasFrame || ((setCommand ||calibrationCommand) &&!m_modeConfirmed)
       || (setCommand && m_modeAwaiting)) return false;
    if((setCommand ||calibrationCommand) &&m_restorePending &&!m_autoRestoreCommand) {
        m_restorePending=false;m_restoreState="CANCELLED_BY_USER";
        m_restoreReason="Explicit source/fit command superseded automatic restoration";
        if(!app_record_connection_event("CONFIGURATION_RESTORE_CANCELLED",app_restore_snapshot())) return false;
    }
    const QByteArray bytes=("CMD,"+command+"\r\n").toLatin1();
    const auto failCommandLog=[this](const QString &detail) {
        m_state="ERROR";m_diagnostic="Command TX recording failed: "+detail;m_stopReasonOverride="command_recording_error";
        if(!app_record_connection_event("COMMAND_RECORDING_ERROR",{{"error",m_diagnostic}})) return false;
        slotStop();return false;
    };
    // Log the exact intended TX bytes before submitting them to the OS.
    QFile log(QDir(m_recorder.app_session_directory()).filePath("command_tx.jsonl"));
    if(!log.open(QIODevice::WriteOnly|QIODevice::Append)) return failCommandLog(log.errorString());
    QJsonObject event; event["host_tx_at"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    event["bytes_hex"]=QString::fromLatin1(bytes.toHex()); event["event"]="TX_ATTEMPT";
    const QByteArray json=QJsonDocument(event).toJson(QJsonDocument::Compact)+'\n';
    if(log.write(json)!=json.size() || !log.flush()) return failCommandLog(log.errorString());
    if(m_serial.write(bytes)!=bytes.size()) { m_commandState="TX_FAILED"; return false; }
    ++m_commandTx; m_pendingCommand=command; m_commandState="WAITING: "+command;
    m_commandTimer->start(3000);
    if(calibrationCommand) {
        m_requestedCalibration=command.right(1).toInt();m_calibrationConfirmed=false;
        m_effectiveCalibration=-1; // A prior same-ID readback cannot confirm a new request.
        if(!m_autoRestoreCommand) m_calibrationChoiceEdited=false;
        m_calibrationState="Fit change unconfirmed";
    }
    if(setCommand) {
        const auto fields=command.split(',');
        if(fields[1]=="TEMP") m_requestedTemperature=fields[2];
        else if(fields[1]=="HUM") m_requestedHumidity=fields[2];
        else { m_requestedTemperature=fields[2]; m_requestedHumidity=fields[3]; }
        if(!m_autoRestoreCommand ||!m_sourceChoicesEdited) {
            const QSignalBlocker a(findChild<QComboBox*>("temperatureSourceChoice")),b(findChild<QComboBox*>("humiditySourceChoice"));
            findChild<QComboBox*>("temperatureSourceChoice")->setCurrentText(m_requestedTemperature);
            findChild<QComboBox*>("humiditySourceChoice")->setCurrentText(m_requestedHumidity);
        }
        m_modeAwaiting=true; m_modeReadback=false; m_modeConfirmed=false; m_expectedGeneration=0;
        m_modeState="MODE_CHANGE_UNCONFIRMED";
        if(!app_persist_mode_state()) return false;
    }
    app_render_state(); app_write_status(); return true;
}

bool MainWindow::app_request_calibration(int profile)
{
    if(profile<0 || profile>3) return false;
    return app_send_command("CAL,"+QString::number(profile));
}

bool MainWindow::app_request_sources(const QString &temperature,const QString &humidity)
{
    if((temperature!="REAL" && temperature!="SIM") || (humidity!="REAL" && humidity!="SIM")) return false;
    return app_send_command("SET,BOTH,"+temperature+","+humidity);
}

bool MainWindow::app_persist_mode_state()
{
    QJsonObject state;
    state["requested_temperature_source"]=m_requestedTemperature; state["requested_humidity_source"]=m_requestedHumidity;
    state["observed_temperature_source"]=m_frameTemperature; state["observed_humidity_source"]=m_frameHumidity;
    state["generation"]=double(m_frameGeneration); state["confirmed"]=m_modeConfirmed; state["state"]=m_modeState;
    if(m_inputMode=="SERIAL") { state["connection"]=m_recovery.snapshot();state["connection_state"]=m_connectionState;state["configuration_restore"]=app_restore_snapshot();
        state["calibration_confirmed"]=m_calibrationConfirmed;state["observed_firmware"]=m_firmwareCurrent?m_observedFirmware:QString();
        state["requested_calibration_profile"]=m_requestedCalibration>=0?QJsonValue(m_requestedCalibration):QJsonValue();
        state["effective_calibration_profile"]=m_effectiveCalibration>=0?QJsonValue(m_effectiveCalibration):QJsonValue();
        state["observed_frame_profile"]=m_frameCalibration>=0?QJsonValue(m_frameCalibration):QJsonValue(); }
    if(!m_recorder.app_record_control_state(state)) {
        m_fileTimer->stop();m_running=false;app_cancel_recovery_work("Recording metadata failed");app_close_serial();m_state="ERROR";m_diagnostic=m_recorder.app_session_error();return false;
    }
    return true;
}

void MainWindow::app_read_serial()
{
    if(!m_running ||m_stopping ||m_serialIoBusy ||m_inputMode!="SERIAL" ||!m_serial.isOpen()) return;
    const auto before=m_parsedCount;
    app_consume_records(m_serial.app_serial_read().records);
    if(m_running && m_serial.bytesAvailable()>0) QTimer::singleShot(0,this,&MainWindow::app_read_serial);
    app_render_state(); if(m_parsedCount!=before) updatePlotRealTime();
}

QJsonObject MainWindow::app_status_snapshot() const
{
    QJsonObject out;
    out["running"]=m_running; out["state"]=m_state; out["source"]=m_source;
    out["source_label"]=m_sourceLabel->text(); out["value_label"]=ui->lbl_value->text();
    out["input_mode"]=m_inputMode; out["input_path"]=m_input.app_file_path();
    out["input_offset_bytes"]=double(m_input.app_file_offset()); out["pending_bytes"]=m_input.app_file_pending_bytes();
    out["parsed_count"]=double(m_parsedCount); out["malformed_count"]=double(m_malformedCount);
    out["diagnostic_count"]=double(m_diagnosticCount); out["restart_count"]=double(m_restartCount);
    out["ui_points"]=m_dataBuf.size(); out["max_ui_points"]=m_maxUiPoints;
    out["plot_points"]=ui->customPlot->graph(0)->data()->size();
    out["display_paused"]=m_displayPaused; out["display_segment_id"]=double(m_displaySegment);
    out["display_event_cache_count"]=m_displayEvents.size();
    out["segment_id"]=double(m_lastTime.segment_id); out["last_elapsed_s"]=m_lastTime.elapsed_s;
    out["last_source_timestamp_ms"]=double(m_lastTimestamp);
    out["recording"]=m_recorder.app_session_active(); out["record_directory"]=m_recorder.app_session_directory();
    out["record_summary"]=m_recorder.app_session_summary();
    out["error"]=m_diagnostic; out["protocol"]="TH3"; out["hardware_status"]="NOT TESTED ON HARDWARE";
    out["git_head"]=QString(APP_GIT_HEAD); out["qt_version"]=qVersion();
    out["software_version"]=QCoreApplication::applicationVersion();
    out["window_visible"]=isVisible(); out["window_width"]=width(); out["window_height"]=height();
    out["window_min_width"]=minimumSizeHint().width(); out["window_min_height"]=minimumSizeHint().height();
    out["connection_state"]=m_connectionState; out["selected_port"]=m_serialPort;
    out["serial_recovery"]=m_recovery.snapshot();out["inspection"]=m_inspector?m_inspector->current_selection():m_inspection;
    out["inspector"]=m_inspector?m_inspector->snapshot():QJsonObject();out["configuration_restore"]=app_restore_snapshot();
    out["help_embedded_available"]=QFile::exists(":/help/user_guide.html");
    out["help_resource_loaded"]=m_userGuide &&m_userGuide->property("embeddedGuideLoaded").toBool();
    out["mcu_identity_verified"]=m_mcuIdentityCurrent;out["mcu_identity_hash"]=m_mcuIdentityHash;
    out["mcu_identity_error"]=m_mcuIdentityError;out["recovery_defaults_migrated"]=m_recoveryMigrationNotice;
    out["observed_firmware"]=m_observedFirmware;out["firmware_current_connection_verified"]=m_firmwareCurrent &&m_serial.isOpen();
    out["about_text"]=app_about_text();
    out["last_plot_time_s"]=m_dataBuf.isEmpty()?0:m_dataBuf.last().time;
    out["active_port"]=m_serial.isOpen()?m_serial.portName():QString(); out["recording_root"]=m_recordDirectory;
    out["device_selection_reason"]=m_deviceSelectionReason;
    out["latest_session"]=m_latestSession; out["last_export_directory"]=app_desktop_export_directory(); out["latest_export"]=m_latestExport;
    out["classification"]=app_classification(); out["frame_source"]=m_source;
    if(m_inputMode=="SERIAL") {
        out["input_path"]=m_serialPort; out["baud"]=m_serialBaud;
        out["input_offset_bytes"]=m_recorder.app_session_summary()["raw_byte_count"].toDouble()+m_serial.app_serial_pending_bytes();
        out["current_connection_received_bytes"]=double(m_serial.app_serial_received_bytes());
        out["pending_bytes"]=m_serial.app_serial_pending_bytes(); out["serial_open"]=m_serial.isOpen();
        out["hardware_status"]=m_serial.app_serial_received_bytes()>0?"UART OBSERVED; SENSOR ACCURACY NOT VERIFIED":"UART NOT OBSERVED; SENSOR ACCURACY NOT VERIFIED";
    }
    out["command_state"]=m_commandState; out["command_tx_count"]=double(m_commandTx); out["command_rx_count"]=double(m_commandRx);
    out["protocol"]=m_frameProtocol;
    out["requested_temperature_source"]=m_requestedTemperature; out["requested_humidity_source"]=m_requestedHumidity;
    out["readback_temperature_source"]=m_readbackTemperature; out["readback_humidity_source"]=m_readbackHumidity;
    out["readback_generation"]=double(m_readbackGeneration); out["mode_readback_received"]=m_modeReadback;
    out["frame_temperature_source"]=m_frameTemperature; out["frame_humidity_source"]=m_frameHumidity;
    out["temperature_valid"]=m_temperatureValid; out["humidity_valid"]=m_humidityValid;
    out["mode_generation"]=double(m_frameGeneration); out["mode_confirmed"]=m_modeConfirmed; out["mode_state"]=m_modeState;
    out["display_max_points"]=APP_DISPLAY_MAX_POINTS;
    out["display_horizon_s"]=APP_DISPLAY_HORIZON_S; out["recent_window_s"]=APP_RECENT_WINDOW_S;
    out["display_oldest_elapsed_s"]=m_dataBuf.isEmpty()?0:m_dataBuf.first().time;
    out["display_span_s"]=m_dataBuf.isEmpty()?0:m_dataBuf.last().time-m_dataBuf.first().time;
    out["view_mode"]=m_plotWindow.app_manual()?"MANUAL":"AUTO";
    out["view_button_text"]=ui->btn_mode->text();
    out["x_axis_mode"]=m_plotWindow.app_manual()?"MANUAL":"AUTO"; out["x_axis_span_s"]=m_plotWindow.app_span();
    out["raw_temperature_visible"]=ui->customPlot->graph(0)->visible();
    out["corrected_temperature_visible"]=ui->customPlot->graph(2)->visible();
    out["humidity_visible"]=ui->customPlot->graph(1)->visible();
    out["corrected_temperature_checkbox_enabled"]=findChild<QCheckBox*>("showCorrectedTemperature")->isEnabled();
    out["calibration_requested"]=m_requestedCalibration;out["calibration_effective"]=m_effectiveCalibration;
    out["calibration_frame_profile"]=m_frameCalibration;out["calibration_confirmed"]=m_calibrationConfirmed;
    out["calibration_state"]=m_calibrationState;
    out["calibration_applied"]=m_calibrationApplied;
    out["temperature_raw_c"]=m_dataBuf.isEmpty()||!std::isfinite(m_dataBuf.last().raw_temp)?QJsonValue():QJsonValue(m_dataBuf.last().raw_temp);
    out["temperature_corrected_c"]=m_dataBuf.isEmpty()||!std::isfinite(m_dataBuf.last().corrected_temp)?QJsonValue():QJsonValue(m_dataBuf.last().corrected_temp);
    out["temperature_y_manual"]=m_temperatureYManual; out["humidity_y_manual"]=m_humidityYManual;
    out["display_limit_note"]=m_dataBuf.size()>=APP_DISPLAY_MAX_POINTS?"Point limit reached; oldest display samples removed; raw/CSV retained":"1200 seconds retained, at most 24000 samples";
    out["plot_x_lower_s"]=ui->customPlot->xAxis->range().lower; out["plot_x_upper_s"]=ui->customPlot->xAxis->range().upper;
    out["plot_temperature_lower"]=ui->customPlot->yAxis->range().lower; out["plot_temperature_upper"]=ui->customPlot->yAxis->range().upper;
    out["plot_humidity_lower"]=ui->customPlot->yAxis2->range().lower; out["plot_humidity_upper"]=ui->customPlot->yAxis2->range().upper;
    out["trigger_item_count"]=m_triggerMarks.size();
    out["selection_pending"]=m_clickCount==1; out["selection_active"]=m_hasAnalysis;
    out["selection_channel"]=m_analysisChannel==APP_SIGNAL_HUMIDITY?"Humidity":"Temperature";
    out["selection"]=m_analysisJson; out["analysis_summary"]=ui->lbl_paramResult->text();
    out["cursor_1_visible"]=m_line1->visible(); out["cursor_2_visible"]=m_line2->visible();
    out["plot_export_title"]=m_plotTitle->text(); out["plot_export_status"]=m_plotStatus->text();
    return out;
}

void MainWindow::app_write_status()
{
    if(m_statusPath.isEmpty()) return;
    QSaveFile file(m_statusPath);
    if(!file.open(QIODevice::WriteOnly)) { ui->statusbar->showMessage("Status output failed: "+file.errorString()); return; }
    file.write(QJsonDocument(app_status_snapshot()).toJson());
    if(!file.commit()) ui->statusbar->showMessage("Status output commit failed: "+file.errorString());
}

QString MainWindow::app_classification() const
{
    QString classification;
    if(m_inputMode=="SERIAL") classification="SERIAL + "+m_source
        +(m_source=="MIXED"?QString(" (T:%1 H:%2)").arg(m_frameTemperature,m_frameHumidity):QString());
    else if(m_inputMode=="REPLAY") classification="REPLAY / FIXTURE";
    else if(m_source=="SIM") classification="SIMULATED";
    else if(m_source=="REAL" ||m_source=="MIXED") classification="REPLAY / FIXTURE ("+m_source+" file)";
    else classification="UNKNOWN file source";
    if(m_inputMode=="REPLAY" && m_source=="SIM") classification+=" | SIMULATED";
    return classification;
}

void MainWindow::app_render_state()
{
    app_fit_analysis_label();
    const QString classification=app_classification();
    const QString trust=m_inputMode=="SERIAL"?"SENSOR ACCURACY NOT VERIFIED":"NOT TESTED ON HARDWARE";
    m_sourceLabel->setText(QString("%1\nMode: %2 | Source: %3\nState: %4 | Recording: %5\nProtocol: "+m_frameProtocol+" | Version: %6\n%7\n%8")
        .arg(classification,m_inputMode,m_source,m_state,m_recorder.app_session_active()?"ON":"OFF",QCoreApplication::applicationVersion(),trust,m_commandState)
        +(m_inputMode=="SERIAL"?"\nConnection: "+m_connectionState:QString())
        +QString("\nRequested T:%1 H:%2\nObserved T:%3 H:%4 | T valid:%5 H valid:%6\n%7").arg(m_requestedTemperature,m_requestedHumidity,m_frameTemperature,m_frameHumidity).arg(m_temperatureValid).arg(m_humidityValid).arg(m_modeState));
    const QString displayState=m_displayPaused?"DISPLAY PAUSED / RECORDING "+QString(m_recorder.app_session_active()?"ON":"OFF"):"DISPLAY LIVE";
    m_sourceLabel->setText(m_sourceLabel->text()+"\n"+displayState);
    // Frozen graphs retain their source classification even if input changes while paused.
    if(!m_displayPaused) m_plotStatus->setText(classification+" | "+m_frameProtocol+" | "+m_state+
        QString("\nMCU fit %1 | %2 | Accuracy unverified")
          .arg(m_frameCalibration>=0?QString::number(m_frameCalibration):m_calibrationConfirmed?QString::number(m_effectiveCalibration):QString("unknown"),
               m_frameTemperature=="SIM"?"SIM (no empirical correction)":m_frameCalibration>0&&!m_calibrationApplied?"Baseline (fit inactive)":m_frameCalibration==0?"Off":"Provisional"));
    ui->btn_stop->setText(m_displayPaused?"Resume Display":"Pause Display"); ui->btn_stop->setEnabled(m_running);
    if(auto button=findChild<QPushButton*>("endSession")) button->setEnabled(m_running);
    if(auto button=findChild<QPushButton*>("reconnectSerial")) button->setEnabled(m_running &&m_inputMode=="SERIAL" &&!m_stopping &&!m_serialIoBusy);
    if(auto banner=findChild<QLabel*>("recoveryBanner")) {
        banner->setVisible(m_inputMode=="SERIAL");
        const auto state=m_recovery.snapshot();
        const QString attempt=m_recovery.attempts()>0?QString(" — attempt %1/12").arg(m_recovery.attempts()):QString();
        banner->setText(m_recovery.state()+" "+m_serialPort+attempt
          +" | Auto reconnect: "+(m_autoRecovery?"ON":"OFF")+" | Restore: "+m_restoreState
          +(m_restoreReason.isEmpty()?QString():" — "+m_restoreReason)
          +(state["reason"].toString().isEmpty()?QString():"\n"+state["reason"].toString())
          +(!m_mcuIdentityError.isEmpty()?" | "+m_mcuIdentityError:QString())
          +(m_recoveryMigrationNotice?"\nRecovery defaults upgraded to ON; legacy OFF intent was not recorded. Review UART Settings.":QString())
          +((m_recovery.state()!="CONNECTED" &&!m_lastSerialError.isEmpty())?" | Last error: "+m_lastSerialError:QString()));
        banner->setToolTip("Operational MCU UID match is not cryptographic authentication. Previous error: "+m_lastSerialError);
    }
    const QString details=QString("Source %1 ms | Segment %2: %3 s | valid_flags %4 | error_flags %5 %6")
        .arg(m_lastTimestamp).arg(m_lastTime.segment_id).arg(m_lastTime.elapsed_s,0,'f',3)
        .arg(m_validFlags).arg(m_errorFlags).arg(m_errorFlags&32?"PWM_NO_SAMPLE":"");
    ui->statusbar->showMessage(m_diagnostic.isEmpty()?details:details+" | "+m_diagnostic);
    if(auto label=findChild<QLabel*>("recordingRootLabel")) {
        label->setText("Recording: "+m_recordDirectory);
        const int height=label->heightForWidth(qMax(1,label->width()));
        if(height>0 &&label->minimumHeight()!=height) label->setMinimumHeight(height);
    }
    if(auto button=findChild<QPushButton*>("openLatestSession")) button->setEnabled(!m_latestSession.isEmpty() &&QDir(m_latestSession).exists());
    if(auto button=findChild<QPushButton*>("exportLatestSession")) button->setEnabled(!m_running &&!m_latestSession.isEmpty());
    if(m_inputMode=="SERIAL") m_pathLabel->setText(QString("SERIAL | %1 | %2 | 8N1 | %3").arg(m_serialPort.isEmpty()?"No device selected":m_serialPort).arg(m_serialBaud).arg(m_serial.isOpen()?"Connected":"Disconnected"));
}

void MainWindow::app_clear_plot_segment()
{
    // QCustomPlot owns/deletes items in removeItem; never delete them again.
    m_triggerMarks.clear(); m_dataBuf.clear(); m_measureMode=false;
    m_plotWindow.app_reset_segment(); m_temperatureYManual=m_humidityYManual=false;
    app_clear_selection("SEGMENT_CHANGED"); m_temperatureRangeInitialized=false;
    if(!m_displayPaused) ui->customPlot->yAxis2->setRange(0,100);
    m_lastTemp=m_lastHumi=std::numeric_limits<double>::quiet_NaN();
    updatePlotRealTime();
}

void MainWindow::slotDraw()
{
    if(m_running &&m_inputMode=="SERIAL" &&!m_serial.isOpen()) { app_reconnect_serial();return; }
    if(m_running) slotStop();
    ++m_connectionEpoch;m_stopping=false;m_gapPending=false;m_lastFrameHostMs=-1;m_segmentPlotOrigin=0;
    m_stopReasonOverride.clear();
    m_connectionHasFrame=false;m_mcuIdentityQueried=m_mcuIdentityCurrent=false;m_mcuIdentityHash.clear();m_mcuIdentityError.clear();
    m_observedFirmware.clear();m_firmwareObservedAt.clear();m_firmwareCurrent=false;
    m_savedConfiguration=QJsonObject::fromVariantMap(app_desktop_open_settings()->value("serial/confirmedConfiguration").toMap());
    m_restoreTarget=m_savedConfiguration;m_restorePending=m_inputMode=="SERIAL" &&m_restoreConfirmedOption &&!m_restoreTarget.isEmpty();
    m_restoreState=m_restorePending?"WAITING_LIVE_READBACK":"IDLE";m_restoreReason.clear();m_lastSerialError.clear();
    m_restoreDeadline=0;m_restoreSetIssued=m_restoreCalIssued=false;
    m_displayPaused=false; m_displayEvents.clear();
    m_connectionState=m_inputMode=="SERIAL"?"CONNECTING":"FILE_INPUT";
    if(m_inputMode=="SERIAL" && m_serialPort.isEmpty()) {
        m_state="ERROR"; m_connectionState="DISCONNECTED";
        m_diagnostic="No serial device selected. Open UART Settings; no port was opened.";
        app_render_state(); app_write_status(); return;
    }
    if(m_everStarted) ++m_restartCount;
    m_everStarted=true;
    app_clear_plot_segment(); m_timeline.app_th3_reset_segment("sampling_start");
    app_session_options_t options;
    options.output_directory=m_recordDirectory; options.input_mode=m_inputMode;
    options.input_path=m_inputMode=="SERIAL"?QString("%1|%2|8N1|NO_FLOW").arg(m_serialPort).arg(m_serialBaud):m_input.app_file_path();
    options.classification=m_inputMode=="SERIAL"?"SERIAL":(m_inputMode=="REPLAY" ? "REPLAY_FIXTURE" : "SIMULATED");
    options.git_head=QString(APP_GIT_HEAD);
    options.software_version=QCoreApplication::applicationVersion();
#ifdef QT_DEBUG
    const QString buildConfiguration="Debug";
#else
    const QString buildConfiguration="Release";
#endif
    options.qt_build_identity=QString("Qt %1 | MinGW64 | %2 | built %3 %4").arg(qVersion(),buildConfiguration,__DATE__,__TIME__);
    if(!m_recorder.app_start_session(options)) { m_state="ERROR"; m_connectionState="ERROR"; m_diagnostic=m_recorder.app_session_error(); app_render_state(); app_write_status(); return; }
    if(m_inputMode=="SERIAL") {
        app_serial_device_t selected;selected.port=m_serialPort;
        for(const auto &d:app_desktop_serial_devices()) if(d.port==m_serialPort) selected=d;
        m_trustedDevice=selected;m_recovery.configure(m_autoRecovery);
        m_recovery.start(selected,m_hostClock.elapsed());
    }
    m_serialIoBusy=true;
    const bool serialOpened=m_inputMode!="SERIAL" ||m_serial.app_serial_open(m_serialPort,m_serialBaud);
    m_serialIoBusy=false;
    if(!serialOpened) {
        m_recovery.error(m_serial.error(),m_hostClock.elapsed());
        if(!app_record_connection_event("SERIAL_OPEN_FAILED",{{"qt_error_code",int(m_serial.error())},{"qt_error_text",m_serial.errorString()}})) return;
        m_running=true;m_source="UNKNOWN";m_lastTime=app_th3_time_t();m_lastTimestamp=0;
        m_frameTemperature=m_frameHumidity="UNKNOWN";m_temperatureValid=m_humidityValid=false;
        app_invalidate_serial_confirmation();app_begin_configuration_recovery();
        m_state=m_recovery.state();m_connectionState=m_state;
        m_lastSerialError="SERIAL_OPEN_FAILED: "+m_serial.errorString();m_diagnostic=m_lastSerialError;
        m_serialTimer->start();app_remember_session();app_persist_mode_state();
        app_render_state();app_write_status();return;
    }
    if(m_inputMode=="SERIAL") {
        const auto s=app_desktop_open_settings(); s->setValue("serial/port",m_serialPort); s->setValue("serial/lastSuccessfulPort",m_serialPort); s->setValue("serial/baud",m_serialBaud);
        for(const auto &d:app_desktop_serial_devices()) if(d.port==m_serialPort) s->setValue("serial/serialNumber",d.serial_number);
        s->sync(); m_connectionState="WAITING_FOR_FIRST_FRAME";
    }
    // Retain adapter offset/pending on Stop/Draw: new session does not replay
    // bytes previously consumed from this growing file.
    m_source="UNKNOWN"; m_commandState="IDLE";
    m_modeAwaiting=false; m_modeReadback=false; m_modeConfirmed=false; m_modeState="EFFECTIVE MODE UNKNOWN";
    m_sourceChoicesEdited=false;
    m_temperatureEvidence.reset();
    m_frameTemperature=m_frameHumidity="UNKNOWN"; m_temperatureValid=m_humidityValid=false;
    m_lastTime=app_th3_time_t(); m_lastTimestamp=0; m_validFlags=m_errorFlags=0;
    m_temperatureEvidence.reset();
    m_running=true; m_state="WAITING"; m_diagnostic.clear();
    app_remember_session();
    if(m_inputMode!="SERIAL") m_fileTimer->start(50);
    else {
        m_identityQueriesPending=true;m_identityQueryAttempts=0;
        m_serialTimer->start();app_record_connection_event("SERIAL_OPENED",{{"mode_confirmed",false},{"calibration_confirmed",false}});
    }
    ui->lbl_value->setText("Temperature NA deg C | Humidity NA %RH");
    ui->lbl_paramResult->setText("TH3 sampling and raw/CSV recording started.");
    app_render_state(); app_write_status();
}

void MainWindow::slotReadFileData()
{
    if(!m_running) return;
    const auto before=m_parsedCount;
    const auto poll=m_input.app_file_poll();
    if(poll.reset) m_timeline.app_th3_reset_segment(poll.reset_reason);
    if(!poll.available) { m_state=poll.error_text.isEmpty()?"WAITING":"ERROR"; m_diagnostic=poll.error_text.isEmpty()?"Waiting for input file":poll.error_text; app_render_state(); return; }
    app_consume_records(poll.records);
    if(!poll.error_text.isEmpty()) { m_state="ERROR"; m_diagnostic=poll.error_text; }
    app_render_state(); if(m_parsedCount!=before) updatePlotRealTime();
}

void MainWindow::app_consume_records(const QList<app_line_record_t> &records)
{
    for(const auto &record : records) {
        auto frame=app_th3_parse_line(record.bytes);
        if(record.overflow) { frame.parse_status=APP_TH3_MALFORMED; frame.diagnostic="LINE_TOO_LONG"; }
        if(!record.bytes.endsWith('\n')) { frame.parse_status=APP_TH3_DIAGNOSTIC; frame.diagnostic=m_inputMode=="SERIAL"?"INCOMPLETE_AT_DISCONNECT":"INCOMPLETE_AT_END_SESSION"; }
        app_th3_time_t time=m_lastTime;
        if(frame.parse_status==APP_TH3_PARSED) {
            if(m_inputMode=="SERIAL" &&m_lastTime.segment_id) {
                const quint32 delta=frame.timestamp_ms-m_lastTimestamp;
                if(m_gapPending ||(delta>APP_SERIAL_SOURCE_GAP_MS &&delta<0x80000000U)) {
                    m_timeline.app_th3_reset_segment(m_gapPending?"SERIAL_RECOVERY_OR_STALE":"MISSING_SOURCE_WINDOWS");
                    m_gapPending=false;
                }
            }
            time=m_timeline.app_th3_append_frame(frame);
        }
        m_temperatureEvidence.observe(record.bytes);
        m_temperatureEvidence.attach(frame);
        const auto received=QDateTime::currentDateTimeUtc();
        m_frameReceivedAt=received.toString(Qt::ISODateWithMs);
        if(!m_recorder.app_record_line(frame,time,received)) {
            m_fileTimer->stop();m_running=false;app_cancel_recovery_work("Recording data failed");app_close_serial();m_state="ERROR";m_diagnostic=m_recorder.app_session_error();app_render_state();app_write_status();return;
        }
        const QByteArray response=record.bytes.trimmed();
        const QStringList responseFields=QString::fromLatin1(response).split(',');
        if(m_inputMode=="SERIAL") {
            static const QRegularExpression bootPattern(QStringLiteral("^BOOT OK (v[0-9]+\\.[0-9]+\\.[0-9]+)(?: |$)"));
            const auto boot=bootPattern.match(QString::fromLatin1(response));
            if(boot.hasMatch()) {
                if(!m_dataBuf.isEmpty()) app_begin_configuration_recovery();
                app_invalidate_serial_confirmation();m_timeline.app_th3_reset_segment("MCU_BOOT");m_gapPending=false;
                // BOOT records identity immediately; the current connection's
                // restoration gate still requires its own STATUS readback.
                m_observedFirmware=boot.captured(1);m_firmwareObservedAt=received.toString(Qt::ISODateWithMs);m_firmwareCurrent=false;
                if(!app_record_connection_event("MCU_BOOT_OBSERVED",{{"firmware",m_observedFirmware}})) return;
            }
            if(responseFields.size()==7 &&responseFields[0]=="RSP" &&responseFields[1]=="STATUS" &&responseFields[2]=="1"
               &&m_pendingCommand=="STATUS?" &&QRegularExpression("^v[0-9]+\\.[0-9]+\\.[0-9]+$").match(responseFields[3]).hasMatch()) {
                m_observedFirmware=responseFields[3];m_firmwareObservedAt=received.toString(Qt::ISODateWithMs);m_firmwareCurrent=true;
            }
        }
        if(m_inputMode=="SERIAL" &&m_pendingCommand=="ID?" &&(response.startsWith("RSP,ID,") ||response.startsWith("ERR,"))) {
            m_mcuIdentityQueried=true;m_mcuIdentityCurrent=false;m_mcuIdentityHash.clear();
            if(responseFields.size()==7 &&responseFields[0]=="RSP" &&responseFields[1]=="ID" &&responseFields[2]=="1" &&responseFields[3]=="STM32F1")
                m_mcuIdentityHash=app_mcu_identity_hash(responseFields[4],responseFields[5],responseFields[6]);
            if(!m_mcuIdentityHash.isEmpty()) {
                if(m_boundMcuIdentityHash.isEmpty() &&m_restoreTarget.isEmpty() &&app_desktop_identity_kind(m_trustedDevice)!="UNVERIFIED") {
                    m_boundMcuIdentityHash=m_mcuIdentityHash;const auto settings=app_desktop_open_settings();
                    settings->setValue("serial/confirmedMcuIdentityHash",m_boundMcuIdentityHash);settings->sync();
                }
                m_mcuIdentityCurrent=m_mcuIdentityHash==m_boundMcuIdentityHash;
            }
            m_mcuIdentityError=m_mcuIdentityCurrent?QString():"MCU identity unsupported, invalid or differs from the confirmed board; automatic configuration changes blocked";
            ++m_commandRx;m_pendingCommand.clear();m_commandTimer->stop();
            if(!app_record_connection_event("MCU_IDENTITY_READBACK",{{"verified",m_mcuIdentityCurrent},{"identity_sha256",m_mcuIdentityHash},{"reason",m_mcuIdentityError},
               {"dbg_device_id",responseFields.size()==7?QJsonValue(responseFields[6]):QJsonValue()},
               {"dbg_device_id_available",!m_mcuIdentityHash.isEmpty() &&responseFields[6]!="0"}})) return;
        }
        if(m_inputMode=="SERIAL" &&responseFields.size()==3 &&responseFields[0]=="ACK" &&responseFields[1]=="CAL"
           &&m_pendingCommand=="CAL,"+responseFields[2]) {
            ++m_commandRx;m_pendingCommand.clear();m_commandTimer->stop();
            m_calibrationState="Fit accepted; awaiting effective readback and frame";
            const auto epoch=m_connectionEpoch;
            QTimer::singleShot(1200,this,[this,epoch]() { if(epoch==m_connectionEpoch) app_send_command("CAL?"); });
        }
        if(m_inputMode=="SERIAL" &&responseFields.size()==5 &&responseFields[0]=="RSP" &&responseFields[1]=="CAL"
           &&responseFields[2]=="1" &&m_pendingCommand=="CAL?") {
            bool ok=false;int effective=responseFields[4].toInt(&ok);
            if(ok &&effective>=0 &&effective<=3) {
                ++m_commandRx;m_effectiveCalibration=effective;m_pendingCommand.clear();m_commandTimer->stop();
                if(m_requestedCalibration<0) { bool requestedOk=false;const int requested=responseFields[3].toInt(&requestedOk);
                    if(requestedOk &&requested>=0 &&requested<=3) m_requestedCalibration=requested; }
                m_commandState=QString::fromLatin1(response);
                m_calibrationState="Fit readback received; awaiting matching MCU evidence";
                if(m_frameTemperature=="SIM")
                    if(!app_append_display_event("SIM_CAL_CONFIGURATION_READBACK",{{"effective_profile",effective},{"applied",false},{"time_basis","Host CAL readback; no per-window physical sidecar in SIM"}},false)) return;
            }
        }
        if(m_inputMode=="SERIAL" && responseFields.size()==5 && responseFields[0]=="ACK" && responseFields[1]=="SET"
           &&m_modeAwaiting &&m_pendingCommand.startsWith("SET,")) {
            bool generationOk=false; const quint32 generation=responseFields[2].toUInt(&generationOk);
            if(generationOk && responseFields[3]==m_requestedTemperature && responseFields[4]==m_requestedHumidity) {
                ++m_commandRx; m_expectedGeneration=generation; m_pendingCommand.clear(); m_commandTimer->stop();
                const auto epoch=m_connectionEpoch;
                QTimer::singleShot(0,this,[this,epoch]() { if(epoch==m_connectionEpoch) app_send_command("MODE?"); });
            }
        }
        if(m_inputMode=="SERIAL" && responseFields.size()==9 && responseFields[0]=="RSP" && responseFields[1]=="MODE" && responseFields[2]=="1"
           &&m_pendingCommand=="MODE?") {
            bool generationOk=false; const quint32 generation=responseFields[6].toUInt(&generationOk);
            if(generationOk && (responseFields[7]=="REAL" ||responseFields[7]=="SIM") && (responseFields[8]=="REAL" ||responseFields[8]=="SIM")) {
                ++m_commandRx; m_readbackGeneration=generation; m_readbackTemperature=responseFields[7]; m_readbackHumidity=responseFields[8]; m_modeReadback=true;
                m_commandState=QString::fromLatin1(response);
                if(!m_modeAwaiting && (responseFields[4]=="REAL" ||responseFields[4]=="SIM") && (responseFields[5]=="REAL" ||responseFields[5]=="SIM")) {
                    m_requestedTemperature=responseFields[4]; m_requestedHumidity=responseFields[5];
                }
                m_pendingCommand.clear();
                if(!m_modeAwaiting) m_commandTimer->stop();
            }
        }
        if(m_inputMode=="SERIAL" && !m_pendingCommand.isEmpty()
           && ((m_pendingCommand=="PING" && response=="ACK,PING")
               || (m_pendingCommand=="STATUS?" && response.startsWith("RSP,STATUS,1,")) || response.startsWith("ERR,"))) {
            ++m_commandRx; m_commandState=QString::fromLatin1(response); m_pendingCommand.clear(); m_commandTimer->stop();
            if(response.startsWith("ERR,") && m_modeAwaiting) {
                m_modeAwaiting=false; m_modeConfirmed=false; m_modeState="MODE_CHANGE_UNCONFIRMED: "+QString::fromLatin1(response);
                if(!app_persist_mode_state()) return;
            }
        }
        if(frame.parse_status==APP_TH3_PARSED) app_accept_frame(frame,time);
        else if(frame.parse_status==APP_TH3_MALFORMED) { ++m_malformedCount; m_state="TRANSIENT"; m_diagnostic="Malformed input retained; waiting for next valid TH3: "+frame.diagnostic; }
        else if(frame.parse_status==APP_TH3_LEGACY &&record.bytes.startsWith("TH2,")) {
            ++m_diagnosticCount;
            m_diagnostic="UNSUPPORTED_LEGACY_PROTOCOL: TH3 required; legacy bytes are recorded. Existing v0.6.0 firmware begins TH3 after explicit Apply Sources.";
            if(m_inputMode=="SERIAL" &&m_source=="UNKNOWN") m_connectionState="LEGACY INPUT / TH3 REQUIRED";
        }
        else ++m_diagnosticCount;
    }
}

void MainWindow::app_accept_frame(const app_th3_frame_t &frame,const app_th3_time_t &time)
{
    ++m_parsedCount;
    const bool previousCalibrationConfirmed=m_calibrationConfirmed;
    if(time.new_segment) {
        if(m_inputMode=="SERIAL" &&!m_dataBuf.isEmpty()) {
            if(time.reason!="SOURCE_CHANGE" &&!m_restorePending) app_begin_configuration_recovery();
            if(time.reason!="SOURCE_CHANGE" ||!m_modeAwaiting) app_invalidate_serial_confirmation();
            const quint32 sourceDelta=frame.timestamp_ms-m_lastTimestamp;
            // Prefer the real MCU delta when its clock ordering is known. A
            // delayed-but-buffered read must not count receipt latency twice.
            const double gap=sourceDelta>0 &&sourceDelta<0x80000000U?sourceDelta/1000.0:
                m_lastFrameHostMs<0?1.0:qMax(1.0,(m_hostClock.elapsed()-m_lastFrameHostMs)/1000.0);
            m_segmentPlotOrigin=m_dataBuf.last().time+gap;
            app_clear_selection("SEGMENT_CHANGED");m_lastTemp=m_lastHumi=std::numeric_limits<double>::quiet_NaN();
        } else { m_segmentPlotOrigin=0;app_clear_plot_segment(); }
    }
    m_lastTime=time; m_lastTimestamp=frame.timestamp_ms; m_source=app_th3_source_text(frame.source);
    if(m_inputMode=="SERIAL") {
        m_lastFrameHostMs=m_hostClock.elapsed();m_connectionHasFrame=true;
        if(!m_serialIoBusy) m_recovery.frame(m_lastFrameHostMs); // Draining a lost handle must not cancel a scheduled retry.
    }
    m_frameProtocol=frame.protocol; m_frameTemperature=app_th3_source_text(frame.temperature_source); m_frameHumidity=app_th3_source_text(frame.humidity_source);
    m_frameGeneration=frame.mode_generation; m_temperatureValid=frame.temperature_valid; m_humidityValid=frame.humidity_valid;
    if(m_inputMode=="SERIAL" && m_modeReadback) {
        const bool observed=m_readbackGeneration==frame.mode_generation && m_readbackTemperature==m_frameTemperature && m_readbackHumidity==m_frameHumidity;
        const bool requested=!m_modeAwaiting || (m_expectedGeneration==frame.mode_generation && m_requestedTemperature==m_frameTemperature && m_requestedHumidity==m_frameHumidity);
        if(observed && requested) {
            const bool changed=!m_modeConfirmed ||m_modeAwaiting;
            m_modeConfirmed=true; m_modeAwaiting=false; m_modeState=QString("Effective T:%1 H:%2 | generation %3").arg(m_frameTemperature,m_frameHumidity).arg(frame.mode_generation);
            if(changed && !m_sourceChoicesEdited) {
                for(const auto &pair:{qMakePair(QString("temperatureSourceChoice"),m_frameTemperature),qMakePair(QString("humiditySourceChoice"),m_frameHumidity)}) {
                    auto choice=findChild<QComboBox*>(pair.first); const QSignalBlocker blocked(choice); choice->setCurrentText(pair.second);
                }
            }
            if(m_pendingCommand.isEmpty()) m_commandTimer->stop();
            if(changed && !app_persist_mode_state()) return;
        } else if(m_modeAwaiting && frame.mode_generation==m_expectedGeneration && m_expectedGeneration!=0) {
            // Readback may have preceded the safe boundary; query again without re-sending SET.
            m_modeReadback=false; QTimer::singleShot(0,this,[this]() { app_send_command("MODE?"); });
        }
    }
    m_validFlags=frame.valid_flags; m_errorFlags=frame.error_flags;
    const int previousCalibration=m_frameCalibration;
    m_frameCalibration=frame.correction_profile;
    m_calibrationApplied=frame.calibration_id>0 &&app_th3_temperature_usable(frame);
    m_calibrationConfirmed=m_requestedCalibration>=0 &&m_effectiveCalibration==m_requestedCalibration
        &&(m_frameCalibration==m_requestedCalibration ||(frame.temperature_source==APP_TH3_SOURCE_SIM &&frame.calibration_id==0));
    if(m_calibrationConfirmed &&frame.temperature_source==APP_TH3_SOURCE_SIM)
        m_calibrationState=QString("Fit %1 configured | SIM: no empirical correction").arg(m_effectiveCalibration);
    else if(m_calibrationConfirmed) m_calibrationState=QString("Fit %1 effective | PROVISIONAL").arg(m_frameCalibration);
    else if(m_frameCalibration>=0) m_calibrationState=QString("Observed MCU fit %1 | PROVISIONAL").arg(m_frameCalibration);
    else m_calibrationState="Pre-correction evidence unavailable";
    const int displayedProfile=m_frameCalibration>=0?m_frameCalibration:m_calibrationConfirmed?m_effectiveCalibration:-1;
    if(!m_calibrationChoiceEdited &&displayedProfile>=0 &&(m_calibrationConfirmed ||m_requestedCalibration<0)) {
        auto choice=findChild<QComboBox*>("calibrationChoice");const QSignalBlocker blocked(choice);choice->setCurrentIndex(displayedProfile);
    }
    const bool correctionOff=(m_frameCalibration==0) ||(m_frameCalibration<0 &&m_effectiveCalibration==0);
    auto correctedCheck=findChild<QCheckBox*>("showCorrectedTemperature");
    if(correctedCheck->isEnabled()==correctionOff) {
        const QSignalBlocker blocked(correctedCheck);correctedCheck->setEnabled(!correctionOff);
        correctedCheck->setChecked(correctionOff?false:m_correctedVisiblePreference);
        ui->customPlot->graph(2)->setVisible(correctedCheck->isChecked());
    }
    if(m_inputMode=="SERIAL" &&(previousCalibrationConfirmed!=m_calibrationConfirmed ||previousCalibration!=m_frameCalibration))
        if(!app_persist_mode_state()) return;
    // The validated TH3 per-channel flags preserve partial validity independently.
    const bool validT=app_th3_temperature_usable(frame);
    const bool validH=app_th3_humidity_usable(frame);
    m_temperatureValid=validT; m_humidityValid=validH;
    if(previousCalibration>=0 &&m_frameCalibration>=0 &&previousCalibration!=m_frameCalibration)
        if(!app_append_display_event("CORRECTION_PROFILE_CHANGED")) return;
    const double temperature=validT?frame.temperature_c.value:std::numeric_limits<double>::quiet_NaN();
    const double humidity=validH?frame.humidity_rh.value:std::numeric_limits<double>::quiet_NaN();
    m_state=frame.valid&&validT&&validH?"VALID":(validT||validH?"PARTIAL_VALID":"INVALID"); m_diagnostic.clear();
    if(m_inputMode=="SERIAL") m_connectionState=m_recovery.state()+" / "+m_state;
    ui->lbl_value->setText(QString("Temperature %1 deg C | Humidity %2 %RH")
        .arg(validT?QString::number(temperature,'f',2):"NA",validH?QString::number(humidity,'f',2):"NA"));
    const double rawTemperature=validT &&frame.physical_temperature_c.present?frame.physical_temperature_c.value:
        (validT &&frame.calibration_id==0?temperature:std::numeric_limits<double>::quiet_NaN());
    // A configured fit outside its support returns the baseline. Keep that MCU
    // output observable; label its inactive correction rather than inventing one.
    const double correctedTemperature=validT &&(frame.calibration_id>0 ||frame.correction_profile>0)?temperature:std::numeric_limits<double>::quiet_NaN();
    ui->lbl_value->setWordWrap(true);
    ui->lbl_value->setText(QString("Temperature raw %1 / corrected %2 deg C | Humidity %3 %RH")
        .arg(std::isfinite(rawTemperature)?QString::number(rawTemperature,'f',2):"NA",
             std::isfinite(correctedTemperature)?QString::number(correctedTemperature,'f',2):"Off / NA",
             validH?QString::number(humidity,'f',2):"NA"));
    const double plotTime=m_inputMode=="SERIAL"?m_segmentPlotOrigin+time.elapsed_s:time.elapsed_s;
    m_dataBuf.append({plotTime,temperature,humidity,time.segment_id,time.unwrapped_timestamp_ms,rawTemperature,correctedTemperature,m_frameCalibration,
                      time.elapsed_s,m_frameTemperature,m_frameHumidity,m_frameReceivedAt,frame.physical_temperature_c.present,int(frame.calibration_id),frame.timestamp_ms,displayedProfile,m_inputMode});
    if(time.new_segment &&m_inputMode=="SERIAL")
        if(!app_append_display_event("SEGMENT_STARTED",{{"reason",time.reason},{"plot_gap_has_no_samples",true}})) return;
    const bool tempHit=m_analysisChannel==APP_SIGNAL_TEMPERATURE &&checkEdgeTrigger(m_lastTemp,temperature);
    const bool humiHit=m_analysisChannel==APP_SIGNAL_HUMIDITY &&checkEdgeTrigger(m_lastHumi,humidity);
    for(int channel=0;channel<2;++channel) {
        if(!(channel==0?tempHit:humiHit)) continue;
        m_triggerMarks.append({plotTime,channel==0?temperature:humidity,channel});
    }
    while(m_dataBuf.size()>APP_DISPLAY_MAX_POINTS || (!m_dataBuf.isEmpty() && m_dataBuf.first().time<plotTime-APP_DISPLAY_HORIZON_S))
        m_dataBuf.removeFirst();
    int breaks=0;
    for(int n=1;n<m_dataBuf.size();++n) if(m_dataBuf[n].segment_id!=m_dataBuf[n-1].segment_id) ++breaks;
    while(m_dataBuf.size()+breaks>APP_DISPLAY_MAX_POINTS) {
        if(m_dataBuf[0].segment_id!=m_dataBuf[1].segment_id) --breaks;
        m_dataBuf.removeFirst();
    }
    m_maxUiPoints=qMax(m_maxUiPoints,m_dataBuf.size());
    for(int index=0;index<m_triggerMarks.size();) {
        if(m_triggerMarks[index].t < m_dataBuf.first().time) m_triggerMarks.removeAt(index);
        else ++index;
    }
    // Up to two channel marks per retained point, including repeated timestamps.
    while(m_triggerMarks.size()>APP_DISPLAY_MAX_TRIGGER_MARKS) m_triggerMarks.removeFirst();
    if((m_clickCount||m_hasAnalysis) && (m_selectionSegment!=time.segment_id || m_x1<m_dataBuf.first().time || (m_hasAnalysis&&m_x2<m_dataBuf.first().time)))
        app_clear_selection("CURSOR_EXPIRED");
    m_lastTemp=temperature; m_lastHumi=humidity;
    app_remember_confirmed_configuration();
    if(m_displayPaused) app_validate_inspection();
    m_plotWindow.app_range_at(plotTime); // Plot coordinate is distinct from exact per-segment source time.
}

bool MainWindow::app_append_display_event(const QString &type,const QJsonObject &details,bool allow_source_time)
{
    const auto now=QDateTime::currentDateTimeUtc();
    const bool known=allow_source_time &&m_lastTime.segment_id!=0 &&m_source!="UNKNOWN"
        &&(m_inputMode!="SERIAL" ||(m_connectionHasFrame &&m_serial.isOpen()));
    QJsonObject event;
    event["event_type"]=type; event["host_event_timestamp"]=now.toString(Qt::ISODateWithMs);
    event["host_label"]=now.toLocalTime().toString("HH:mm:ss"); event["source_time_known"]=known;
    event["source_timestamp_ms"]=known?QJsonValue(double(m_lastTimestamp)):QJsonValue(QJsonValue::Null);
    event["unwrapped_source_timestamp_ms"]=known?QJsonValue(double(m_lastTime.unwrapped_timestamp_ms)):QJsonValue(QJsonValue::Null);
    event["source_elapsed_s"]=known?QJsonValue(m_lastTime.elapsed_s):QJsonValue(QJsonValue::Null);
    event["plot_elapsed_s"]=known?QJsonValue(m_inputMode=="SERIAL"?m_segmentPlotOrigin+m_lastTime.elapsed_s:m_lastTime.elapsed_s):QJsonValue(QJsonValue::Null);
    event["segment_id"]=known?QJsonValue(double(m_lastTime.segment_id)):QJsonValue(QJsonValue::Null);
    event["generation"]=known?QJsonValue(double(m_frameGeneration)):QJsonValue(QJsonValue::Null);
    event["correction_profile"]=m_frameCalibration>=0?QJsonValue(m_frameCalibration):QJsonValue();
    event["session_id"]=QFileInfo(m_recorder.app_session_directory()).fileName();
    event["input_mode"]=m_inputMode;event["classification"]=app_classification();
    event["details"]=details;
    event["event_index"]=m_recorder.app_session_summary()["event_count"].toDouble()+1;
    if(!m_recorder.app_record_display_event(event)) {
        m_diagnostic=m_recorder.app_session_error(); m_state="ERROR";
        m_fileTimer->stop();m_running=false;app_cancel_recovery_work("Recording event failed");app_close_serial();
        app_render_state(); app_write_status(); return false;
    }
    m_displayEvents.append(event);
    // Only the visible annotation cache is bounded. The append-only sidecar is complete.
    while(m_displayEvents.size()>APP_DISPLAY_MAX_TRIGGER_MARKS) m_displayEvents.removeFirst();
    if(m_inspector) m_inspector->update_events(m_displayEvents);
    return true;
}

void MainWindow::slotPauseDisplay()
{
    if(!m_running ||!app_append_display_event(m_displayPaused?"RESUME_DISPLAY":"PAUSE_DISPLAY")) return;
    m_displayPaused=!m_displayPaused;
    if(m_displayPaused) {
        m_measureMode=false;
        app_render_plot_items();
        m_plotStatus->setText(m_plotStatus->text()+" | DISPLAY PAUSED / RECORDING ON");
        ui->customPlot->replot();
    } else updatePlotRealTime();
    app_render_state(); app_write_status();
}

void MainWindow::app_render_plot_items()
{
    for(auto item:m_renderedMarks) ui->customPlot->removeItem(item);
    m_renderedMarks.clear();
    m_visibleEvents.clear();
    const auto range=ui->customPlot->xAxis->range();
    for(const auto &mark:m_triggerMarks) {
        if(mark.t<range.lower ||mark.t>range.upper) continue;
        auto text=new QCPItemText(ui->customPlot);
        text->setText(ui->radio_rise->isChecked()?"+":"-"); text->setFont(QFont("Arial",12));
        if(mark.channel==1) text->position->setAxes(ui->customPlot->xAxis,ui->customPlot->yAxis2);
        text->position->setCoords(mark.t,mark.val); m_renderedMarks.append(text);
    }
    double lastFitLabelPixel=-1e30,lastEventLinePixel=-1e30;
    for(const auto &event:m_displayEvents) {
        const auto kind=event["event_type"].toString();
        if(kind!="CORRECTION_PROFILE_CHANGED" &&kind!="PAUSE_DISPLAY" &&kind!="RESUME_DISPLAY"
           &&kind!="SEGMENT_STARTED" &&kind!="SERIAL_DISCONNECTED" &&kind!="SERIAL_REOPENED" &&kind!="CONFIGURATION_RESTORED") continue;
        const double x=event.contains("plot_elapsed_s")?event["plot_elapsed_s"].toDouble():event["source_elapsed_s"].toDouble();
        if(!event["source_time_known"].toBool() ||(m_inputMode!="SERIAL" &&quint64(event["segment_id"].toDouble())!=m_displaySegment)
           ||x<range.lower ||x>range.upper) continue;
        m_visibleEvents.append(event);
        const double pixel=ui->customPlot->xAxis->coordToPixel(x);
        if(std::abs(pixel-lastEventLinePixel)<5) continue; // Cluster geometry; every event remains in the Inspector/JSONL.
        lastEventLinePixel=pixel;
        auto line=new QCPItemLine(ui->customPlot);
        line->start->setTypeY(QCPItemPosition::ptAxisRectRatio); line->end->setTypeY(QCPItemPosition::ptAxisRectRatio);
        line->start->setCoords(x,0); line->end->setCoords(x,1); line->setPen(QPen(QColor(130,130,130,150),1,Qt::DashLine));
        m_renderedMarks.append(line);
        const bool fitEvent=event["event_type"]=="CORRECTION_PROFILE_CHANGED";
        if(pixel-lastFitLabelPixel<120) continue;
        lastFitLabelPixel=pixel;
        auto label=new QCPItemText(ui->customPlot); label->setFont(QFont("Arial",8)); label->setColor(QColor(95,95,95));
        const bool paused=event["event_type"]=="PAUSE_DISPLAY";
        const QString eventType=event["event_type"].toString();
        const QString prefix=fitEvent?QString("Fit %1 ").arg(event["correction_profile"].toInt()):
            eventType=="PAUSE_DISPLAY"?QString("Paused "):eventType=="RESUME_DISPLAY"?QString("Resumed "):QString("Segment ");
        label->setText(prefix+event["host_label"].toString());
        label->position->setTypeY(QCPItemPosition::ptAxisRectRatio); label->position->setCoords(x,paused?0.02:0.98);
        label->setPositionAlignment(paused?(Qt::AlignLeft|Qt::AlignTop):(Qt::AlignRight|Qt::AlignBottom));
        m_renderedMarks.append(label);
    }
}

void MainWindow::slotStop()
{
    if(m_stopping) return;
    if(!m_running &&!m_recorder.app_session_active()) return;
    m_stopping=true;++m_connectionEpoch;m_serialTimer->stop();m_recovery.stop();m_identityQueriesPending=false;
    if(m_restorePending) { m_restorePending=false;m_restoreState="CANCELLED_SESSION_ENDED";m_restoreReason="End Session cancelled configuration recovery"; }
    bool failed=m_state=="ERROR";
    QString failure=m_diagnostic;
    if(m_running && m_inputMode=="SERIAL") {
        QElapsedTimer drain; drain.start();
        while(m_serial.bytesAvailable()>0 && drain.elapsed()<1000) app_consume_records(m_serial.app_serial_read().records);
        app_consume_records(m_serial.app_serial_finish().records);
    } else if(m_running) {
        // Drain the existing backlog, never chase a still-growing producer forever.
        // An unusually large backlog exceeding five seconds is an explicit error.
        const qint64 target=QFileInfo(m_input.app_file_path()).size();
        QElapsedTimer timer; timer.start();
        qint64 previous;
        do { previous=m_input.app_file_offset(); slotReadFileData(); }
        while(m_running && m_input.app_file_offset()<target && m_input.app_file_offset()>previous && timer.elapsed()<5000);
        if(m_running && m_input.app_file_offset()<target) { m_state="ERROR"; m_diagnostic="Stop left unconsumed input bytes; original file retained"; }
        if(m_recorder.app_session_active()) app_consume_records(m_input.app_file_finish());
    }
    if(m_state=="ERROR") { failed=true; failure=m_diagnostic; }
    m_fileTimer->stop(); m_running=false;
    app_close_serial();m_firmwareCurrent=false;
    m_connectionState=failed?"ERROR":"DISCONNECTED";
    if(m_commandTimer) m_commandTimer->stop();
    if(!m_pendingCommand.isEmpty()) { m_commandState="DISCONNECTED_UNCONFIRMED"; m_pendingCommand.clear(); }
    if(m_modeAwaiting) { m_modeState="MODE_CHANGE_UNCONFIRMED"; m_modeAwaiting=false; m_modeConfirmed=false; app_persist_mode_state(); }
    if(failed) { m_state="ERROR"; m_diagnostic=failure; }
    const bool finalized=m_recorder.app_stop_session(failed?(m_stopReasonOverride.isEmpty()?"transport_or_parse_error":m_stopReasonOverride):"end_session");
    if(!finalized) { m_state="ERROR"; m_diagnostic=m_recorder.app_session_error(); }
    else if(m_state!="ERROR") m_state="STOPPED";
    if(finalized &&!failed) app_remember_session();
    if(m_displayPaused) m_plotStatus->setText(m_plotStatus->text().replace("RECORDING ON","RECORDING OFF"));
    app_render_state(); app_write_status();
    m_stopping=false;
}
void MainWindow::app_finish_session() { slotStop(); }
void MainWindow::app_close_for_automation()
{
    // Only explicit CLI test timeout/stop or owned harness teardown calls this.
    // Button, menu and window X always take the default-No confirmation path.
    m_automationClose=true;close();m_automationClose=false;
}
void MainWindow::closeEvent(QCloseEvent *event)
{
    if(!m_automationClose) {
        QMessageBox confirm(QMessageBox::Question,"Exit TempHumi Monitor?",
          "Exit TempHumi Monitor?\nThe current acquisition session will end and its records will be completed.",QMessageBox::NoButton,this);
        confirm.setObjectName("exitConfirmation");
        auto yes=confirm.addButton("Yes, Exit",QMessageBox::YesRole);
        auto no=confirm.addButton("No, Continue",QMessageBox::RejectRole);
        confirm.setDefaultButton(no);confirm.setEscapeButton(no);confirm.exec();
        if(confirm.clickedButton()!=yes) { event->ignore();return; }
    }
    app_finish_session();if(m_inspector) m_inspector->hide();if(m_userGuide) m_userGuide->hide();QMainWindow::closeEvent(event);
}
void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    QTimer::singleShot(0,this,&MainWindow::app_fit_analysis_label);
}
void MainWindow::slotExit() { close(); }
void MainWindow::slotConfig() {
    SettingsDialog dialog(this,m_serialPort,m_serialBaud,m_autoRecovery,m_restoreConfirmedOption);
    if(dialog.exec()==QDialog::Accepted) {
        const auto config=dialog.getSerialConfig();
        if(!(m_running &&m_inputMode=="SERIAL" &&config.portName==m_serialPort &&config.baudRate==m_serialBaud))
            app_configure_serial(config.portName,config.baudRate,m_recordDirectory);
        app_set_recovery_options(config.automaticRecovery,config.restoreConfirmed);
        const auto s=app_desktop_open_settings(); s->setValue("serial/port",config.portName); s->setValue("serial/baud",config.baudRate); s->sync();
    }
}
void MainWindow::slotTrigLevelChanged(double value)
{
    m_trigLevel=value; m_trigLine->point1->setCoords(0,value); m_trigLine->point2->setCoords(100,value); ui->customPlot->replot();
}
bool MainWindow::checkEdgeTrigger(double previous,double current)
{
    if(!std::isfinite(previous)||!std::isfinite(current)) return false;
    return ui->radio_rise->isChecked() ? previous<m_trigLevel&&current>=m_trigLevel : previous>m_trigLevel&&current<=m_trigLevel;
}
void MainWindow::updatePlotRealTime()
{
    if(m_displayPaused) return;
    m_visibleSamples=m_dataBuf;
    QVector<double> time,temperature,humidity,corrected;
    quint64 previousSegment=0;double previousTime=0;
    for(const auto &point : m_dataBuf) {
        if(previousSegment &&previousSegment!=point.segment_id) {
            // Geometry-only NaN separates true segments; it is never a sampled
            // value, recorded row, inspector target or interpolation.
            time.append((previousTime+point.time)/2);const double missing=std::numeric_limits<double>::quiet_NaN();
            temperature.append(missing);humidity.append(missing);corrected.append(missing);
        }
        time.append(point.time);temperature.append(point.raw_temp);humidity.append(point.humi);corrected.append(point.corrected_temp);
        previousSegment=point.segment_id;previousTime=point.time;
    }
    ui->customPlot->graph(0)->setData(time,temperature,true); ui->customPlot->graph(1)->setData(time,humidity,true);
    ui->customPlot->graph(2)->setData(time,corrected,true);
    const auto window=m_plotWindow.app_range_at(time.isEmpty()?0:time.last());
    ui->customPlot->xAxis->setRange(window.lower,window.upper);
    ui->customPlot->xAxis->setLabel(m_inputMode=="SERIAL" &&m_segmentPlotOrigin>0?"Session plot time (s; inspect for exact source time)":"Source session time (s)");
    m_displaySegment=m_lastTime.segment_id;
    app_render_plot_items();
    app_validate_inspection();
    if(auto seconds=findChild<QLineEdit*>("axisSeconds")) {
        if(!m_plotWindow.app_manual()) seconds->setText(QString::number(window.span));
    }
    if(!time.isEmpty()) {
        double minimum=std::numeric_limits<double>::infinity(),maximum=-minimum;
        double humidityMin=0,humidityMax=100;
        for(const auto &point:m_dataBuf) {
            if(point.time<window.lower ||point.time>window.upper) continue;
            if(ui->customPlot->graph(2)->visible() &&std::isfinite(point.corrected_temp)) { minimum=qMin(minimum,point.corrected_temp); maximum=qMax(maximum,point.corrected_temp); }
            if(ui->customPlot->graph(0)->visible() &&std::isfinite(point.raw_temp)) { minimum=qMin(minimum,point.raw_temp); maximum=qMax(maximum,point.raw_temp); }
            if(ui->customPlot->graph(1)->visible() &&std::isfinite(point.humi)) { humidityMin=qMin(humidityMin,point.humi); humidityMax=qMax(humidityMax,point.humi); }
        }
        if(std::isfinite(minimum) && !m_temperatureYManual) {
            const double padding=qMax(2.0,(maximum-minimum)*0.12);
            const auto current=ui->customPlot->yAxis->range();
            // Initialize to the actual valid values; then expand only near an
            // edge. This hysteresis avoids rescaling every incoming frame.
            if(!m_temperatureRangeInitialized || minimum<current.lower+current.size()*0.04 || maximum>current.upper-current.size()*0.04) {
                ui->customPlot->yAxis->setRange(m_temperatureRangeInitialized?qMin(current.lower,minimum-padding):minimum-padding,
                                              m_temperatureRangeInitialized?qMax(current.upper,maximum+padding):maximum+padding);
                m_temperatureRangeInitialized=true;
            }
        }
        const auto rhRange=ui->customPlot->yAxis2->range();
        if(!m_humidityYManual && (humidityMin<rhRange.lower || humidityMax>rhRange.upper)) {
            const double padding=qMax(5.0,(humidityMax-humidityMin)*0.05);
            ui->customPlot->yAxis2->setRange(qMin(rhRange.lower,humidityMin-padding),qMax(rhRange.upper,humidityMax+padding));
        }
    }
    ui->customPlot->replot();
}
void MainWindow::slotSwitchMode()
{
    app_set_recent_view(!m_isFastSample);
}
void MainWindow::app_set_recent_view(bool recent)
{
    m_isFastSample=recent;
    app_set_x_window(recent?"MANUAL":"AUTO",recent?32:5);
}
bool MainWindow::app_set_x_window(const QString &mode,int seconds)
{
    if((mode!="AUTO" && mode!="MANUAL") ||!m_plotWindow.app_set_mode(mode=="MANUAL",seconds)) return false;
    m_plotWindow.app_range_at(m_dataBuf.isEmpty()?0:m_dataBuf.last().time);
    if(auto combo=findChild<QComboBox*>("axisMode")) { const QSignalBlocker blocked(combo); combo->setCurrentText(mode); }
    if(auto entry=findChild<QLineEdit*>("axisSeconds")) { entry->setReadOnly(mode=="AUTO"); entry->setText(QString::number(seconds)); }
    ui->btn_mode->setText("X: "+mode); updatePlotRealTime(); app_write_status(); return true;
}
void MainWindow::app_reset_y_scale()
{
    m_temperatureYManual=m_humidityYManual=false; m_temperatureRangeInitialized=false;
    ui->customPlot->yAxis->setRange(15,35); ui->customPlot->yAxis2->setRange(0,100);
    updatePlotRealTime(); app_write_status();
}
void MainWindow::slotMeasure()
{
    app_hide_inspection();
    if(m_dataBuf.isEmpty()) { m_measureMode=false; app_clear_selection("INSUFFICIENT_DATA"); return; }
    const bool measure=!m_measureMode; app_clear_selection("NO_SELECTION"); m_measureMode=measure;
    ui->lbl_paramResult->setText(m_measureMode?"Select two source-time cursors for "+ui->cb_analysisChannel->currentText()+".":"Measure stopped.\nN/A: NO_SELECTION");
    app_fit_analysis_label();
    ui->customPlot->replot();
}
bool MainWindow::getNearestValue(double x,double &temperature,double &humidity,int &index)
{
    if(m_dataBuf.isEmpty()) return false;
    index=0; double distance=qAbs(m_dataBuf.first().time-x);
    for(int i=1;i<m_dataBuf.size();++i) { const double d=qAbs(m_dataBuf[i].time-x); if(d<distance) { distance=d; index=i; } }
    temperature=m_dataBuf[index].temp; humidity=m_dataBuf[index].humi; return true;
}
void MainWindow::app_clear_selection(const QString &reason)
{
    m_clickCount=0; m_hasAnalysis=false; m_analysisJson=QJsonObject(); m_selectionSegment=0;
    m_line1->setVisible(false); m_line2->setVisible(false); ui->btn_saveAnalysis->setEnabled(false);
    ui->lbl_paramResult->setText("N/A: "+reason+"\nChoose Measure and two source-time cursors.");
    app_fit_analysis_label();
}

void MainWindow::app_set_analysis_channel(app_signal_channel_t channel)
{
    m_analysisChannel=channel;
    const int index=channel==APP_SIGNAL_HUMIDITY?1:0;
    if(ui->cb_analysisChannel->currentIndex()!=index) ui->cb_analysisChannel->setCurrentIndex(index);
    if(m_hasAnalysis) app_analyze_selection(channel,m_x1,m_x2,m_selectionSegment);
}

app_signal_result_t MainWindow::app_analyze_selection(app_signal_channel_t channel,double t1,double t2,quint64 segment)
{
    m_analysisChannel=channel; m_x1=t1; m_x2=t2;
    const QSignalBlocker channelSignals(ui->cb_analysisChannel);
    ui->cb_analysisChannel->setCurrentIndex(channel==APP_SIGNAL_HUMIDITY?1:0);
    m_selectionSegment=segment?segment:m_lastTime.segment_id;
    QVector<app_signal_sample_t> samples;
    // Keep invalid records in the timing sequence; NaN values never become zero.
    for(const auto &point:m_dataBuf) {
        const double value=channel==APP_SIGNAL_HUMIDITY?point.humi:point.temp;
        app_signal_sample_t sample;
        sample.time_s=point.time; sample.value=value; sample.valid=std::isfinite(value); sample.segment_id=point.segment_id;
        samples.append(sample);
    }
    const bool expired=!m_dataBuf.isEmpty() && (t1<m_dataBuf.first().time || t2>m_dataBuf.last().time);
    m_analysis=app_signal_analyze_window(expired?QVector<app_signal_sample_t>():samples,channel,t1,t2,m_selectionSegment);
    if(expired) { m_analysis.selection_valid=false; m_analysis.selection_reason="CURSOR_EXPIRED_OR_OUTSIDE_BUFFER"; m_analysis.response_reason=m_analysis.selection_reason; }
    QSet<int> profiles;
    for(const auto &point:m_dataBuf) if(point.segment_id==m_selectionSegment &&point.time>=qMin(t1,t2) &&point.time<=qMax(t1,t2) &&point.calibration_profile>=0) profiles.insert(point.calibration_profile);
    if(channel==APP_SIGNAL_TEMPERATURE &&profiles.size()>1) {
        m_analysis.response_valid=false;m_analysis.response_reason="MIXED_CORRECTION_PROFILES";
    }
    m_analysisJson=app_signal_analysis_json(m_analysis,QCoreApplication::applicationVersion());
    QJsonArray profileList;for(int profile:profiles) profileList.append(profile);
    m_analysisJson["correction_profiles"]=profileList;
    m_analysisJson["temperature_series"]="Published MCU temperature; raw red curve is retained separately. Mixed profiles invalidate response-time fitting.";
    m_analysisJson["classification"]=app_classification(); m_analysisJson["input_mode"]=m_inputMode;
    m_analysisJson["protocol"]=m_frameProtocol;
    m_analysisJson["temperature_source"]=m_frameTemperature; m_analysisJson["humidity_source"]=m_frameHumidity;
    m_analysisJson["mode_generation"]=double(m_frameGeneration);
    m_analysisJson["frame_source"]=m_source; m_analysisJson["hardware_status"]="NOT TESTED ON HARDWARE";
    m_analysisJson["time_definition"]=m_inputMode=="SERIAL"?
        "Selection coordinates are session plot seconds; subtract source_segment_plot_origin_s for exact per-segment source time. Relative durations are unchanged.":
        "Source segment elapsed seconds; original unwrapped TH3 timestamps are retained separately";
    if(!m_dataBuf.isEmpty() && std::isfinite(t1) && std::isfinite(t2)) {
        for(const auto &point:m_dataBuf) if(point.segment_id==m_selectionSegment) {
            const double sourceOrigin=double(point.source_timestamp_ms)-point.time*1000;
            m_analysisJson["requested_source_start_ms"]=sourceOrigin+t1*1000;
            m_analysisJson["requested_source_end_ms"]=sourceOrigin+t2*1000;
            m_analysisJson["source_elapsed_start_s"]=t1-(point.time-point.source_elapsed_s);
            m_analysisJson["source_elapsed_end_s"]=t2-(point.time-point.source_elapsed_s);
            m_analysisJson["source_segment_plot_origin_s"]=point.time-point.source_elapsed_s;
            break;
        }
    }
    m_hasAnalysis=true; m_clickCount=0; ui->btn_saveAnalysis->setEnabled(true);
    for(auto line:{m_line1,m_line2}) line->setVisible(m_analysis.selection_valid);
    m_line1->start->setCoords(t1,0); m_line1->end->setCoords(t1,1);
    m_line2->start->setCoords(t2,0); m_line2->end->setCoords(t2,1);
    app_render_analysis(); ui->customPlot->replot(); app_write_status();
    return m_analysis;
}

void MainWindow::app_render_analysis()
{
    QString text=QString("%1 | Segment %2\nSource t1 %3 s | t2 %4 s\nSamples %5 | valid %6 | invalid %7\n")
        .arg(m_analysisChannel==APP_SIGNAL_HUMIDITY?"Humidity":"Temperature").arg(m_selectionSegment)
        .arg(m_analysisJson["source_elapsed_start_s"].toDouble(m_x1),0,'f',3)
        .arg(m_analysisJson["source_elapsed_end_s"].toDouble(m_x2),0,'f',3).arg(m_analysis.sample_count).arg(m_analysis.valid_sample_count).arg(m_analysis.invalid_sample_count);
    if(m_analysisJson.contains("requested_source_start_ms"))
        text+=QString("Source %1 to %2 ms\n").arg(m_analysisJson["requested_source_start_ms"].toDouble(),0,'f',0).arg(m_analysisJson["requested_source_end_ms"].toDouble(),0,'f',0);
    if(!m_analysis.selection_valid) text+="N/A: "+m_analysis.selection_reason;
    else {
        text+="Mean "+app_metric_text(m_analysis.mean)+"\nRMS "+app_metric_text(m_analysis.rms)
            +"\nMin / Max "+app_metric_text(m_analysis.minimum)+" / "+app_metric_text(m_analysis.maximum)
            +"\nPeak-to-peak "+app_metric_text(m_analysis.peak_to_peak)
            +"\nRise 10-90 "+app_metric_text(m_analysis.rise_time_10_90_s)
            +"\nFall 90-10 "+app_metric_text(m_analysis.fall_time_90_10_s)
            +"\nOvershoot "+app_metric_text(m_analysis.overshoot_pct)
            +"\nSettling 2% "+app_metric_text(m_analysis.settling_time_2pct_s)
            +"\nSettling 5% "+app_metric_text(m_analysis.settling_time_5pct_s);
    }
    ui->lbl_paramResult->setText(text);
    app_fit_analysis_label();
}

void MainWindow::app_fit_analysis_label()
{
    // QLabel's wrapped minimum hint may cover only a few lines. Reserve the
    // actual text height for the current width/font; the scroll area provides
    // access when a small window cannot show the whole panel at once.
    auto label=ui->lbl_paramResult;
    const int height=label->heightForWidth(qMax(1,label->width()));
    if(height>0 && label->minimumHeight()!=height) {
        label->setMinimumHeight(height); label->updateGeometry();
        ui->analysisLayout->activate(); ui->sideLayout->activate();
    }
}

bool MainWindow::app_save_analysis(const QString &path)
{
    m_exportError.clear();
    if(!m_hasAnalysis || !app_output_path_allowed(path)) { m_exportError="No analysis selection, or destination is protected evidence"; return false; }
    // Explicit analysis output never replaces the authoritative raw/CSV/metadata.
    const QString absolute=QFileInfo(path).absoluteFilePath();
    QSaveFile file(absolute);
    if(!file.open(QIODevice::WriteOnly)) { m_exportError="Cannot save analysis: "+file.errorString(); return false; }
    const auto bytes=QJsonDocument(m_analysisJson).toJson(QJsonDocument::Indented);
    if(file.write(bytes)!=bytes.size() ||!file.commit()) { m_exportError="Analysis save failed: "+file.errorString(); return false; }
    app_remember_export(absolute); return true;
}

bool MainWindow::app_export_plot(const QString &path)
{
    m_exportError.clear();
    if(!app_output_path_allowed(path)) { m_exportError="Plot destination is protected evidence"; return false; }
    const QString absolute=QFileInfo(path).absoluteFilePath();
    app_render_state(); updatePlotRealTime();
    if(!ui->customPlot->savePng(absolute,qMax(1000,ui->customPlot->width()),qMax(600,ui->customPlot->height()),1.0,-1)) { m_exportError="Cannot write PNG: check path, permissions, file lock and free space"; return false; }
    app_remember_export(absolute); return true;
}

void MainWindow::app_remember_export(const QString &path)
{
    m_latestExport=path;
    const auto settings=app_desktop_open_settings();
    settings->setValue("lastExportDirectory",QFileInfo(path).absolutePath());
    if(QFileInfo(path).suffix().compare("png",Qt::CaseInsensitive)==0) settings->setValue("lastPngDirectory",QFileInfo(path).absolutePath());
    settings->setValue("latestExport",path); settings->sync();
    ui->statusbar->showMessage("Saved: "+path,10000); app_write_status();
}
void MainWindow::app_remember_session()
{
    const QString path=m_recorder.app_session_directory(); QString error;
    if(!app_session_validate_directory(path,&error)) return;
    m_latestSession=QFileInfo(path).canonicalFilePath(); m_latestSessionIdentity=QFileInfo(path).fileName();
    const auto settings=app_desktop_open_settings(); settings->setValue("latestSession/path",m_latestSession); settings->setValue("latestSession/identity",m_latestSessionIdentity); settings->sync();
}
bool MainWindow::app_open_latest_session()
{
    if(!app_session_validate_directory(m_latestSession,&m_exportError) ||m_latestSessionIdentity!=QFileInfo(m_latestSession).fileName()) return false;
    if(!QDesktopServices::openUrl(QUrl::fromLocalFile(m_latestSession))) { m_exportError="Windows could not open the session directory"; return false; }
    return true;
}
bool MainWindow::app_export_session(const QString &parent,QString *destination)
{
    m_exportError.clear();
    if(m_running) { m_exportError="Stop recording before exporting the session"; return false; }
    QString path;
    if(!app_session_export_directory(m_latestSession,parent,&path,&m_exportError)) return false;
    if(destination) *destination=path;
    const auto settings=app_desktop_open_settings(); settings->setValue("lastExportDirectory",parent); settings->sync();
    m_latestExport=path; settings->setValue("latestExport",path); settings->sync();
    ui->statusbar->showMessage("Session copied and SHA-256 verified: "+path,10000); app_write_status(); return true;
}

bool MainWindow::app_output_path_allowed(const QString &path) const
{
    if(path.isEmpty()) return false;
    const auto normalized=[](const QString &value) {
        const QFileInfo info(value); const QString canonical=info.canonicalFilePath();
        return canonical.isEmpty()?QDir::cleanPath(info.absoluteFilePath()):canonical;
    };
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity sensitivity=Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity sensitivity=Qt::CaseSensitive;
#endif
    const QString target=normalized(path),name=QFileInfo(path).fileName().toLower();
    if(name=="raw_input.log" || name=="parsed.csv" || name=="metadata.json" || name=="command_tx.jsonl" ||name=="session_events.jsonl") return false;
    const QString session=m_recorder.app_session_directory();
    const QStringList protectedPaths={m_input.app_file_path(),m_statusPath,
        session.isEmpty()?QString():session+"/raw_input.log",session.isEmpty()?QString():session+"/parsed.csv",
        session.isEmpty()?QString():session+"/metadata.json",session.isEmpty()?QString():session+"/command_tx.jsonl",
        session.isEmpty()?QString():session+"/session_events.jsonl"};
    for(const auto &protectedPath:protectedPaths)
        if(!protectedPath.isEmpty() && QString::compare(target,normalized(protectedPath),sensitivity)==0) return false;
    return true;
}

bool MainWindow::app_select_cursor(double x)
{
    if(m_displayPaused||!m_measureMode||m_dataBuf.isEmpty()||!std::isfinite(x)) return false;
    double temperature,humidity; int index;
    if(x<m_dataBuf.first().time || x>m_dataBuf.last().time || !getNearestValue(x,temperature,humidity,index)) {
        ui->lbl_paramResult->setText("N/A: CURSOR_OUTSIDE_DISPLAY_BUFFER"); return false;
    }
    const double sampleTime=m_dataBuf[index].time;
    if(m_clickCount==0) {
        m_x1=sampleTime; m_selectionSegment=m_dataBuf[index].segment_id; m_clickCount=1;
        m_line1->start->setCoords(sampleTime,0); m_line1->end->setCoords(sampleTime,1); m_line1->setVisible(true);
        ui->lbl_paramResult->setText(QString("%1 cursor 1: source %2 ms\nSegment %3: %4 s\nChoose cursor 2.")
            .arg(ui->cb_analysisChannel->currentText()).arg(m_dataBuf[index].raw_source_timestamp_ms).arg(m_selectionSegment).arg(m_dataBuf[index].source_elapsed_s,0,'f',3));
    }
    else {
        if(m_x1<m_dataBuf.first().time || m_selectionSegment!=m_dataBuf[index].segment_id) { app_clear_selection("CURSOR_EXPIRED_OR_MULTIPLE_SEGMENTS"); return false; }
        app_analyze_selection(m_analysisChannel,qMin(m_x1,sampleTime),qMax(m_x1,sampleTime),m_selectionSegment);
        m_measureMode=false;
    }
    app_fit_analysis_label();
    ui->customPlot->replot();
    return true;
}
void MainWindow::customPlotMousePress(QMouseEvent *event)
{
    if(event->button()!=Qt::LeftButton || !ui->customPlot->axisRect()->rect().contains(event->pos())) return;
    if(m_measureMode) app_select_cursor(ui->customPlot->xAxis->pixelToCoord(event->pos().x()));
}
void MainWindow::customPlotMouseWheel(QWheelEvent *event)
{
    const bool humidity=event->pos().x()>=ui->customPlot->axisRect()->rect().center().x();
    ui->customPlot->axisRect()->setRangeZoomAxes(nullptr,humidity?ui->customPlot->yAxis2:ui->customPlot->yAxis);
    if(humidity) m_humidityYManual=true; else m_temperatureYManual=true;
}

bool MainWindow::eventFilter(QObject *object,QEvent *event)
{
    if(object!=ui->customPlot) return QMainWindow::eventFilter(object,event);
    const QRect area=ui->customPlot->axisRect()->rect();
    const auto axisAt=[this,area](const QPoint &position)->QCPAxis* {
        if(position.y()<area.top() ||position.y()>area.bottom()) return nullptr;
        return position.x()<area.center().x()?ui->customPlot->yAxis:ui->customPlot->yAxis2;
    };
    const auto manual=[this](QCPAxis *axis) {
        if(axis==ui->customPlot->yAxis) m_temperatureYManual=true; else m_humidityYManual=true;
    };
    if(event->type()==QEvent::ToolTip) {
        auto help=static_cast<QHelpEvent*>(event);
        app_inspect_plot(help->pos(),true);return true;
    }
    if(event->type()==QEvent::Leave) app_hide_inspection();
    if(event->type()==QEvent::Wheel) {
        app_hide_inspection();
        auto wheel=static_cast<QWheelEvent*>(event); auto axis=axisAt(wheel->pos());
        if(!axis) return true;
        const int delta=wheel->angleDelta().y();
        if(delta) { manual(axis); axis->scaleRange(std::pow(0.85,delta/120.0),axis->pixelToCoord(wheel->pos().y())); ui->customPlot->replot(); }
        return true;
    }
    if(event->type()==QEvent::MouseButtonPress) {
        auto mouse=static_cast<QMouseEvent*>(event);
        if(mouse->button()==Qt::LeftButton) {
            if(!area.contains(mouse->pos())) app_hide_inspection();
            m_plotDragged=false;
            if(m_measureMode) {
                app_hide_inspection();
                if(area.contains(mouse->pos())) app_select_cursor(ui->customPlot->xAxis->pixelToCoord(mouse->pos().x()));
                return true;
            }
            m_dragAxis=axisAt(mouse->pos());
            if(m_dragAxis) { m_dragStartRange=m_dragAxis->range(); m_dragStartPoint=mouse->pos(); return true; }
        }
    }
    if(event->type()==QEvent::MouseMove &&m_dragAxis) {
        const auto mouse=static_cast<QMouseEvent*>(event);
        if(!m_plotDragged &&(mouse->pos()-m_dragStartPoint).manhattanLength()<QApplication::startDragDistance()) return true;
        if(!m_plotDragged) { m_plotDragged=true;manual(m_dragAxis);app_hide_inspection(); }
        const double shift=(mouse->pos().y()-m_dragStartPoint.y())*m_dragStartRange.size()/qMax(1,area.height());
        m_dragAxis->setRange(m_dragStartRange.lower+shift,m_dragStartRange.upper+shift);
        ui->customPlot->replot(); return true;
    }
    if(event->type()==QEvent::MouseButtonRelease &&m_dragAxis) {
        const auto mouse=static_cast<QMouseEvent*>(event);m_dragAxis=nullptr;
        if(!m_plotDragged &&(mouse->pos()-m_dragStartPoint).manhattanLength()<QApplication::startDragDistance())
            app_inspect_plot(mouse->pos(),false,true);
        return true;
    }
    return QMainWindow::eventFilter(object,event);
}
