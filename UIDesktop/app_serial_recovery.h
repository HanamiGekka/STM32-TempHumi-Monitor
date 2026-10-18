#ifndef APP_SERIAL_RECOVERY_H
#define APP_SERIAL_RECOVERY_H
#include "app_desktop_settings.h"
#include <QJsonObject>
#include <QSerialPort>
QString app_mcu_identity_hash(const QString &uidHex,const QString &flashKiB,const QString &deviceId);

// Pure policy: monotonic host milliseconds are supplied by the caller. It never
// opens a port, changes permissions, resets a board or invents a measurement.
class app_serial_recovery_t
{
public:
    enum action_t { RECOVERY_IGNORE, RECOVERY_KEEP_STREAM, RECOVERY_RELEASE_HANDLE };
    void start(const app_serial_device_t &device,qint64 now);
    void configure(bool enabled);
    void stop();
    action_t error(QSerialPort::SerialPortError error,qint64 now);
    bool stale(qint64 now);
    bool stream_expired(qint64 now) const;
    void stream_lost(qint64 now);
    void frame(qint64 now);
    void confirmed(qint64 now);
    void verifying();
    void wait_for_user(const QString &reason);
    bool due(qint64 now) const;
    QString candidate(const QVector<app_serial_device_t> &devices,qint64 now);
    void opened(qint64 now);
    void open_failed(QSerialPort::SerialPortError error,qint64 now);
    QString state() const { return m_state; }
    QJsonObject snapshot() const;
    bool automatic_identity() const;
    bool trusted(const QVector<app_serial_device_t> &devices,const QString &port) const;
    QString identity_kind() const { return app_desktop_identity_kind(m_expected); }
    int attempts() const { return m_attempts; }
private:
    void schedule(qint64 now);
    app_serial_device_t m_expected;
    QString m_state="DISCONNECTED",m_reason;
    bool m_active=false,m_enabled=false,m_incident=false,m_handleOpen=false;
    int m_lastError=0;
    int m_attempts=0;
    qint64 m_lastFrame=0,m_nextAttempt=0;
};
#endif
