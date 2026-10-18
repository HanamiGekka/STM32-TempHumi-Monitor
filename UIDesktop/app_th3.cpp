#include "app_th3.h"

#include <QRegularExpression>
#include <cmath>
#include <limits>

namespace {
// Consumer mirrors the TH3 contract in Core/Inc/app_measurement_source.h.
// These validity bits describe fields; unrelated PWM errors do not invalidate T/RH.
const quint16 APP_TH3_VALID_TEMPERATURE = 1U << 0;
const quint16 APP_TH3_VALID_HUMIDITY = 1U << 1;
const int APP_TH3_FIELD_COUNT = 23;
const int APP_TH3_MAX_LINE_BYTES = 64 * 1024;

static bool app_th3_parse_unsigned(const QString &text, quint64 maximum, quint64 *value)
{
    static const QRegularExpression decimal(QStringLiteral("^[0-9]+$"));
    if (!decimal.match(text).hasMatch()) return false;
    bool ok = false;
    const quint64 parsed = text.toULongLong(&ok, 10);
    if (!ok || parsed > maximum) return false;
    *value = parsed;
    return true;
}

static bool app_th3_parse_optional_real(const QString &text, app_th3_optional_real_t *value)
{
    if (text == QStringLiteral("NA")) return true;
    static const QRegularExpression decimal(QStringLiteral("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$"));
    if (!decimal.match(text).hasMatch()) return false;
    bool ok = false;
    const double parsed = text.toDouble(&ok);
    // Producer fields are float, but may carry finite diagnostic outliers.
    // No extra engineering range, clipping or conversion is introduced here.
    if (!ok || !std::isfinite(parsed) || std::abs(parsed) > std::numeric_limits<float>::max()) return false;
    value->present = true;
    value->value = parsed;
    return true;
}

static bool app_th3_parse_optional_code(const QString &text, app_th3_optional_code_t *value)
{
    if (text == QStringLiteral("NA")) return true;
    static const QRegularExpression decimal(QStringLiteral("^-?[0-9]+$"));
    if (!decimal.match(text).hasMatch()) return false;
    bool ok = false;
    const qlonglong parsed = text.toLongLong(&ok, 10);
    if (!ok || parsed < std::numeric_limits<qint16>::min() || parsed > std::numeric_limits<qint16>::max()) return false;
    value->present = true;
    value->value = static_cast<qint16>(parsed);
    return true;
}

static bool app_th3_parse_optional_ticks(const QString &text, app_th3_optional_ticks_t *value)
{
    if (text == QStringLiteral("NA")) return true;
    quint64 parsed = 0;
    if (!app_th3_parse_unsigned(text, std::numeric_limits<quint32>::max(), &parsed)) return false;
    value->present = true;
    value->value = static_cast<quint32>(parsed);
    return true;
}

static app_th3_frame_t app_th3_reject_frame(app_th3_frame_t frame, const QString &reason)
{
    frame.parse_status = APP_TH3_MALFORMED;
    frame.diagnostic = reason;
    return frame;
}
}

