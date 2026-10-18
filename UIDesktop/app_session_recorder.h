#ifndef APP_SESSION_RECORDER_H
#define APP_SESSION_RECORDER_H

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include "app_th3.h"

struct app_session_options_t
{
    QString output_directory;
    QString input_mode;
    QString input_path;
    QString classification;
    QString git_head;
    QString qt_build_identity;
    QString software_version;
};

/* RUN #1 only records explicit file streams or replay. It cannot label a file
 * carrying source=REAL as LIVE REAL; transport and frame source are independent.
 * Caller owns the line/timeline ordering. Every write/flush failure is returned.
 */
class app_session_recorder_t
{
public:
    app_session_recorder_t() = default;
    ~app_session_recorder_t();
    bool app_start_session(const app_session_options_t &options);
    bool app_record_line(const app_th3_frame_t &frame, const app_th3_time_t &time,
                         const QDateTime &received_utc);
    bool app_stop_session(const QString &reason);
    bool app_session_active() const { return m_active; }
    QString app_session_directory() const { return m_directory; }
    QString app_session_error() const { return m_error; }
    QJsonObject app_session_summary() const;
    bool app_record_control_state(const QJsonObject &state);
    bool app_record_display_event(const QJsonObject &event);

private:
    bool app_write_metadata();
    bool app_fail_session(const QString &error, bool persist_metadata = true);
    QString app_classify_frame(const app_th3_frame_t &frame) const;
    app_session_options_t m_options;
    QFile m_raw;
    QFile m_csv;
    QFile m_events;
    quint64 m_event_count=0;
    QString m_directory;
    QString m_error;
    QString m_stop_reason;
    QDateTime m_start_utc;
    QDateTime m_end_utc;
    QDateTime m_last_metadata_utc;
    bool m_active = false;
    quint64 m_raw_line_count = 0;
    quint64 m_raw_record_count = 0;
    quint64 m_raw_fragment_count = 0;
    quint64 m_raw_byte_count = 0;
    quint64 m_csv_row_count = 0;
    quint64 m_parsed_count = 0;
    quint64 m_th3_count = 0;
    quint64 m_diagnostic_count = 0;
    quint64 m_legacy_count = 0;
    quint64 m_malformed_count = 0;
    quint64 m_invalid_count = 0;
    quint64 m_metadata_retry_count = 0;
    quint64 m_metadata_recovered_count = 0;
    qint64 m_metadata_max_write_ms = 0;
    QSet<quint64> m_segment_ids;
    QSet<QString> m_observed_classifications;
    QSet<QString> m_observed_protocols;
    QJsonObject m_last_channels;
    QJsonObject m_control_state;
    QJsonArray m_segments;
};

#endif
