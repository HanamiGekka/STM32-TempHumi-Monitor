#include "app_session_recorder.h"
#include <QCoreApplication>

#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStringList>
#include <QThread>
#include <QUuid>
#include <QFileInfo>

static QString app_record_escape_csv(const QString &value)
{
    QString escaped = value;
    escaped.replace('"', "\"\"");
    return '"' + escaped + '"';
}

static QByteArray app_record_format_csv(const QStringList &fields)
{
    QStringList escaped;
    for (const QString &field : fields) escaped.append(app_record_escape_csv(field));
    return (escaped.join(',') + "\r\n").toUtf8();
}

static QString app_record_wall_time(const QDateTime &value)
{
    return value.toUTC().toString(Qt::ISODateWithMs);
}

static QString app_record_text_line(const QByteArray &bytes)
{
    QByteArray text = bytes;
    if (text.endsWith('\n')) text.chop(1);
    if (text.endsWith('\r')) text.chop(1);
    return QString::fromUtf8(text);
}

app_session_recorder_t::~app_session_recorder_t()
{
    if (m_active) app_stop_session("RECORDER_DESTROYED");
}

bool app_session_recorder_t::app_record_control_state(const QJsonObject &state)
{
    m_control_state=state;
    if(!m_active) return true;
    if(!app_write_metadata()) return app_fail_session(m_error,false);
    return true;
}

bool app_session_recorder_t::app_record_display_event(const QJsonObject &event)
{
    if(!m_active) return false;
    QJsonObject identified=event;
    identified["session_id"]=QFileInfo(m_directory).fileName();
    identified["event_index"]=double(m_event_count+1);
    const QByteArray bytes=QJsonDocument(identified).toJson(QJsonDocument::Compact)+'\n';
    if(m_events.write(bytes)!=bytes.size() ||!m_events.flush())
        return app_fail_session("Display event write/flush failed: "+m_events.errorString());
    ++m_event_count;
    if(!app_write_metadata()) return app_fail_session(m_error,false);
    return true;
}

bool app_session_recorder_t::app_fail_session(const QString &error, bool persist_metadata)
{
    m_error = error;
    m_active = false;
    m_end_utc = QDateTime::currentDateTimeUtc();
    m_stop_reason = "RECORDING_ERROR";
    m_raw.close();
    m_csv.close();
    m_events.close();
    // A best-effort error metadata write cannot make the original I/O failure pass.
    if (persist_metadata) app_write_metadata();
    return false;
}

