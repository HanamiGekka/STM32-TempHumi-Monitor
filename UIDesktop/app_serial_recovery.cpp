#include "app_serial_recovery.h"
#include <QCryptographicHash>
#include <QRegularExpression>

QString app_mcu_identity_hash(const QString &uidHex,const QString &flashKiB,const QString &deviceId) {
    bool flashOk=false,idOk=false;const int flash=flashKiB.toInt(&flashOk),id=deviceId.toInt(&idOk);
    // ST ES096 Rev15 section2.2.2: DBGMCU_IDCODE reads zero in user mode.
    // Zero means this optional model diagnostic is unavailable, not a zero UID.
    // Identity still requires valid factory UID/capacity and the saved UID match.
    if(!QRegularExpression("^[0-9a-fA-F]{24}$").match(uidHex).hasMatch() ||!flashOk ||!idOk
       ||(flash!=64 &&flash!=128) ||(id!=0 &&id!=0x410)) return QString();
    const auto uid=QByteArray::fromHex(uidHex.toLatin1());
    if(uid==QByteArray(12,0) ||uid==QByteArray(12,char(255))
       ||(uid.left(4)==uid.mid(4,4) &&uid.left(4)==uid.mid(8,4))) return QString();
    return QString::fromLatin1(QCryptographicHash::hash(uid,QCryptographicHash::Sha256).toHex());
}

static const int APP_RECOVERY_MAX_ATTEMPTS=12;
static const qint64 APP_RECOVERY_INTERVAL_MS=5000;
static const qint64 APP_SERIAL_STALE_MS=3500;
static const qint64 APP_SERIAL_STREAM_TIMEOUT_MS=8000;

void app_serial_recovery_t::configure(bool enabled)
{
    m_enabled=enabled;
    if(!enabled &&m_active &&m_incident) wait_for_user("Automatic recovery disabled; reconnect the selected port manually");
}