app_th3_frame_t app_th3_parse_line(const QByteArray &line)
{
    app_th3_frame_t frame;
    frame.raw_line = line;
    QByteArray text = line;
    if (text.endsWith('\n')) text.chop(1);
    if (text.endsWith('\r')) text.chop(1);
    if (text.startsWith("TH,") ||text.startsWith("TH2,") ||text=="TH2") {
        frame.protocol="UNSUPPORTED_LEGACY_PROTOCOL";
        frame.parse_status=APP_TH3_LEGACY;
        frame.diagnostic="UNSUPPORTED_LEGACY_PROTOCOL";
        return frame;
    }
    if (!text.startsWith("TH3,") &&text!="TH3") {
        frame.diagnostic="HUMAN_DIAGNOSTIC";
        return frame;
    }
    frame.tokens = QString::fromLatin1(text).split(',', Qt::KeepEmptyParts);
    if (line.size() > APP_TH3_MAX_LINE_BYTES) return app_th3_reject_frame(frame, QStringLiteral("LINE_TOO_LONG"));
    if (text.contains('\n') || text.contains('\r') || text.contains('\0')) return app_th3_reject_frame(frame, QStringLiteral("EMBEDDED_CONTROL_CHARACTER"));
    frame.protocol="TH3";
    if (frame.tokens.size() != APP_TH3_FIELD_COUNT) return app_th3_reject_frame(frame,"FIELD_COUNT_EXPECTED_23");
    for (const QString &token : frame.tokens) {
        if (token.isEmpty()) return app_th3_reject_frame(frame, QStringLiteral("EMPTY_FIELD"));
    }
    if (frame.tokens[1] == QStringLiteral("SIM")) frame.source = APP_TH3_SOURCE_SIM;
    else if (frame.tokens[1] == QStringLiteral("REAL")) frame.source = APP_TH3_SOURCE_REAL;
    else if (frame.tokens[1] == "MIXED") frame.source=APP_TH3_SOURCE_MIXED;
    else return app_th3_reject_frame(frame, QStringLiteral("SOURCE_EXPECTED_SIM_OR_REAL"));
    quint64 number = 0;
    if (!app_th3_parse_unsigned(frame.tokens[2], std::numeric_limits<quint32>::max(), &number)) return app_th3_reject_frame(frame, QStringLiteral("INVALID_TIMESTAMP_MS"));
    frame.timestamp_ms = static_cast<quint32>(number);
    if (!app_th3_parse_unsigned(frame.tokens[3], 1, &number)) return app_th3_reject_frame(frame, QStringLiteral("INVALID_VALID_BOOLEAN"));
    frame.valid = number != 0;
    quint16 *integers[] = {&frame.valid_flags, &frame.error_flags, &frame.calibration_id, &frame.sample_count};
    for (int index = 0; index < 4; ++index) {
        if (!app_th3_parse_unsigned(frame.tokens[4 + index], std::numeric_limits<quint16>::max(), &number)) return app_th3_reject_frame(frame, QStringLiteral("INVALID_UINT16_FIELD_%1").arg(5 + index));
        *integers[index] = static_cast<quint16>(number);
    }
    app_th3_optional_real_t *engineering[] = {&frame.temperature_c, &frame.humidity_rh, &frame.v_ntc, &frame.v_chs1101, &frame.rh_pwm, &frame.rh_adc};
    for (int index = 0; index < 6; ++index) {
        if (!app_th3_parse_optional_real(frame.tokens[8 + index], engineering[index])) return app_th3_reject_frame(frame, QStringLiteral("INVALID_FLOAT_FIELD_%1").arg(9 + index));
    }
    if (!app_th3_parse_optional_code(frame.tokens[14], &frame.raw_ntc_code)) return app_th3_reject_frame(frame, QStringLiteral("INVALID_RAW_NTC_CODE"));
    if (!app_th3_parse_optional_code(frame.tokens[15], &frame.raw_chs1101_code)) return app_th3_reject_frame(frame, QStringLiteral("INVALID_RAW_CHS1101_CODE"));
    if (!app_th3_parse_optional_ticks(frame.tokens[16], &frame.pwm_period_ticks)) return app_th3_reject_frame(frame, QStringLiteral("INVALID_PWM_PERIOD_TICKS"));
    if (!app_th3_parse_optional_ticks(frame.tokens[17], &frame.pwm_high_ticks)) return app_th3_reject_frame(frame, QStringLiteral("INVALID_PWM_HIGH_TICKS"));
    // Existing SIM has no physical acquisition. Numeric physical fields would misrepresent its origin.
    {
        auto source=[](const QString &token) { return token=="REAL"?APP_TH3_SOURCE_REAL:(token=="SIM"?APP_TH3_SOURCE_SIM:APP_TH3_SOURCE_UNKNOWN); };
        frame.temperature_source=source(frame.tokens[18]); frame.humidity_source=source(frame.tokens[19]);
        if(frame.temperature_source==APP_TH3_SOURCE_UNKNOWN || frame.humidity_source==APP_TH3_SOURCE_UNKNOWN)
            return app_th3_reject_frame(frame,"INVALID_CHANNEL_SOURCE");
        const auto overall=frame.temperature_source==frame.humidity_source?frame.temperature_source:APP_TH3_SOURCE_MIXED;
        if(frame.source!=overall) return app_th3_reject_frame(frame,"INCONSISTENT_CHANNEL_SOURCE");
        if(!app_th3_parse_unsigned(frame.tokens[20],1,&number)) return app_th3_reject_frame(frame,"INVALID_TEMPERATURE_VALID");
        frame.temperature_valid=number!=0;
        if(!app_th3_parse_unsigned(frame.tokens[21],1,&number)) return app_th3_reject_frame(frame,"INVALID_HUMIDITY_VALID");
        frame.humidity_valid=number!=0;
        if(!app_th3_parse_unsigned(frame.tokens[22],std::numeric_limits<quint32>::max(),&number)) return app_th3_reject_frame(frame,"INVALID_MODE_GENERATION");
        frame.mode_generation=quint32(number);
        if(frame.temperature_valid!=((frame.valid_flags&1)!=0 && frame.temperature_c.present)
           || frame.humidity_valid!=((frame.valid_flags&2)!=0 && frame.humidity_rh.present)
           ||frame.valid!=(frame.temperature_valid&&frame.humidity_valid)) return app_th3_reject_frame(frame,"INCONSISTENT_CHANNEL_VALIDITY");
        if(frame.temperature_source==APP_TH3_SOURCE_SIM && (frame.tokens[10]!="NA" || frame.tokens[14]!="NA" || (frame.valid_flags&(4|64))!=0))
            return app_th3_reject_frame(frame,"SIM_TEMPERATURE_PHYSICAL_FIELDS");
        if(frame.humidity_source==APP_TH3_SOURCE_SIM) {
            for(int index : {11,12,13,15,16,17}) if(frame.tokens[index]!="NA") return app_th3_reject_frame(frame,"SIM_HUMIDITY_PHYSICAL_FIELDS");
            if((frame.valid_flags&(8|16|32|128|256))!=0) return app_th3_reject_frame(frame,"SIM_HUMIDITY_PHYSICAL_FLAGS");
        }
    }
    frame.parse_status = APP_TH3_PARSED;
    return frame;
}

