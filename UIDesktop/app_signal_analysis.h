#ifndef APP_SIGNAL_ANALYSIS_H
#define APP_SIGNAL_ANALYSIS_H

#include <QJsonObject>
#include <QString>
#include <QVector>

enum app_signal_channel_t {
    APP_SIGNAL_TEMPERATURE,
    APP_SIGNAL_HUMIDITY
};

struct app_signal_sample_t {
    double time_s = 0;
    double value = 0;
    bool valid = false;
    quint64 segment_id = 0;
};

// A value is meaningful only when available is true; JSON uses null for N/A.
struct app_signal_metric_t {
    bool available = false;
    double value = 0;
    QString reason;
    QString unit;
};

struct app_signal_result_t {
    app_signal_channel_t channel = APP_SIGNAL_TEMPERATURE;
    quint64 segment_id = 0;
    double requested_t1_s = 0;
    double requested_t2_s = 0;
    quint64 sample_count = 0;
    quint64 valid_sample_count = 0;
    quint64 invalid_sample_count = 0;
    bool selection_valid = false;
    QString selection_reason;
    bool response_valid = false;
    QString response_reason;
    QString response_direction;
    app_signal_metric_t actual_tstart_s, actual_tend_s, duration_s;
    app_signal_metric_t first_value, last_value, delta, mean, rms;
    app_signal_metric_t standard_deviation, minimum, maximum, peak_to_peak;
    app_signal_metric_t average_slope;
    app_signal_metric_t mean_dt_s, min_dt_s, max_dt_s, std_dt_s;
    app_signal_metric_t effective_sample_rate_hz;
    app_signal_metric_t initial_level, final_level, response_amplitude;
    app_signal_metric_t rise_time_10_90_s, fall_time_90_10_s;
    app_signal_metric_t overshoot_pct, undershoot_pct;
    app_signal_metric_t settling_time_2pct_s, settling_time_5pct_s;
};

// Inclusive source-time window. Timing includes invalid-value records; values do
// not. Nonfinite times, non-increasing selected times and mixed segments reject
// the selection. No source, file, UI or hardware access occurs in this module.
app_signal_result_t app_signal_analyze_window(
    const QVector<app_signal_sample_t> &samples, app_signal_channel_t channel,
    double t1_s, double t2_s, quint64 segment_id);

// The caller supplies the canonical software version; this module has no copy.
QJsonObject app_signal_analysis_json(const app_signal_result_t &result,
                                    const QString &software_version = QString());

#endif
