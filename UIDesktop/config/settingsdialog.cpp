
#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include <QSerialPort>
#include <QLabel>
#include <QCheckBox>
#include "app_desktop_settings.h"

SettingsDialog::SettingsDialog(QWidget *parent,const QString &configuredPort,int baud,bool automaticRecovery,bool restoreConfirmed) :
    QDialog(parent),
    ui(new Ui::SettingsDialog),m_configuredPort(configuredPort)
{
    ui->setupUi(this);
    this->setWindowTitle("UART connection settings");
    ui->additionalOptionsGroupBox->setTitle("Connection");
    ui->localEchoCheckBox->hide();
    m_statusHint=new QLabel("8N1 / no flow control. Draw / Start connects; End Session disconnects. Automatic recovery trusts this USB attachment, not a unique MCU identity.",ui->additionalOptionsGroupBox);
    m_statusHint->setWordWrap(true);
    ui->verticalLayout->addWidget(m_statusHint);
    m_automaticRecovery=new QCheckBox("Automatically reconnect this selected adapter",ui->additionalOptionsGroupBox);
    m_automaticRecovery->setObjectName("automaticRecoveryOption");m_automaticRecovery->setChecked(automaticRecovery);
    m_restoreConfirmed=new QCheckBox("Restore confirmed sources and temperature fit",ui->additionalOptionsGroupBox);
    m_restoreConfirmed->setObjectName("restoreConfirmedOption");m_restoreConfirmed->setChecked(restoreConfirmed);
    m_restoreConfirmed->setEnabled(automaticRecovery);
    ui->verticalLayout->addWidget(m_automaticRecovery);ui->verticalLayout->addWidget(m_restoreConfirmed);
    connect(m_automaticRecovery,&QCheckBox::toggled,m_restoreConfirmed,&QCheckBox::setEnabled);
    m_automaticRecovery->setToolTip("Twelve attempts per incident, five seconds apart. Requires a unique USB serial or matching Windows instance AND USB location. Moving to another USB socket or another matching adapter requires manual selection.");
    ui->btnApply->setText("Select serial input");
    ui->selectBox->setTitle("Available ports");
    // Offline/multiple-device hints must not widen the dialog to their text length.
    ui->cbSerialPort->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    ui->cbSerialPort->setMinimumContentsLength(12);

    //填充下拉选项
    ui->cbBaudRate->addItems({"9600","19200","38400","57600","115200"});
    ui->cbBaudRate->setEditable(true);
    ui->cbBaudRate->setCurrentText(QString::number(baud));

    ui->cbDataBits->addItems({"5","6","7","8"});
    ui->cbDataBits->setCurrentText("8");

    ui->cbParity->addItems({"No","Odd","Even"});
    ui->cbParity->setCurrentText("No");

    ui->cbStopBits->addItems({"1","1.5","2"});
    ui->cbStopBits->setCurrentText("1");

    ui->cbFlowControl->addItems({"No","Hardware","Software"});
    ui->cbFlowControl->setCurrentText("No");
    // This adapter's explicit contract is 8N1/no flow. Do not offer ineffective settings.
    for(auto widget : {ui->cbDataBits,ui->cbParity,ui->cbStopBits,ui->cbFlowControl}) widget->setEnabled(false);
    ui->localEchoCheckBox->setEnabled(false);

    //扫描串口
    scanPorts();

    //信号槽绑定
    connect(ui->cbSerialPort, &QComboBox::currentTextChanged, this, &SettingsDialog::onPortChanged);
    connect(ui->btnApply, &QPushButton::clicked, this, &SettingsDialog::onApply);
    connect(ui->btnCancel,&QPushButton::clicked,this,&QDialog::reject);
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::scanPorts()
{
    ui->cbSerialPort->clear();
    QList<QSerialPortInfo> portList = QSerialPortInfo::availablePorts();
    for(auto &info : portList)
    {
        ui->cbSerialPort->addItem(info.portName());
    }
    const auto settings=app_desktop_open_settings();
    const auto choice=app_desktop_choose_serial(app_desktop_serial_devices(),
        m_configuredPort.isEmpty()?settings->value("serial/port").toString():m_configuredPort,
        settings->value("serial/serialNumber").toString());
    if(!choice.port.isEmpty()) ui->cbSerialPort->setCurrentText(choice.port);
    else { ui->cbSerialPort->insertItem(0,choice.reason,QString()); ui->cbSerialPort->setCurrentIndex(0); }
    onPortChanged(choice.port);
}

void SettingsDialog::onPortChanged(const QString &portName)
{
    ui->btnApply->setEnabled(false);
    for(auto label:{ui->descriptionLabel,ui->manufacturerLabel,ui->serialNumberLabel,ui->locationLabel,ui->vidLabel,ui->pidLabel}) {
        label->setText("Not connected"); label->setWordWrap(true);
    }
    QList<QSerialPortInfo> portList = QSerialPortInfo::availablePorts();
    for(auto &info : portList)
    {
        if(info.portName() == portName)
        {
            fillPortInfo(info);
            bool identity=false;
            for(const auto &device:app_desktop_serial_devices()) if(device.port==portName)
                identity=app_desktop_identity_kind(device)!="UNVERIFIED";
            m_automaticRecovery->setEnabled(identity);
            m_restoreConfirmed->setEnabled(identity &&m_automaticRecovery->isChecked());
            if(!identity) m_statusHint->setText("No verifiable USB identity/location. Select this port manually; automatic reconnect and restore are unavailable.");
            ui->btnApply->setEnabled(true);
            break;
        }
    }
}

void SettingsDialog::fillPortInfo(QSerialPortInfo info)
{
    ui->descriptionLabel->setText("Description: "+info.description());
    ui->manufacturerLabel->setText("Manufacturer: "+info.manufacturer());
    ui->serialNumberLabel->setText("Serial: "+(info.serialNumber().isEmpty()?QString("not supplied"):info.serialNumber()));
    ui->locationLabel->setText("Device: "+info.systemLocation());
    ui->vidLabel->setText("VID: "+(info.hasVendorIdentifier()?QString::number(info.vendorIdentifier(),16):QString("unknown")));
    ui->pidLabel->setText("PID: "+(info.hasProductIdentifier()?QString::number(info.productIdentifier(),16):QString("unknown")));
}

void SettingsDialog::onApply()
{
    bool validBaud=false; const int baud=ui->cbBaudRate->currentText().toInt(&validBaud);
    if(!validBaud ||baud<=0 ||baud>1000000 ||!ui->btnApply->isEnabled()) {
        m_statusHint->setText("Choose an available port and baud between 1 and 1000000."); return;
    }
    m_cfg.portName = ui->cbSerialPort->currentText();
    m_cfg.baudRate = ui->cbBaudRate->currentText().toInt();
    m_cfg.dataBits = ui->cbDataBits->currentText().toInt();

    QString parityStr = ui->cbParity->currentText();
    if(parityStr == "No")
        m_cfg.parity = QSerialPort::NoParity;
    else if(parityStr == "Odd")
        m_cfg.parity = QSerialPort::OddParity;
    else
        m_cfg.parity = QSerialPort::EvenParity;

    QString stopStr = ui->cbStopBits->currentText();
    if(stopStr == "1")
        m_cfg.stopBits = QSerialPort::OneStop;
    else if(stopStr == "1.5")
        m_cfg.stopBits = QSerialPort::OneAndHalfStop;
    else
        m_cfg.stopBits = QSerialPort::TwoStop;

    QString flowStr = ui->cbFlowControl->currentText();
    if(flowStr == "No")
        m_cfg.flowControl = QSerialPort::NoFlowControl;
    else if(flowStr == "Hardware")
        m_cfg.flowControl = QSerialPort::HardwareControl;
    else
        m_cfg.flowControl = QSerialPort::SoftwareControl;

    m_cfg.localEcho = ui->localEchoCheckBox->isChecked();
    m_cfg.automaticRecovery=m_automaticRecovery->isEnabled() &&m_automaticRecovery->isChecked();
    m_cfg.restoreConfirmed=m_cfg.automaticRecovery &&m_restoreConfirmed->isChecked();

    this->accept();
}

SerialConfig SettingsDialog::getSerialConfig()
{
    return m_cfg;
}
