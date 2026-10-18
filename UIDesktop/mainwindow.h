#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include <QTimer>
#include <QJsonObject>
#include <QLabel>
#include "qcustomplot.h"
#include "config/settingsdialog.h"
#include "app_th3.h"
#include "app_file_input.h"
#include "app_session_recorder.h"
#include "app_signal_analysis.h"
#include "app_serial_input.h"
#include "app_plot_window.h"
#include "app_temperature_evidence.h"
#include "app_serial_recovery.h"
#include "app_inspector.h"
#include <QElapsedTimer>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

struct SamplePoint {
    double time,temp,humi; quint64 segment_id,source_timestamp_ms;
    double raw_temp,corrected_temp; int calibration_profile;
    double source_elapsed_s; QString temperature_source,humidity_source,received_at;
    bool original_paired; int calibration_id;
    quint32 raw_source_timestamp_ms;
    int configured_profile;
    QString input_mode;
};
struct TriggerMark { double t; double val; int channel; };

class MainWindow : public QMainWindow
{
    Q_OBJECT
    friend struct app_window_test_access_t;
public:
    explicit MainWindow(QWidget *parent=nullptr);
    ~MainWindow();
    void app_configure_input(const QString &path,const QString &mode,const QString &record_directory);
    void app_set_status_path(const QString &path);
    QJsonObject app_status_snapshot() const;
    void app_finish_session();
    void app_configure_serial(const QString &port,int baud,const QString &record_directory);
    bool app_send_command(const QString &command);
    bool app_request_sources(const QString &temperature,const QString &humidity);
    bool app_request_calibration(int profile);
    app_signal_result_t app_analyze_selection(app_signal_channel_t channel,double t1,double t2,quint64 segment=0);
    bool app_select_cursor(double source_elapsed_s);
    void app_set_analysis_channel(app_signal_channel_t channel);
    bool app_save_analysis(const QString &path);
    bool app_export_plot(const QString &path);
    void app_set_recent_view(bool recent);
    void app_set_recording_root(const QString &path,bool persist=true);
    bool app_set_x_window(const QString &mode,int seconds);
    void app_reset_y_scale();
    bool app_open_latest_session();
    bool app_export_session(const QString &parent,QString *destination=nullptr);
    QString app_last_export_error() const { return m_exportError; }
    bool app_reconnect_serial();
    QString app_about_text() const;
    void app_set_recovery_options(bool automatic,bool restoreConfirmed,bool persist=true);
    void app_close_for_automation();
protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *object,QEvent *event) override;
private slots:
    void slotDraw();
    void slotStop();
    void slotPauseDisplay();
    void slotMeasure();
    void slotExit();
    void slotConfig();
    void slotSwitchMode();
    void slotReadFileData();
    void slotTrigLevelChanged(double value);
    void customPlotMousePress(QMouseEvent *event);
    void customPlotMouseWheel(QWheelEvent *event);