void app_serial_recovery_t::start(const app_serial_device_t &device,qint64 now)
{
    m_expected=device;m_active=true;m_attempts=0;m_nextAttempt=0;m_incident=true;m_handleOpen=true;
    m_lastFrame=now;m_state="WAITING_DATA";m_reason.clear();m_lastError=0;
}
void app_serial_recovery_t::stop()
{
    m_active=false;m_nextAttempt=0;m_handleOpen=false;m_incident=false;
    m_state="DISCONNECTED";m_reason="Session ended; retries cancelled";
}
bool app_serial_recovery_t::automatic_identity() const
{
    return m_enabled &&app_desktop_identity_kind(m_expected)!="UNVERIFIED";
}
bool app_serial_recovery_t::trusted(const QVector<app_serial_device_t> &devices,const QString &port) const
{
    int compatible=0,matches=0;QString match;
    for(const auto &d:devices) {
        if(d.has_vendor &&d.has_product &&m_expected.has_vendor &&m_expected.has_product
           &&d.vendor==m_expected.vendor &&d.product==m_expected.product) ++compatible;
        if(app_desktop_identity_matches(m_expected,d)) { ++matches;match=d.port; }
    }
    return automatic_identity() &&compatible==1 &&matches==1 &&match==port;
}
void app_serial_recovery_t::wait_for_user(const QString &reason)
{
    m_state="WAITING_FOR_USER";m_reason=reason;m_nextAttempt=0;
}
void app_serial_recovery_t::schedule(qint64 now)
{
    m_incident=true;m_handleOpen=false;
    if(!automatic_identity()) {
        wait_for_user(m_enabled?"No trustworthy USB attachment identity; select a device manually":"Automatic recovery is off; reconnect the selected port manually");
    } else if(m_attempts>=APP_RECOVERY_MAX_ATTEMPTS) {
        wait_for_user("Twelve attempts exhausted; last Qt error "+QString::number(m_lastError)+". Resolve the cause and reconnect manually");
    } else {
        m_state=m_lastError==QSerialPort::PermissionError?"BUSY_OR_DENIED":"RECOVERING";
        m_nextAttempt=now+APP_RECOVERY_INTERVAL_MS;
    }
}
app_serial_recovery_t::action_t app_serial_recovery_t::error(QSerialPort::SerialPortError value,qint64 now)
{
    if(!m_active ||value==QSerialPort::NoError) return RECOVERY_IGNORE;
    if(value==QSerialPort::ParityError ||value==QSerialPort::FramingError
       ||value==QSerialPort::BreakConditionError ||value==QSerialPort::TimeoutError) return RECOVERY_KEEP_STREAM;
    m_lastError=int(value);
    if(value==QSerialPort::UnsupportedOperationError) {
        m_handleOpen=false;m_incident=true;wait_for_user("Unsupported port configuration; use UART Settings");
    } else schedule(now);
    return RECOVERY_RELEASE_HANDLE;
}
bool app_serial_recovery_t::stale(qint64 now)
{
    if(!m_active ||!m_handleOpen ||m_state=="STALE" ||m_state=="WAITING_FOR_USER" ||now-m_lastFrame<APP_SERIAL_STALE_MS) return false;
    m_state="STALE";m_reason="No valid TH3 frame for 3.5 seconds; no data fabricated";return true;
}
bool app_serial_recovery_t::stream_expired(qint64 now) const
{
    return m_active &&m_handleOpen &&m_state!="WAITING_FOR_USER" &&now-m_lastFrame>=APP_SERIAL_STREAM_TIMEOUT_MS;
}
void app_serial_recovery_t::stream_lost(qint64 now)
{
    m_lastError=0;m_reason="No valid TH3 for eight seconds; controlled handle recovery";schedule(now);
}
void app_serial_recovery_t::frame(qint64 now)
{
    if(!m_active) return;
    if(m_state=="WAITING_FOR_USER") { m_lastFrame=now;m_handleOpen=true;return; }
    m_lastFrame=now;m_handleOpen=true;m_nextAttempt=0;
    m_state=m_incident?"RECOVERING":"CONNECTED";
    m_reason=m_incident?"TH3 received; verifying live device/configuration":QString();
}
void app_serial_recovery_t::confirmed(qint64 now)
{
    if(!m_active ||!m_handleOpen) return;
    m_incident=false;m_state="CONNECTED";m_reason.clear();m_nextAttempt=0;
    // Only a complete live handshake ends an incident. One TH3 alone does not
    // replenish its budget; the next confirmed incident gets twelve attempts.
    Q_UNUSED(now);m_attempts=0;m_lastError=0;
}
void app_serial_recovery_t::verifying()
{
    m_incident=true;
    if(m_handleOpen &&m_state!="WAITING_FOR_USER") m_state="RECOVERING";
}
void app_serial_recovery_t::opened(qint64 now)
{
    if(!m_active) return;
    m_lastFrame=now;m_handleOpen=true;m_incident=true;m_state="WAITING_DATA";m_reason="Reopened; waiting for TH3 and live readback";m_nextAttempt=0;
}
bool app_serial_recovery_t::due(qint64 now) const
{
    return m_active &&!m_handleOpen &&m_nextAttempt>0 &&now>=m_nextAttempt;
}
QString app_serial_recovery_t::candidate(const QVector<app_serial_device_t> &devices,qint64 now)
{
    if(!due(now)) return QString();
    ++m_attempts;m_nextAttempt=0;QString port;int matches=0,compatible=0;
    for(const auto &d:devices) {
        if(d.has_vendor &&d.has_product &&d.vendor==m_expected.vendor &&d.product==m_expected.product) ++compatible;
        if(app_desktop_identity_matches(m_expected,d)) { port=d.port;++matches; }
    }
    if(compatible>1 ||matches>1) { wait_for_user("Multiple identical adapters or conflicting identity; manual selection required");return QString(); }
    if(matches==0) {
        if(compatible==1) wait_for_user("USB attachment/identity changed; manual selection required");
        else { m_reason="Expected USB attachment absent";schedule(now); }
        return QString();
    }
    return port;
}
void app_serial_recovery_t::open_failed(QSerialPort::SerialPortError value,qint64 now)
{
    m_lastError=int(value);
    if(value==QSerialPort::UnsupportedOperationError) { m_handleOpen=false;wait_for_user("Unsupported port settings; manual correction required"); }
    else schedule(now);
}
QJsonObject app_serial_recovery_t::snapshot() const
{
    // Attachment evidence is not a cryptographic MCU identity. Never serialize
    // the actual USB serial/PnP/path values into release or session metadata.
    return {{"state",m_state},{"reason",m_reason},{"active",m_active},{"automatic_enabled",m_enabled},
      {"automatic_identity_available",automatic_identity()},{"identity_kind",identity_kind()},
      {"attempts",m_attempts},{"maximum_attempts",APP_RECOVERY_MAX_ATTEMPTS},{"retry_interval_ms",double(APP_RECOVERY_INTERVAL_MS)},
      {"last_qt_error",m_lastError},{"next_attempt_host_ms",double(m_nextAttempt)},{"last_frame_host_ms",double(m_lastFrame)},
      {"stale_threshold_ms",double(APP_SERIAL_STALE_MS)},{"stream_timeout_ms",double(APP_SERIAL_STREAM_TIMEOUT_MS)}};
}
