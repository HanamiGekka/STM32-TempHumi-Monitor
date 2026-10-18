#ifndef APP_TH3_H
#define APP_TH3_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QtGlobal>

enum app_th3_source_t { APP_TH3_SOURCE_UNKNOWN, APP_TH3_SOURCE_REAL, APP_TH3_SOURCE_SIM, APP_TH3_SOURCE_MIXED };
enum app_th3_parse_status_t { APP_TH3_PARSED, APP_TH3_MALFORMED, APP_TH3_LEGACY, APP_TH3_DIAGNOSTIC };

// Presence is independent of the stored value. Missing TH3 fields never mean measured zero.
struct app_th3_optional_real_t { bool present = false; double value = 0.0; };
struct app_th3_optional_code_t { bool present = false; qint16 value = 0; };
struct app_th3_optional_ticks_t { bool present = false; quint32 value = 0; };

struct app_th3_frame_t
{
    QByteArray raw_line; // Exact received bytes including the original line terminator.
    QStringList tokens; // Original CSV tokens without the terminator; also retained on errors.
    app_th3_parse_status_t parse_status = APP_TH3_DIAGNOSTIC;
    QString diagnostic;
    app_th3_source_t source = APP_TH3_SOURCE_UNKNOWN;
    QString protocol = "TH3";
    app_th3_source_t temperature_source=APP_TH3_SOURCE_UNKNOWN,humidity_source=APP_TH3_SOURCE_UNKNOWN;
    bool temperature_valid=false,humidity_valid=false;
    quint32 mode_generation=0;
    quint32 timestamp_ms = 0;
    bool valid = false;
    quint16 valid_flags = 0;
    quint16 error_flags = 0;
    quint16 calibration_id = 0;
    quint16 sample_count = 0;
    app_th3_optional_real_t temperature_c, humidity_rh, v_ntc, v_chs1101, rh_pwm, rh_adc;
    app_th3_optional_code_t raw_ntc_code, raw_chs1101_code;
    app_th3_optional_ticks_t pwm_period_ticks, pwm_high_ticks;
    // Supplemental MCU diagnostic evidence, not extra TH3 wire fields.
    app_th3_optional_real_t physical_temperature_c;
    int correction_profile=-1;
    QString correction_pair_status="UNAVAILABLE";
};

app_th3_frame_t app_th3_parse_line(const QByteArray &line);
QString app_th3_source_text(app_th3_source_t source);
QString app_th3_parse_status_text(app_th3_parse_status_t status);
bool app_th3_temperature_usable(const app_th3_frame_t &frame);
bool app_th3_humidity_usable(const app_th3_frame_t &frame);

struct app_th3_time_t
{
    quint64 segment_id = 0;
    quint64 unwrapped_timestamp_ms = 0;
    double elapsed_s = 0.0;
    bool new_segment = false;
    QString reason;
};

class app_th3_timeline_t
{
public:
    app_th3_time_t app_th3_append_frame(const app_th3_frame_t &frame);
    void app_th3_reset_segment(const QString &reason);
private:
    bool m_active = false;
    quint64 m_segment_id = 0;
    quint32 m_previous_timestamp_ms = 0;
    quint64 m_unwrapped_timestamp_ms = 0;
    quint64 m_start_timestamp_ms = 0;
    app_th3_source_t m_source = APP_TH3_SOURCE_UNKNOWN;
    app_th3_source_t m_temperature_source=APP_TH3_SOURCE_UNKNOWN,m_humidity_source=APP_TH3_SOURCE_UNKNOWN;
    quint32 m_generation=0;
    QString m_pending_reason = QStringLiteral("START");
};

#endif