private:
    void app_clear_plot_segment();
    void app_render_state();
    void app_write_status();
    void app_accept_frame(const app_th3_frame_t &frame,const app_th3_time_t &time);
    void app_consume_records(const QList<app_line_record_t> &records);
    void app_read_serial();
    bool app_persist_mode_state();
    void app_remember_export(const QString &path);
    void app_remember_session();
    bool app_append_display_event(const QString &type,const QJsonObject &details=QJsonObject(),bool allow_source_time=true);
    void app_serial_error(QSerialPort::SerialPortError error);
    void app_serial_tick();
    void app_close_serial();
    void app_invalidate_serial_confirmation();
    bool app_open_serial_recovery(const QString &port,bool automatic);
    bool app_record_connection_event(const QString &type,const QJsonObject &details=QJsonObject());
    void app_query_live_identity();
    void app_release_serial_handle();
    void app_cancel_recovery_work(const QString &reason);
    void app_begin_configuration_recovery();
    void app_restore_configuration_tick();
    void app_remember_confirmed_configuration();
    bool app_live_configuration_confirmed() const;
    QJsonObject app_restore_snapshot() const;
    bool app_inspect_plot(const QPoint &position,bool events_only=false,bool cycle=false);
    void app_hide_inspection();
    void app_validate_inspection();
    void app_show_inspector();
    void app_show_user_guide();
    void app_render_plot_items();
    bool checkEdgeTrigger(double previous,double current);
    void updatePlotRealTime();
    bool getNearestValue(double x,double &temperature,double &humidity,int &index);
    void app_clear_selection(const QString &reason);
    void app_render_analysis();
    void app_fit_analysis_label();
    QString app_classification() const;
    bool app_output_path_allowed(const QString &path) const;
    Ui::MainWindow *ui;
    QVector<SamplePoint> m_dataBuf;
    QVector<SamplePoint> m_visibleSamples;
    QVector<QJsonObject> m_visibleEvents;
    QJsonObject m_inspection;
    app_inspector_t *m_inspector=nullptr;
    QDialog *m_userGuide=nullptr;
    int m_eventInspectionOffset=0;
    QPoint m_inspectionPosition;
    bool m_plotDragged=false;
    QVector<TriggerMark> m_triggerMarks;
    QList<QCPAbstractItem*> m_renderedMarks;
    QVector<QJsonObject> m_displayEvents;
    bool m_displayPaused=false;
    quint64 m_displaySegment=0;
    bool m_isFastSample=true;
    double m_trigLevel=40.0;
    bool m_measureMode=false;
    int m_clickCount=0;
    double m_x1=0,m_x2=0;
    quint64 m_selectionSegment=0;
    bool m_hasAnalysis=false;
    app_signal_channel_t m_analysisChannel=APP_SIGNAL_TEMPERATURE;
    app_signal_result_t m_analysis;
    QJsonObject m_analysisJson;
    QCPTextElement *m_plotTitle=nullptr,*m_plotStatus=nullptr;
    bool m_temperatureRangeInitialized=false;
    bool m_temperatureYManual=false,m_humidityYManual=false;
    QCPAxis *m_dragAxis=nullptr;
    QCPRange m_dragStartRange;
    QPoint m_dragStartPoint;
    app_plot_window_t m_plotWindow;
    QCPItemLine *m_line1=nullptr,*m_line2=nullptr;
    QCPItemStraightLine *m_trigLine=nullptr;
    QTimer *m_fileTimer;
    QLabel *m_sourceLabel,*m_pathLabel;
    app_file_input_t m_input;
    app_serial_input_t m_serial;
    QString m_serialPort,m_pendingCommand,m_commandState="IDLE";
    int m_serialBaud=115200;
    QTimer *m_commandTimer=nullptr;
    QTimer *m_serialTimer=nullptr;
    QElapsedTimer m_hostClock;
    app_serial_recovery_t m_recovery;
    bool m_serialIoBusy=false,m_stopping=false,m_gapPending=false,m_identityQueriesPending=false;
    bool m_connectionHasFrame=false;
    quint64 m_connectionEpoch=0;
    int m_identityQueryAttempts=0;
    double m_segmentPlotOrigin=0;
    qint64 m_lastFrameHostMs=-1;
    QString m_observedFirmware,m_firmwareObservedAt;
    QString m_frameReceivedAt;
    QString m_stopReasonOverride;
    bool m_firmwareCurrent=false;
    quint64 m_commandTx=0,m_commandRx=0;
    QString m_requestedTemperature="REAL",m_requestedHumidity="REAL";
    QString m_frameTemperature="UNKNOWN",m_frameHumidity="UNKNOWN",m_frameProtocol="TH3";
    QString m_readbackTemperature="UNKNOWN",m_readbackHumidity="UNKNOWN";
    quint32 m_frameGeneration=0,m_readbackGeneration=0,m_expectedGeneration=0;
    bool m_modeAwaiting=false,m_modeReadback=false,m_modeConfirmed=false;
    bool m_temperatureValid=false,m_humidityValid=false;
    QString m_modeState="EFFECTIVE MODE UNKNOWN";
    app_th3_timeline_t m_timeline;
    app_temperature_evidence_t m_temperatureEvidence;
    int m_requestedCalibration=-1,m_effectiveCalibration=-1,m_frameCalibration=-1;
    bool m_calibrationConfirmed=false,m_correctedVisiblePreference=true,m_calibrationChoiceEdited=false;
    bool m_calibrationApplied=false;
    QString m_calibrationState="Correction profile unknown";
    app_session_recorder_t m_recorder;
    QString m_inputMode="FILE_STREAM",m_recordDirectory,m_statusPath;
    QString m_state="STOPPED",m_diagnostic,m_source="UNKNOWN";
    QString m_connectionState="DISCONNECTED",m_deviceSelectionReason;
    QString m_mcuIdentityHash,m_boundMcuIdentityHash,m_mcuIdentityError;
    bool m_mcuIdentityQueried=false,m_mcuIdentityCurrent=false;
    bool m_recoveryMigrationNotice=false;
    bool m_autoRecovery=true,m_restoreConfirmedOption=true,m_restorePending=false,m_autoRestoreCommand=false;
    bool m_restoreSetIssued=false,m_restoreCalIssued=false,m_automationClose=false;
    qint64 m_restoreDeadline=0;
    QString m_restoreState="IDLE",m_restoreReason,m_lastSerialError;
    app_serial_device_t m_trustedDevice;
    QJsonObject m_savedConfiguration,m_restoreTarget;
    bool m_sourceChoicesEdited=false;
    QString m_latestSession,m_latestSessionIdentity,m_latestExport,m_exportError;
    bool m_running=false,m_everStarted=false;
    double m_lastTemp,m_lastHumi;
    app_th3_time_t m_lastTime;
    quint32 m_lastTimestamp=0;
    quint16 m_validFlags=0,m_errorFlags=0;
    quint64 m_parsedCount=0,m_malformedCount=0,m_diagnosticCount=0,m_restartCount=0;
    int m_maxUiPoints=0;
};
#endif