bool app_session_recorder_t::app_start_session(const app_session_options_t &options)
{
    if (m_active && !app_stop_session("NEW_SESSION")) return false;
    m_error.clear();
    if (options.input_mode != "FILE_STREAM" && options.input_mode != "REPLAY" && options.input_mode != "SERIAL") {
        m_error = "Unsupported input mode";
        return false;
    }
    if (options.classification != "SIMULATED" && options.classification != "REPLAY"
            && options.classification != "FIXTURE" && options.classification != "REPLAY_FIXTURE"
            && !(options.input_mode == "SERIAL" && options.classification == "SERIAL")) {
        m_error = "Explicit SIMULATED, REPLAY or FIXTURE classification required";
        return false;
    }
    if (options.output_directory.isEmpty() || !QDir().mkpath(options.output_directory)) {
        m_error = "Cannot create recording output directory: " + options.output_directory;
        return false;
    }
    m_options = options;
    m_start_utc = QDateTime::currentDateTimeUtc();
    m_end_utc = QDateTime();
    m_last_metadata_utc = QDateTime();
    m_stop_reason.clear();
    m_raw_line_count = m_raw_record_count = m_raw_fragment_count = m_raw_byte_count = 0;
    m_csv_row_count = m_parsed_count = m_diagnostic_count = m_legacy_count = 0;
    m_th3_count=0;
    m_event_count=0;
    m_malformed_count = m_invalid_count = 0;
    m_metadata_retry_count = m_metadata_recovered_count = 0;
    m_metadata_max_write_ms = 0;
    m_segment_ids.clear();
    m_observed_classifications.clear();
    m_observed_protocols.clear(); m_last_channels=QJsonObject();
    m_control_state=QJsonObject();
    m_segments = QJsonArray();
    const QString name = "session_" + m_start_utc.toString("yyyyMMdd_HHmmss_zzz")
            + "_" + QUuid::createUuid().toString(QUuid::Id128).left(12);
    m_directory = QDir(options.output_directory).absoluteFilePath(name);
    if (!QDir().mkdir(m_directory)) {
        m_error = "Cannot create unique session directory: " + m_directory;
        return false;
    }
    m_raw.setFileName(m_directory + "/raw_input.log");
    m_csv.setFileName(m_directory + "/parsed.csv");
    m_events.setFileName(m_directory + "/session_events.jsonl");
    if (!m_raw.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return app_fail_session("Cannot open raw log: " + m_raw.errorString());
    if (!m_csv.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return app_fail_session("Cannot open CSV: " + m_csv.errorString());
    if (!m_events.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return app_fail_session("Cannot open display events: " + m_events.errorString());
    const QStringList header = {
        "raw_record_index", "raw_byte_offset", "record_kind", "segment_id", "classification",
        "host_receive_time", "input_mode", "frame_source", "source_timestamp_ms",
        "session_elapsed_s", "unwrapped_timestamp_ms", "valid", "valid_flags", "error_flags",
        "calibration_id", "sample_count", "temperature_c", "humidity_rh", "v_ntc",
        "v_chs1101", "rh_pwm", "rh_adc", "raw_ntc_code", "raw_chs1101_code",
        "pwm_period_ticks", "pwm_high_ticks", "parse_status", "diagnostic", "raw_line",
        "frame_protocol", "temperature_source", "humidity_source", "temperature_valid", "humidity_valid", "mode_generation",
        "physical_temperature_c", "correction_profile", "correction_pair_status"
    };
    const QByteArray header_bytes = app_record_format_csv(header);
    if (m_csv.write(header_bytes) != header_bytes.size() || !m_csv.flush())
        return app_fail_session("Cannot write CSV header: " + m_csv.errorString());
    m_active = true;
    if (!app_write_metadata()) return app_fail_session(m_error, false);
    return true;
}

QString app_session_recorder_t::app_classify_frame(const app_th3_frame_t &frame) const
{
    if (m_options.input_mode == "SERIAL") return "SERIAL_" + app_th3_source_text(frame.source);
    if (frame.source == APP_TH3_SOURCE_REAL || frame.source==APP_TH3_SOURCE_MIXED) return "REPLAY_FIXTURE";
    return m_options.classification;
}

bool app_session_recorder_t::app_record_line(const app_th3_frame_t &frame,
                                             const app_th3_time_t &time,
                                             const QDateTime &received_utc)
{
    if (!m_active) {
        if (m_error.isEmpty()) m_error = "Recording is not active";
        return false;
    }
    const bool parsed = frame.parse_status == APP_TH3_PARSED;
    if (parsed && frame.tokens.size() != 23)
        return app_fail_session("Parsed frame lacks the exact source tokens");
    const quint64 byte_offset = m_raw_byte_count;
    if (m_raw.write(frame.raw_line) != frame.raw_line.size() || !m_raw.flush())
        return app_fail_session("Raw write/flush failed: " + m_raw.errorString());
    ++m_raw_record_count;
    m_raw_byte_count += static_cast<quint64>(frame.raw_line.size());
    m_raw_line_count += static_cast<quint64>(frame.raw_line.count('\n'));
    const bool complete_line = frame.raw_line.endsWith('\n');
    if (!complete_line) ++m_raw_fragment_count;
    const QString classification = app_classify_frame(frame);
    m_observed_classifications.insert(classification);
    QStringList row = {
        QString::number(m_raw_record_count), QString::number(byte_offset),
        complete_line ? "LINE" : "FRAGMENT", QString::number(time.segment_id), classification,
        app_record_wall_time(received_utc), m_options.input_mode,
        app_th3_source_text(frame.source),
        parsed ? frame.tokens.at(2) : "NA",
        parsed ? QString::number(time.elapsed_s, 'f', 6) : "NA",
        parsed ? QString::number(time.unwrapped_timestamp_ms) : "NA"
    };
    // Retain validated producer lexemes rather than reformatting floats or inventing
    // numbers for malformed/diagnostic text. NA stays NA even for valid=0 records.
    for (int index = 3; index < 18; ++index) row.append(parsed ? frame.tokens.at(index) : "NA");
    row.append(app_th3_parse_status_text(frame.parse_status));
    row.append(frame.diagnostic);
    row.append(app_record_text_line(frame.raw_line));
    row.append(parsed?frame.protocol:"NA");
    row.append(parsed?app_th3_source_text(frame.temperature_source):"NA");
    row.append(parsed?app_th3_source_text(frame.humidity_source):"NA");
    row.append(parsed?(frame.temperature_valid?"1":"0"):"NA");
    row.append(parsed?(frame.humidity_valid?"1":"0"):"NA");
    row.append(parsed?QString::number(frame.mode_generation):"NA");
    row.append(parsed&&frame.physical_temperature_c.present?QString::number(frame.physical_temperature_c.value,'g',17):"NA");
    row.append(parsed&&frame.correction_profile>=0?QString::number(frame.correction_profile):"NA");
    row.append(parsed?frame.correction_pair_status:"NA");
    const QByteArray csv_bytes = app_record_format_csv(row);
    if (m_csv.write(csv_bytes) != csv_bytes.size() || !m_csv.flush())
        return app_fail_session("CSV write/flush failed: " + m_csv.errorString());
    ++m_csv_row_count;
    if (parsed) {
        if(frame.protocol=="TH3") ++m_th3_count;
        m_observed_protocols.insert(frame.protocol);
        m_last_channels["temperature_source"]=app_th3_source_text(frame.temperature_source);
        m_last_channels["humidity_source"]=app_th3_source_text(frame.humidity_source);
        m_last_channels["temperature_valid"]=frame.temperature_valid;
        m_last_channels["humidity_valid"]=frame.humidity_valid;
        m_last_channels["mode_generation"]=double(frame.mode_generation);
        ++m_parsed_count;
        // Match UI validity: a claimed valid flag cannot make a missing T/RH
        // value usable. Unrelated errors such as PWM_NO_SAMPLE remain recorded.
        if (!app_th3_temperature_usable(frame) || !app_th3_humidity_usable(frame)) ++m_invalid_count;
        if (!m_segment_ids.contains(time.segment_id)) {
            m_segment_ids.insert(time.segment_id);
            QJsonObject segment;
            segment["segment_id"] = static_cast<double>(time.segment_id);
            segment["reason"] = time.reason.isEmpty() ? "FIRST_FRAME" : time.reason;
            segment["host_receive_time"] = app_record_wall_time(received_utc);
            segment["first_source_timestamp_ms"] = static_cast<double>(frame.timestamp_ms);
            segment["first_raw_record_index"] = static_cast<double>(m_raw_record_count);
            m_segments.append(segment);
        }
    } else if (frame.parse_status == APP_TH3_MALFORMED) {
        ++m_malformed_count;
    } else if (frame.parse_status == APP_TH3_LEGACY) {
        ++m_legacy_count;
    } else {
        ++m_diagnostic_count;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!m_last_metadata_utc.isValid() || m_last_metadata_utc.msecsTo(now) >= 1000) {
        if (!app_write_metadata()) return app_fail_session(m_error, false);
    }
    return true;
}

QJsonObject app_session_recorder_t::app_session_summary() const
{
    QJsonObject metadata;
    metadata["metadata_version"] = 1;
    metadata["application_id"]="org.temphumi.monitor";
    metadata["session_id"]=QFileInfo(m_directory).fileName();
    metadata["session_start_wall_time"] = app_record_wall_time(m_start_utc);
    metadata["session_end_wall_time"] = m_end_utc.isValid() ? app_record_wall_time(m_end_utc) : QString();
    metadata["input_mode"] = m_options.input_mode;
    metadata["input_path"] = m_options.input_path;
    metadata["git_head"] = m_options.git_head;
    metadata["qt_build_identity"] = m_options.qt_build_identity;
    metadata["software_version"] = m_options.software_version.isEmpty()
            ? QCoreApplication::applicationVersion() : m_options.software_version;
    metadata["qt_version"] = QT_VERSION_STR;
    metadata["protocol"] = "TH3";
    metadata["channels"]=m_last_channels;
    metadata["control_state"]=m_control_state;
    metadata["csv_schema"]="TH3: original columns plus protocol/channel source/channel valid/generation";
    metadata["csv_schema_version"]=3;
    metadata["temperature_evidence"]="MCU float32 sidecar matched to TH3 timestamp/cal/value; missing stays NA. physical_temperature_c is the pre-correction mean, not latest ADC conversion. No Qt calibration.";
    metadata["configured_classification"] = m_options.classification;
    metadata["classification"] = m_observed_classifications.contains("REPLAY_FIXTURE")
            ? (m_observed_classifications.size() > 1 ? "MIXED_REPLAY_FIXTURE" : "REPLAY_FIXTURE")
            : m_options.classification;
    QJsonArray classifications;
    QStringList observed = m_observed_classifications.values();
    observed.sort();
    for (const QString &classification : observed) classifications.append(classification);
    metadata["observed_classifications"] = classifications;
    metadata["hardware_status"] = m_options.input_mode == "SERIAL"
        ? (m_raw_byte_count>0 ? "UART OBSERVED; SENSOR ACCURACY NOT VERIFIED" : "UART NOT OBSERVED; SENSOR ACCURACY NOT VERIFIED")
        : "NOT TESTED ON HARDWARE";
    metadata["active"] = m_active;
    metadata["stop_reason"] = m_stop_reason;
    metadata["recording_error"] = m_error;
    metadata["metadata_retry_count"] = static_cast<double>(m_metadata_retry_count);
    metadata["metadata_recovered_write_count"] = static_cast<double>(m_metadata_recovered_count);
    metadata["metadata_max_write_ms"] = static_cast<double>(m_metadata_max_write_ms);
    metadata["metadata_retry_policy"] = "Windows atomic open/commit only: 150 ms bounded retry; write errors fail immediately; no direct-write fallback";
    metadata["raw_line_count"] = static_cast<double>(m_raw_line_count);
    metadata["raw_record_count"] = static_cast<double>(m_raw_record_count);
    metadata["raw_fragment_count"] = static_cast<double>(m_raw_fragment_count);
    metadata["raw_byte_count"] = static_cast<double>(m_raw_byte_count);
    metadata["csv_row_count"] = static_cast<double>(m_csv_row_count);
    metadata["csv_parsed_count"] = static_cast<double>(m_parsed_count);
    metadata["parsed_th2_count"] = 0; // Stable schema-2 column retained for old readers; never parsed now.
    metadata["parsed_th3_count"] = static_cast<double>(m_th3_count);
    metadata["parsed_total_count"] = static_cast<double>(m_parsed_count);
    metadata["ignored_diagnostic_count"] = static_cast<double>(m_diagnostic_count);
    metadata["legacy_count"] = static_cast<double>(m_legacy_count);
    metadata["malformed_count"] = static_cast<double>(m_malformed_count);
    metadata["invalid_frame_count"] = static_cast<double>(m_invalid_count);
    metadata["invalid_frame_rule"] = "At least one canonical T/RH field is unusable: valid, valid_flags and NA are checked; error_flags alone do not invalidate T/RH";
    metadata["raw_line_count_rule"] = "Number of LF terminators in exact raw bytes; oversized fragments are counted separately as records";
    metadata["csv_row_count_rule"] = "One classified CSV row per raw record, including diagnostics, legacy lines, malformed lines and oversized fragments";
    metadata["segment_count"] = m_segments.size();
    metadata["restart_count"] = qMax(0, m_segments.size() - 1);
    metadata["restart_count_rule"] = "Within this recorder session: segment_count minus one; window Stop/Start creates independent recorder sessions";
    metadata["segments"] = m_segments;
    metadata["raw_file"] = "raw_input.log";
    metadata["csv_file"] = "parsed.csv";
    metadata["event_file"]="session_events.jsonl";
    metadata["event_schema_version"]=1;
    metadata["event_count"]=double(m_event_count);
    return metadata;
}

bool app_session_recorder_t::app_write_metadata()
{
    if (m_directory.isEmpty()) return false;
    const QByteArray bytes = QJsonDocument(app_session_summary()).toJson(QJsonDocument::Indented);
    QElapsedTimer timer;
    timer.start();
    int attempts = 0;
    QString stage, detail;
    // Windows readers omitting FILE_SHARE_DELETE can briefly prevent atomic
    // replacement. Retry the same metadata snapshot only; raw/CSV are never
    // re-written. The 150 ms retry budget can delay this Qt file poll (normally
    // 50 ms), while the independent C producer keeps writing its growing file.
    // This is software-only; no hardware sampling timing is changed. No event
    // pumping is used here, so recorder calls cannot become re-entrant.
#ifdef Q_OS_WIN
    const qint64 retry_budget_ms = 150;
#else
    const qint64 retry_budget_ms = 0;
#endif
    for (;;) {
        bool retryable = false;
        {
            QSaveFile file(m_directory + "/metadata.json");
            file.setDirectWriteFallback(false);
            ++attempts;
            if (!file.open(QIODevice::WriteOnly)) {
                stage = "OPEN";
                detail = file.errorString();
                retryable = file.error() == QFileDevice::PermissionsError
                        || file.error() == QFileDevice::OpenError;
            } else if (file.write(bytes) != bytes.size()) {
                stage = "WRITE";
                detail = file.errorString();
                file.cancelWriting();
            } else if (!file.commit()) {
                stage = "COMMIT";
                detail = file.errorString();
                retryable = true;
            } else {
                m_metadata_retry_count += static_cast<quint64>(attempts - 1);
                if (attempts > 1) ++m_metadata_recovered_count;
                m_metadata_max_write_ms = qMax(m_metadata_max_write_ms, timer.elapsed());
                m_last_metadata_utc = QDateTime::currentDateTimeUtc();
                return true;
            }
        } // Destroy/cancel the failed temporary file before a fresh atomic attempt.
        const qint64 remaining_ms = retry_budget_ms - timer.elapsed();
        if (!retryable || remaining_ms <= 0) break;
        QThread::msleep(static_cast<unsigned long>(qMin<qint64>(10, remaining_ms)));
        if (timer.elapsed() >= retry_budget_ms) break;
    }
    m_metadata_retry_count += static_cast<quint64>(attempts - 1);
    m_metadata_max_write_ms = qMax(m_metadata_max_write_ms, timer.elapsed());
    // Persistent failure remains fatal and cannot consume a second retry budget
    // through app_fail_session. Preserve any preceding raw/CSV error as primary.
    if (m_error.isEmpty()) m_error = QString("Metadata %1 failed after %2 attempt(s), %3 ms (retry budget %4 ms): %5")
            .arg(stage).arg(attempts).arg(timer.elapsed()).arg(retry_budget_ms).arg(detail);
    return false;
}

bool app_session_recorder_t::app_stop_session(const QString &reason)
{
    if (!m_active) return m_error.isEmpty();
    if (!m_raw.flush()) return app_fail_session("Raw final flush failed: " + m_raw.errorString());
    if (!m_csv.flush()) return app_fail_session("CSV final flush failed: " + m_csv.errorString());
    if (!m_events.flush()) return app_fail_session("Event final flush failed: " + m_events.errorString());
    m_raw.close();
    m_csv.close();
    m_events.close();
    m_active = false;
    m_end_utc = QDateTime::currentDateTimeUtc();
    m_stop_reason = reason;
    return app_write_metadata();
}
