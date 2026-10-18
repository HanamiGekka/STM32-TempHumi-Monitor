#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QSerialPortInfo>
class QLabel;
class QCheckBox;

namespace Ui {
class SettingsDialog;
}

struct SerialConfig
{
    QString portName;
    int baudRate;
    int dataBits;
    int parity;
    int stopBits;
    int flowControl;
    bool localEcho;
    bool automaticRecovery=false,restoreConfirmed=false;
};

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr,const QString &configuredPort=QString(),int baud=115200,
                            bool automaticRecovery=false,bool restoreConfirmed=false);
    ~SettingsDialog();
    SerialConfig getSerialConfig();

private slots:
    void onPortChanged(const QString &portName);
    void onApply();

private:
    Ui::SettingsDialog *ui;
    SerialConfig m_cfg={};
    QString m_configuredPort;
    QLabel *m_statusHint=nullptr;
    QCheckBox *m_automaticRecovery=nullptr,*m_restoreConfirmed=nullptr;
    void fillPortInfo(QSerialPortInfo info);
    void scanPorts();
};

#endif // SETTINGSDIALOG_H

