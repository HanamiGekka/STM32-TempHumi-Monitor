#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDateTime>
#include <QCoreApplication>
#include <QFileInfo>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#ifndef APP_GIT_HEAD
#define APP_GIT_HEAD "UNKNOWN"
#endif

void MainWindow::app_cancel_recovery_work(const QString &reason)
{
    ++m_connectionEpoch;
    if(m_serialTimer) m_serialTimer->stop();
    if(m_commandTimer) m_commandTimer->stop();
    m_pendingCommand.clear();m_identityQueriesPending=false;m_recovery.stop();
    if(m_restorePending) { m_restoreState="FAILED";m_restoreReason=reason; }
    m_restorePending=false;
}

void MainWindow::app_close_serial()
{
    const QSignalBlocker blocked(m_serial);
    if(m_serial.isOpen()) m_serial.close();
}

void MainWindow::app_invalidate_serial_confirmation()
{
    ++m_connectionEpoch;
    m_commandTimer->stop();m_pendingCommand.clear();m_commandState="DISCONNECTED_UNCONFIRMED";
    m_modeAwaiting=m_modeReadback=m_modeConfirmed=false;
    m_modeState="EFFECTIVE MODE UNKNOWN: live confirmation required";
    m_calibrationConfirmed=false;m_effectiveCalibration=m_requestedCalibration=-1;
    m_calibrationState="Correction profile unconfirmed";m_firmwareCurrent=false;
    m_connectionHasFrame=false;m_mcuIdentityQueried=m_mcuIdentityCurrent=false;m_mcuIdentityHash.clear();m_mcuIdentityError.clear();
    m_temperatureEvidence.reset();m_identityQueriesPending=true;m_identityQueryAttempts=0;
}

bool MainWindow::app_record_connection_event(const QString &type,const QJsonObject &details)
{
    QJsonObject information=details;
    information["port"]=m_serialPort;
    information["connection"]=m_recovery.snapshot();
    information["pending_read_bytes"]=double(qMax<qint64>(0,m_serial.bytesAvailable()));
    information["pending_write_bytes"]=double(qMax<qint64>(0,m_serial.bytesToWrite()));
    information["partial_line_bytes"]=m_serial.app_serial_pending_bytes();
    information["last_observed_source_timestamp_ms"]=m_lastTime.segment_id?QJsonValue(double(m_lastTimestamp)):QJsonValue();
    if(!app_append_display_event(type,information,false)) return false;
    return app_persist_mode_state();
}

void MainWindow::app_serial_error(QSerialPort::SerialPortError error)
{
    if(!m_running ||m_inputMode!="SERIAL" ||m_serialIoBusy ||m_stopping ||error==QSerialPort::NoError) return;
    if(!m_serial.isOpen() &&error!=QSerialPort::PermissionError
       &&(m_recovery.state()=="RECOVERING" ||m_recovery.state()=="DISCONNECTED" ||m_recovery.state()=="BUSY_OR_DENIED" ||m_recovery.state()=="WAITING_FOR_USER")) return;
    const QString reason=m_serial.errorString();
    const auto action=m_recovery.error(error,m_hostClock.elapsed());
    if(action==app_serial_recovery_t::RECOVERY_IGNORE) return;
    QJsonObject details{{"qt_error_code",int(error)},{"qt_error_text",reason},
                        {"handle_was_open",m_serial.isOpen()},{"automatic_action",action==app_serial_recovery_t::RECOVERY_KEEP_STREAM?"KEEP_READING":"RELEASE_HANDLE"}};
    // Snapshot queued bytes before releasing a lost handle; callbacks produced
    // by a failed read/close are not another recovery incident.
    if(!app_record_connection_event(action==app_serial_recovery_t::RECOVERY_KEEP_STREAM?"SERIAL_TRANSIENT_ERROR":"SERIAL_DISCONNECTED",details)) return;
    m_diagnostic=QString("Qt serial error %1: %2").arg(int(error)).arg(reason);m_lastSerialError=m_diagnostic;
    if(action==app_serial_recovery_t::RECOVERY_KEEP_STREAM) {
        m_state="TRANSIENT";app_render_state();app_write_status();return;
    }
    app_begin_configuration_recovery();app_release_serial_handle();
}

void MainWindow::app_release_serial_handle()
{
    m_serialIoBusy=true;
    for(int n=0;m_running &&m_serial.isOpen() &&m_serial.bytesAvailable()>0 &&n<8;++n) {
        const auto batch=m_serial.app_serial_read();
        if(batch.records.isEmpty() &&m_serial.bytesAvailable()==0) break;
        app_consume_records(batch.records);
    }
    if(m_running) app_consume_records(m_serial.app_serial_finish().records);
    app_close_serial();m_serialIoBusy=false;
    app_invalidate_serial_confirmation();m_gapPending=true;
    m_connectionState=m_recovery.state();m_state=m_connectionState;
    if(m_running) app_persist_mode_state();
    app_render_state();app_write_status();
}