QString app_th3_source_text(app_th3_source_t source)
{
    switch (source) {
    case APP_TH3_SOURCE_REAL: return QStringLiteral("REAL");
    case APP_TH3_SOURCE_SIM: return QStringLiteral("SIM");
    case APP_TH3_SOURCE_MIXED: return QStringLiteral("MIXED");
    default: return QStringLiteral("UNKNOWN");
    }
}

QString app_th3_parse_status_text(app_th3_parse_status_t status)
{
    switch (status) {
    case APP_TH3_PARSED: return QStringLiteral("PARSED");
    case APP_TH3_MALFORMED: return QStringLiteral("MALFORMED");
    case APP_TH3_LEGACY: return QStringLiteral("LEGACY");
    default: return QStringLiteral("DIAGNOSTIC");
    }
}

bool app_th3_temperature_usable(const app_th3_frame_t &frame)
{
    return frame.parse_status == APP_TH3_PARSED && frame.temperature_valid && (frame.valid_flags & APP_TH3_VALID_TEMPERATURE) != 0 && frame.temperature_c.present;
}

bool app_th3_humidity_usable(const app_th3_frame_t &frame)
{
    return frame.parse_status == APP_TH3_PARSED && frame.humidity_valid && (frame.valid_flags & APP_TH3_VALID_HUMIDITY) != 0 && frame.humidity_rh.present;
}

void app_th3_timeline_t::app_th3_reset_segment(const QString &reason)
{
    m_active = false;
    m_pending_reason = reason.isEmpty() ? QStringLiteral("RESET") : reason;
}

app_th3_time_t app_th3_timeline_t::app_th3_append_frame(const app_th3_frame_t &frame)
{
    app_th3_time_t result;
    if (frame.parse_status != APP_TH3_PARSED) return result;
    QString reason = m_pending_reason;
    quint32 delta_ms = 0;
    if (m_active && (frame.source != m_source || frame.temperature_source!=m_temperature_source
        || frame.humidity_source!=m_humidity_source || frame.mode_generation!=m_generation)) {
        m_active = false;
        reason = QStringLiteral("SOURCE_CHANGE");
    }
    if (m_active) {
        delta_ms = frame.timestamp_ms - m_previous_timestamp_ms;
        // Serial-number ordering disambiguates wrap from reset for gaps below 2^31 ms.
        // A gap of 24.85 days or greater is ambiguous and starts a new segment.
        if (delta_ms >= 0x80000000U) {
            m_active = false;
            reason = QStringLiteral("TIMESTAMP_ROLLBACK");
        }
    }
    if (!m_active) {
        ++m_segment_id;
        m_unwrapped_timestamp_ms = frame.timestamp_ms;
        m_start_timestamp_ms = m_unwrapped_timestamp_ms;
        m_source = frame.source;
        m_temperature_source=frame.temperature_source; m_humidity_source=frame.humidity_source; m_generation=frame.mode_generation;
        m_active = true;
        result.new_segment = true;
        result.reason = reason;
    } else {
        m_unwrapped_timestamp_ms += delta_ms;
    }
    m_pending_reason.clear();
    m_previous_timestamp_ms = frame.timestamp_ms;
    result.segment_id = m_segment_id;
    result.unwrapped_timestamp_ms = m_unwrapped_timestamp_ms;
    result.elapsed_s = static_cast<double>(m_unwrapped_timestamp_ms - m_start_timestamp_ms) / 1000.0;
    return result;
}
