#include "mainwindow.h"
#include <QDateTime>

bool MainWindow::app_live_configuration_confirmed() const
{
    return m_running &&m_inputMode=="SERIAL" &&m_serial.isOpen() &&m_connectionHasFrame
        &&m_modeConfirmed &&m_firmwareCurrent &&m_calibrationConfirmed &&m_effectiveCalibration>=0
        &&m_lastFrameHostMs>=0 &&m_hostClock.elapsed()-m_lastFrameHostMs<3500;
}
void MainWindow::app_set_recovery_options(bool automatic,bool restoreConfirmed,bool persist)
{
    m_autoRecovery=automatic;m_restoreConfirmedOption=automatic &&restoreConfirmed;
    m_recovery.configure(automatic);
    if(persist) m_recoveryMigrationNotice=false;
    if(!m_restoreConfirmedOption &&m_restorePending) { m_restoreState="DISABLED";m_restoreReason="Configuration restore is off; live state is displayed"; }
    if(persist) {
        const auto settings=app_desktop_open_settings();
        settings->setValue("serial/recoveryPolicyVersion",2);
        settings->setValue("serial/recoveryPreferenceOrigin","explicit_user_choice");
        settings->setValue("serial/recoveryMigrationNoticePending",false);
        settings->setValue("serial/automaticRecovery",automatic);
        settings->setValue("serial/restoreConfirmedConfiguration",m_restoreConfirmedOption);settings->sync();
    }
    if(m_running) app_persist_mode_state();
    app_render_state();app_write_status();
}
void MainWindow::app_remember_confirmed_configuration()
{
    if(m_restorePending ||!app_live_configuration_confirmed() ||!m_mcuIdentityCurrent) return;
    // A combo edit/request/ACK is not effective configuration. Only current
    // MODE/CAL readback and matching TH3 evidence enter this snapshot.
    m_savedConfiguration={{"temperature_source",m_frameTemperature},{"humidity_source",m_frameHumidity},
      {"profile",m_effectiveCalibration},{"firmware",m_observedFirmware},{"mcu_identity_sha256",m_mcuIdentityHash},
      {"generation",double(m_frameGeneration)},{"confirmed_at",m_frameReceivedAt}};
    const auto settings=app_desktop_open_settings();const auto previous=settings->value("serial/confirmedConfiguration").toMap();
    bool changed=false;
    for(const auto &key:{"temperature_source","humidity_source","profile","firmware","mcu_identity_sha256"})
        if(previous.value(key)!=m_savedConfiguration.value(key).toVariant()) changed=true;
    if(changed) {settings->setValue("serial/confirmedConfiguration",m_savedConfiguration.toVariantMap());settings->sync();}
}
void MainWindow::app_begin_configuration_recovery()
{
    if(!m_restorePending) m_restoreTarget=m_savedConfiguration;
    m_recovery.verifying();
    m_restorePending=true;m_restoreState="WAITING_LIVE_READBACK";m_restoreReason="Read TH3, MODE, STATUS and CAL before any change";
    m_restoreSetIssued=m_restoreCalIssued=false;m_restoreDeadline=0;
}
QJsonObject MainWindow::app_restore_snapshot() const
{
    return {{"automatic_recovery_enabled",m_autoRecovery},{"restore_confirmed_enabled",m_restoreConfirmedOption},
      {"state",m_restoreState},{"reason",m_restoreReason},{"pending",m_restorePending},
      {"last_confirmed",m_savedConfiguration},{"recovery_target",m_restoreTarget},
      {"set_issued",m_restoreSetIssued},{"cal_issued",m_restoreCalIssued}};
}
void MainWindow::app_restore_configuration_tick()
{
    if(!m_running ||m_stopping ||m_inputMode!="SERIAL" ||!m_serial.isOpen() ||!m_connectionHasFrame) return;
    const auto now=m_hostClock.elapsed();
    if(!m_restorePending) {
        if(m_recovery.state()=="WAITING_FOR_USER") return;
        if(m_mcuIdentityQueried &&!m_mcuIdentityCurrent &&app_live_configuration_confirmed()) {
            m_restoreState="UNSAFE_IDENTITY";m_restoreReason=m_mcuIdentityError;
            m_recovery.wait_for_user(m_mcuIdentityError);m_connectionState=m_recovery.state();
            app_record_connection_event("MCU_IDENTITY_BLOCKED",{{"reason",m_mcuIdentityError}});
            app_render_state();app_write_status();return;
        }
        if(app_live_configuration_confirmed() &&m_mcuIdentityCurrent) {
            const bool newlyConfirmed=m_recovery.state()!="CONNECTED";
            m_recovery.confirmed(now);app_remember_confirmed_configuration();
            m_connectionState=m_recovery.state()+" / "+m_state;
            if(newlyConfirmed) {app_render_state();app_write_status();}
        }
        return;
    }
    if(m_restoreDeadline==0) m_restoreDeadline=now+20000;
    const auto finish=[this,now](const QString &state,const QString &reason) {
        m_restorePending=false;m_restoreState=state;m_restoreReason=reason;
        if(state=="UNSAFE_IDENTITY") m_recovery.wait_for_user(reason);
        else if(app_live_configuration_confirmed() &&m_mcuIdentityCurrent) m_recovery.confirmed(now);
        else { m_recovery.wait_for_user(reason);m_identityQueriesPending=false; }
        m_connectionState=m_recovery.state()+" / "+m_state;
        app_remember_confirmed_configuration();
        app_record_connection_event(state=="RESTORED"?"CONFIGURATION_RESTORED":"CONFIGURATION_RESTORE_RESULT",app_restore_snapshot());
        app_render_state();app_write_status();
    };
    if(now>m_restoreDeadline) { finish("FAILED","Live configuration/restore was not confirmed within20 seconds; no success inferred");return; }
    if(!app_live_configuration_confirmed() ||!m_pendingCommand.isEmpty()) return;
    if(!m_restoreConfirmedOption) { finish("DISABLED","Automatic configuration restore is off; actual readback retained");return; }
    if(m_restoreTarget.isEmpty()) { finish("NO_CONFIRMED_CONFIGURATION","No earlier readback+TH3 configuration is available");return; }
    const auto devices=app_desktop_serial_devices();
    bool sameAttachment=false;
    for(const auto &d:devices) if(d.port==m_serialPort) sameAttachment=app_desktop_identity_matches(m_trustedDevice,d);
    if(!sameAttachment ||!m_recovery.trusted(devices,m_serialPort)) {
        finish("UNSAFE_IDENTITY","Adapter identity/location is absent or ambiguous; no SET/CAL sent");return;
    }
    if(!m_mcuIdentityCurrent ||m_restoreTarget["mcu_identity_sha256"].toString()!=m_mcuIdentityHash) {
        finish("UNSAFE_IDENTITY","Fresh MCU UID does not match the saved confirmed board; no SET/CAL sent");return;
    }
    if(m_restoreTarget["firmware"].toString()!=m_observedFirmware) {
        finish("UNSAFE_IDENTITY","Observed firmware differs from the confirmed peer; no SET/CAL sent");return;
    }
    const QString temperature=m_restoreTarget["temperature_source"].toString(),humidity=m_restoreTarget["humidity_source"].toString();
    const int profile=m_restoreTarget["profile"].toInt(-1);
    if((temperature!="REAL" &&temperature!="SIM") ||(humidity!="REAL" &&humidity!="SIM") ||profile<0 ||profile>3) {
        finish("FAILED","Confirmed recovery snapshot is invalid; no command sent");return;
    }
    if(m_frameTemperature!=temperature ||m_frameHumidity!=humidity) {
        if(m_restoreSetIssued) { finish("FAILED","Source readback did not reach the saved state; no repeated SET");return; }
        m_autoRestoreCommand=true;const bool sent=app_request_sources(temperature,humidity);m_autoRestoreCommand=false;
        if(!sent) { finish("FAILED","Saved source request could not be submitted");return; }
        m_restoreSetIssued=true;m_restoreState="RESTORING_SOURCES";m_restoreReason="Awaiting ACK, MODE and matching TH3";
        app_record_connection_event("CONFIGURATION_RESTORE_SET",{{"temperature_source",temperature},{"humidity_source",humidity}});return;
    }
    if(m_effectiveCalibration!=profile) {
        if(m_restoreCalIssued) { finish("FAILED","Fit readback did not reach the saved state; no repeated CAL");return; }
        m_autoRestoreCommand=true;const bool sent=app_request_calibration(profile);m_autoRestoreCommand=false;
        if(!sent) { finish("FAILED","Saved fit request could not be submitted");return; }
        m_restoreCalIssued=true;m_restoreState="RESTORING_FIT";m_restoreReason="Awaiting ACK, CAL readback and matching TH3";
        app_record_connection_event("CONFIGURATION_RESTORE_CAL",{{"profile",profile}});return;
    }
    finish("RESTORED","Saved sources and fit confirmed by live readback and TH3; display choices retained");
}