bool MainWindow::app_open_serial_recovery(const QString &port,bool automatic)
{
    if(!m_running ||!m_recorder.app_session_active() ||m_stopping ||m_serial.isOpen()) return false;
    if(!automatic) {
        app_serial_device_t selected;selected.port=port;
        for(const auto &d:app_desktop_serial_devices()) if(d.port==port) selected=d;
        if(!app_desktop_identity_matches(m_trustedDevice,selected)) {
            m_restoreReason="Explicitly selected attachment differs; fresh MCU UID must match before restoring prior configuration";
            m_trustedDevice=selected;
        }
        m_recovery.start(selected,m_hostClock.elapsed());
    }
    m_serialPort=port;
    if(!app_record_connection_event("SERIAL_RECONNECT_ATTEMPT",{{"automatic",automatic}})) return false;
    m_serialIoBusy=true;
    const bool opened=m_serial.app_serial_open(port,m_serialBaud);
    const auto code=m_serial.error();const auto error=m_serial.errorString();
    m_serialIoBusy=false;
    if(!opened) {
        m_recovery.open_failed(code,m_hostClock.elapsed());
        m_connectionState=m_recovery.state();m_state=m_connectionState;
        m_diagnostic=QString("Reconnect failed (Qt %1): %2").arg(int(code)).arg(error);
        app_record_connection_event("SERIAL_RECONNECT_FAILED",{{"qt_error_code",int(code)},{"qt_error_text",error},{"automatic",automatic}});
    } else {
        app_invalidate_serial_confirmation();
        m_recovery.opened(m_hostClock.elapsed());m_gapPending=true;
        m_restoreDeadline=m_hostClock.elapsed()+20000;
        m_connectionState="WAITING_DATA";m_state="WAITING";
        m_diagnostic="Port reopened; waiting for TH3, MODE, STATUS and CAL evidence";
        app_record_connection_event("SERIAL_REOPENED",{{"automatic",automatic},{"mode_confirmed",false},{"calibration_confirmed",false}});
    }
    app_render_state();app_write_status();return opened;
}

bool MainWindow::app_reconnect_serial()
{
    if(m_inputMode!="SERIAL" ||m_serialPort.isEmpty() ||!m_running ||m_stopping ||m_serialIoBusy) return false;
    if(m_serial.isOpen()) {
        app_begin_configuration_recovery();
        if(!app_record_connection_event("SERIAL_MANUAL_RESTART",{{"reason","User explicitly requested reopening the selected port"}})) return false;
        app_release_serial_handle();
    }
    return app_open_serial_recovery(m_serialPort,false);
}

void MainWindow::app_query_live_identity()
{
    if(!m_identityQueriesPending ||!m_connectionHasFrame ||!m_serial.isOpen() ||!m_pendingCommand.isEmpty()) return;
    QString command;
    if(!m_modeConfirmed) { if(m_modeReadback) return;command="MODE?"; }
    else if(!m_firmwareCurrent) command="STATUS?";
    else if(!m_mcuIdentityQueried) command="ID?";
    else if(m_effectiveCalibration<0) command="CAL?";
    else { m_identityQueriesPending=false;return; }
    if(m_identityQueryAttempts>=9) { m_identityQueriesPending=false;m_diagnostic="Live identity queries exhausted; unresolved fields remain unconfirmed";return; }
    if(app_send_command(command)) ++m_identityQueryAttempts;
}

void MainWindow::app_serial_tick()
{
    if(!m_running ||!m_recorder.app_session_active() ||m_inputMode!="SERIAL" ||m_stopping) {
        m_serialTimer->stop();m_recovery.stop();return;
    }
    const auto now=m_hostClock.elapsed();
    if(m_serial.isOpen() &&m_recovery.stale(now)) {
        m_state="STALE";m_connectionState="STALE";m_gapPending=true;
        m_diagnostic="No valid TH3 for 3.5 seconds; port/session retained, no measurements added";
        if(!app_record_connection_event("SERIAL_STALE")) return;
        app_render_state();app_write_status();
    }
    if(m_serial.isOpen() &&m_recovery.stream_expired(now)) {
        // A delayed GUI callback may have valid bytes queued. Drain first so a
        // host delay is not mislabeled a wire outage; only no valid TH3 expires.
        if(m_serial.bytesAvailable()>0) app_read_serial();
        if(m_running &&m_serial.isOpen() &&m_recovery.stream_expired(m_hostClock.elapsed())) {
            m_recovery.stream_lost(m_hostClock.elapsed());app_begin_configuration_recovery();
            m_lastSerialError="Host timeout: no valid TH3 for eight seconds";
            if(!app_record_connection_event("SERIAL_STREAM_TIMEOUT",{{"trigger","HOST_NO_VALID_TH3"},{"qt_error_code",QJsonValue()}})) return;
            app_release_serial_handle();
        }
    }
    if(m_recovery.due(now)) {
        const QString port=m_recovery.candidate(app_desktop_serial_devices(),now);
        if(!port.isEmpty()) app_open_serial_recovery(port,true);
        else {
            m_connectionState=m_recovery.state();m_state=m_connectionState;
            app_record_connection_event("SERIAL_RECOVERY_WAIT");app_render_state();app_write_status();
        }
    }
    if(m_running &&m_serial.isOpen()) { app_query_live_identity();app_restore_configuration_tick(); }
}

QString MainWindow::app_about_text() const
{
    QString observed="Not verified / Disconnected";
    if(!m_observedFirmware.isEmpty()) observed=(m_firmwareCurrent &&m_serial.isOpen()?"Observed ":m_serial.isOpen()?"Current connection unverified; last observed ":"Disconnected / last observed ")
        +m_observedFirmware+"\nObserved locally: "+app_ui_local_time(m_firmwareObservedAt)+"\nUTC evidence: "+m_firmwareObservedAt;
    return "TempHumi Monitor "+QCoreApplication::applicationVersion()+"\nTH3 | Qt "+QString(qVersion())
        +"\nApplication source: "+QString(APP_GIT_HEAD)+"\nMCU firmware: "+observed
        +"\nSensor accuracy is not verified.\nREAL, SIM and REPLAY remain explicitly classified.";
}
