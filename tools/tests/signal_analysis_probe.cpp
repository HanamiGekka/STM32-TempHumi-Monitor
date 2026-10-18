#include "app_signal_analysis.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <cmath>
#include <functional>
#include <limits>

static QJsonArray app_probe_cases;
static QJsonArray app_probe_checks;
static int app_probe_passed = 0;
static int app_probe_failed = 0;

static void app_probe_assert(bool passed, const QString &name,
                             const QJsonValue &actual, const QJsonValue &expected,
                             double tolerance = 0)
{
    QJsonObject check;
    check.insert("name", name);
    check.insert("passed", passed);
    check.insert("actual", actual);
    check.insert("expected", expected);
    check.insert("absolute_tolerance", tolerance);
    app_probe_checks.append(check);
    passed ? ++app_probe_passed : ++app_probe_failed;
}

static void app_probe_number(const app_signal_metric_t &metric, const QString &name,
                            double expected, double tolerance = 1e-9)
{
    app_probe_assert(metric.available && std::isfinite(metric.value)
                      && std::abs(metric.value - expected) <= tolerance,
                     name, metric.available ? QJsonValue(metric.value) : QJsonValue(metric.reason),
                     expected, tolerance);
}

static void app_probe_na(const app_signal_metric_t &metric, const QString &name,
                        const QString &reason)
{
    app_probe_assert(!metric.available && metric.reason == reason, name,
                     metric.available ? QJsonValue(metric.value) : QJsonValue(metric.reason), reason);
}

static void app_probe_counts(const app_signal_result_t &result, int total, int valid, int invalid)
{
    app_probe_assert(result.sample_count == quint64(total), "sample_count", double(result.sample_count), total);
    app_probe_assert(result.valid_sample_count == quint64(valid), "valid_sample_count", double(result.valid_sample_count), valid);
    app_probe_assert(result.invalid_sample_count == quint64(invalid), "invalid_sample_count", double(result.invalid_sample_count), invalid);
}

static QVector<app_signal_sample_t> app_probe_vector(int count, double dt,
                                                    const std::function<double(int)> &value)
{
    QVector<app_signal_sample_t> samples;
    samples.reserve(count);
    for (int i = 0; i < count; ++i) {
        app_signal_sample_t sample;
        sample.time_s = i * dt;
        sample.value = value(i);
        sample.valid = true;
        sample.segment_id = 7;
        samples.append(sample);
    }
    return samples;
}

static void app_probe_case(const QString &name, app_signal_channel_t channel,
                           const QVector<app_signal_sample_t> &samples,
                           const std::function<void(const app_signal_result_t &)> &verify,
                           double t1 = -1, double t2 = 100000)
{
    app_probe_checks = QJsonArray();
    const int failures_before = app_probe_failed;
    QElapsedTimer timer;
    timer.start();
    const app_signal_result_t result = app_signal_analyze_window(samples, channel, t1, t2, 7);
    const qint64 elapsed_ns = timer.nsecsElapsed();
    verify(result);
    const QJsonObject serialized = app_signal_analysis_json(result, QStringLiteral("probe-caller-version"));
    app_probe_assert(serialized.value("software_version").toString() == "probe-caller-version",
                     "caller_version", serialized.value("software_version"), "probe-caller-version");
    app_probe_assert(serialized.value("segment_id").toString() == "7", "segment_identity", serialized.value("segment_id"), "7");
    app_probe_assert(serialized.value("metric_definitions").toObject().value("standard_deviation").toString().contains("Population"),
                     "population_definition", serialized.value("metric_definitions").toObject().value("standard_deviation"), "Population");
    if (!result.rise_time_10_90_s.available)
        app_probe_assert(serialized.value("response").toObject().value("rise_time_10_90_s").toObject().value("value").isNull(),
                         "NA_serializes_null", serialized.value("response").toObject().value("rise_time_10_90_s").toObject().value("value"), QJsonValue(QJsonValue::Null));
    QJsonObject report;
    report.insert("name", name);
    report.insert("channel", channel == APP_SIGNAL_TEMPERATURE ? "Temperature" : "Humidity");
    report.insert("passed", app_probe_failed == failures_before);
    report.insert("elapsed_ns", double(elapsed_ns));
    report.insert("checks", app_probe_checks);
    report.insert("result", serialized);
    QJsonArray input;
    if (samples.size() <= 200) {
        for (const app_signal_sample_t &sample : samples) {
            QJsonObject point;
            point.insert("time_s", std::isfinite(sample.time_s) ? QJsonValue(sample.time_s) : QJsonValue(QJsonValue::Null));
            point.insert("value", std::isfinite(sample.value) ? QJsonValue(sample.value) : QJsonValue(QJsonValue::Null));
            point.insert("valid", sample.valid);
            point.insert("segment_id", QString::number(sample.segment_id));
            input.append(point);
        }
        report.insert("input", input);
    } else {
        report.insert("input_generator", "100000 points; time=i*0.01 seconds; value=3; valid=true; segment=7");
    }
    app_probe_cases.append(report);
}

#define NUMBER(field, value) app_probe_number(r.field, QStringLiteral(#field), value)
#define NEAR(field, value, tolerance) app_probe_number(r.field, QStringLiteral(#field), value, tolerance)
#define NA(field, reason) app_probe_na(r.field, QStringLiteral(#field), QStringLiteral(reason))
#define REASON(field, expected) app_probe_assert(r.field == QStringLiteral(expected), QStringLiteral(#field), r.field, QStringLiteral(expected))

static void app_probe_channel(app_signal_channel_t channel)
{
    const auto constant = app_probe_vector(100, 0.5, [](int) { return 4.0; });
    app_probe_case("constant_signal", channel, constant, [](const app_signal_result_t &r) {
        app_probe_counts(r, 100, 100, 0);
        NUMBER(mean, 4); NUMBER(rms, 4); NUMBER(standard_deviation, 0);
        NUMBER(first_value, 4); NUMBER(last_value, 4); NUMBER(delta, 0);
        NUMBER(minimum, 4); NUMBER(maximum, 4); NUMBER(peak_to_peak, 0);
        NUMBER(duration_s, 49.5); NUMBER(mean_dt_s, 0.5); NUMBER(min_dt_s, 0.5);
        NUMBER(max_dt_s, 0.5); NUMBER(std_dt_s, 0); NUMBER(effective_sample_rate_hz, 2);
        NUMBER(average_slope, 0); NUMBER(initial_level, 4); NUMBER(final_level, 4);
        NUMBER(response_amplitude, 0); REASON(response_reason, "LOW_AMPLITUDE");
        NA(rise_time_10_90_s, "LOW_AMPLITUDE"); NA(settling_time_2pct_s, "LOW_AMPLITUDE");
    });
    app_probe_case("linear_ramp", channel, app_probe_vector(100, 0.2, [](int i) { return 2 + 0.5 * i; }), [](const app_signal_result_t &r) {
        app_probe_counts(r, 100, 100, 0);
        NUMBER(mean, 26.75); NUMBER(rms, std::sqrt(26.75 * 26.75 + 0.25 * 9999 / 12));
        NUMBER(standard_deviation, 0.5 * std::sqrt(9999.0 / 12));
        NUMBER(first_value, 2); NUMBER(last_value, 51.5); NUMBER(delta, 49.5);
        NUMBER(duration_s, 19.8); NUMBER(average_slope, 2.5);
        NUMBER(mean_dt_s, 0.2); NUMBER(effective_sample_rate_hz, 5);
        NUMBER(initial_level, 4.25); NUMBER(final_level, 49.25); NUMBER(response_amplitude, 45);
        REASON(response_reason, "NO_STABLE_INITIAL_VALUE");
    });
    const auto rise = app_probe_vector(100, 1, [](int i) { return i < 20 ? 10.0 : 20.0; });
    app_probe_case("ideal_rising_step", channel, rise, [](const app_signal_result_t &r) {
        app_probe_counts(r, 100, 100, 0);
        NUMBER(mean, 18); NUMBER(rms, std::sqrt(340.0)); NUMBER(standard_deviation, 4);
        NUMBER(initial_level, 10); NUMBER(final_level, 20); NUMBER(response_amplitude, 10);
        NUMBER(rise_time_10_90_s, 0.8); NA(fall_time_90_10_s, "NOT_FALLING_RESPONSE");
        NUMBER(overshoot_pct, 0); NUMBER(undershoot_pct, 0);
        NUMBER(settling_time_2pct_s, 19.98); NUMBER(settling_time_5pct_s, 19.95);
        REASON(response_direction, "RISING");
    });
    app_probe_case("ideal_falling_step", channel, app_probe_vector(100, 1, [](int i) { return i < 20 ? 40.0 : 20.0; }), [](const app_signal_result_t &r) {
        NUMBER(mean, 24); NUMBER(rms, std::sqrt(640.0)); NUMBER(standard_deviation, 8);
        NUMBER(response_amplitude, -20); NUMBER(fall_time_90_10_s, 0.8);
        NA(rise_time_10_90_s, "NOT_RISING_RESPONSE"); NUMBER(overshoot_pct, 0);
        NUMBER(settling_time_2pct_s, 19.98); NUMBER(settling_time_5pct_s, 19.95);
        REASON(response_direction, "FALLING");
    });
    auto overshoot = rise;
    overshoot[15].value = 9;
    overshoot[40].value = 22;
    app_probe_case("step_known_overshoot_and_undershoot", channel, overshoot, [](const app_signal_result_t &r) {
        NUMBER(overshoot_pct, 20); NUMBER(undershoot_pct, 10); NUMBER(rise_time_10_90_s, 0.8);
        NUMBER(settling_time_2pct_s, 40.9); NUMBER(settling_time_5pct_s, 40.75);
    });
    auto fall_overshoot = app_probe_vector(100, 1, [](int i) { return i < 20 ? 20.0 : 10.0; });
    fall_overshoot[15].value = 21;
    fall_overshoot[40].value = 8;
    app_probe_case("falling_orientation_overshoot", channel, fall_overshoot, [](const app_signal_result_t &r) {
        NUMBER(overshoot_pct, 20); NUMBER(undershoot_pct, 10); NUMBER(fall_time_90_10_s, 0.8);
        NUMBER(settling_time_2pct_s, 40.9); NUMBER(settling_time_5pct_s, 40.75);
    });
    app_probe_case("settled_linear_transition", channel, app_probe_vector(100, 1, [](int i) {
        return i < 20 ? 10.0 : (i < 40 ? 10 + 0.5 * (i - 19) : 20.0);
    }), [](const app_signal_result_t &r) {
        NUMBER(rise_time_10_90_s, 16); NUMBER(settling_time_2pct_s, 38.6);
        NUMBER(settling_time_5pct_s, 38); NUMBER(overshoot_pct, 0);
    });
    auto not_settled = rise;
    not_settled.last().value = 20.25;
    app_probe_case("never_settled_2pct", channel, not_settled, [](const app_signal_result_t &r) {
        NUMBER(final_level, 20.025); NUMBER(response_amplitude, 10.025);
        NUMBER(overshoot_pct, 0.225 / 10.025 * 100);
        NA(settling_time_2pct_s, "NOT_SETTLED"); NUMBER(settling_time_5pct_s, 19.952375);
    });
    auto final_na = rise;
    final_na.last().valid = false;
    app_probe_case("never_settled_final_NA", channel, final_na, [](const app_signal_result_t &r) {
        app_probe_counts(r, 100, 99, 1); NUMBER(final_level, 20);
        NA(settling_time_2pct_s, "NOT_SETTLED"); NA(settling_time_5pct_s, "NOT_SETTLED");
    });
    app_probe_case("low_amplitude", channel, app_probe_vector(100, 1, [](int i) { return i < 20 ? 10.0 : 10.0000001; }), [](const app_signal_result_t &r) {
        NEAR(response_amplitude, 0.0000001, 1e-12); REASON(response_reason, "LOW_AMPLITUDE");
        NA(overshoot_pct, "LOW_AMPLITUDE"); NA(rise_time_10_90_s, "LOW_AMPLITUDE");
    });
    auto one = app_probe_vector(1, 1, [](int) { return 7.0; });
    one[0].time_s = 5;
    app_probe_case("one_sample", channel, one, [](const app_signal_result_t &r) {
        app_probe_counts(r, 1, 1, 0); NUMBER(mean, 7); NUMBER(rms, 7);
        NUMBER(first_value, 7); NUMBER(last_value, 7); NUMBER(minimum, 7); NUMBER(maximum, 7);
        NUMBER(peak_to_peak, 0); NA(standard_deviation, "INSUFFICIENT_DATA");
        NA(delta, "INSUFFICIENT_DATA"); NA(duration_s, "INSUFFICIENT_DATA");
        NA(average_slope, "INSUFFICIENT_DATA"); NA(mean_dt_s, "INSUFFICIENT_DATA");
        NA(rise_time_10_90_s, "INSUFFICIENT_DATA");
    }, 5, 5);
    app_probe_case("two_samples", channel, app_probe_vector(2, 2, [](int i) { return 3.0 + 2 * i; }), [](const app_signal_result_t &r) {
        app_probe_counts(r, 2, 2, 0); NUMBER(mean, 4); NUMBER(rms, std::sqrt(17.0));
        NUMBER(standard_deviation, 1); NUMBER(delta, 2); NUMBER(average_slope, 1);
        NUMBER(duration_s, 2); NUMBER(mean_dt_s, 2); NUMBER(std_dt_s, 0);
        NUMBER(effective_sample_rate_hz, 0.5); NA(rise_time_10_90_s, "INSUFFICIENT_DATA");
    });
    auto all_na = app_probe_vector(100, 0.1, [](int) { return std::numeric_limits<double>::quiet_NaN(); });
    app_probe_case("all_NA", channel, all_na, [](const app_signal_result_t &r) {
        app_probe_counts(r, 100, 0, 100); NA(mean, "INSUFFICIENT_DATA"); NA(rms, "INSUFFICIENT_DATA");
        NA(first_value, "INSUFFICIENT_DATA"); NUMBER(duration_s, 9.9);
        NUMBER(mean_dt_s, 0.1); NUMBER(effective_sample_rate_hz, 10);
    });
    auto mixed = app_probe_vector(6, 1, [](int i) { return double(i + 1); });
    for (int i = 1; i < mixed.size(); i += 2) mixed[i].valid = false;
    app_probe_case("mixed_NA", channel, mixed, [](const app_signal_result_t &r) {
        app_probe_counts(r, 6, 3, 3); NUMBER(mean, 3); NUMBER(rms, std::sqrt(35.0 / 3));
        NUMBER(standard_deviation, std::sqrt(8.0 / 3)); NUMBER(delta, 4); NUMBER(average_slope, 1);
        NUMBER(duration_s, 5); NUMBER(mean_dt_s, 1); NUMBER(effective_sample_rate_hz, 1);
    });
    auto bad_time = rise;
    bad_time[50].time_s = bad_time[49].time_s;
    app_probe_case("invalid_repeated_timestamps", channel, bad_time, [](const app_signal_result_t &r) {
        app_probe_counts(r, 100, 100, 0); REASON(selection_reason, "INVALID_TIMESTAMPS");
        NA(mean, "INVALID_TIMESTAMPS"); NA(mean_dt_s, "INVALID_TIMESTAMPS");
    });
    bad_time[50].time_s = 40;
    app_probe_case("invalid_rollback_timestamps", channel, bad_time, [](const app_signal_result_t &r) {
        REASON(selection_reason, "INVALID_TIMESTAMPS"); NA(rms, "INVALID_TIMESTAMPS");
    });
    bad_time = rise;
    bad_time[50].time_s = std::numeric_limits<double>::infinity();
    app_probe_case("nonfinite_timestamps", channel, bad_time, [](const app_signal_result_t &r) {
        app_probe_counts(r, 99, 99, 0); REASON(selection_reason, "INVALID_TIMESTAMPS");
        NA(mean, "INVALID_TIMESTAMPS");
    });
    auto irregular = app_probe_vector(4, 1, [](int i) { return double(i + 1); });
    irregular[0].time_s = 0; irregular[1].time_s = 0.1; irregular[2].time_s = 0.4; irregular[3].time_s = 1;
    app_probe_case("irregular_timestamps", channel, irregular, [](const app_signal_result_t &r) {
        NUMBER(mean, 2.5); NUMBER(rms, std::sqrt(7.5)); NUMBER(standard_deviation, std::sqrt(1.25));
        NUMBER(duration_s, 1); NUMBER(mean_dt_s, 1.0 / 3); NUMBER(min_dt_s, 0.1); NUMBER(max_dt_s, 0.6);
        NUMBER(std_dt_s, std::sqrt(0.46 / 3 - 1.0 / 9)); NUMBER(effective_sample_rate_hz, 3);
        NUMBER(average_slope, 3);
    });
    auto segments = rise;
    for (int i = 50; i < segments.size(); ++i) segments[i].segment_id = 8;
    app_probe_case("segment_boundary", channel, segments, [](const app_signal_result_t &r) {
        app_probe_counts(r, 100, 100, 0); REASON(selection_reason, "MULTIPLE_SEGMENTS");
        NA(mean, "MULTIPLE_SEGMENTS"); NA(settling_time_5pct_s, "MULTIPLE_SEGMENTS");
    });
    app_probe_case("negative_temperature_no_clamp", channel, app_probe_vector(4, 1, [](int i) { return -20.0 + 10 * i; }), [](const app_signal_result_t &r) {
        NUMBER(mean, -5); NUMBER(rms, std::sqrt(150.0)); NUMBER(standard_deviation, std::sqrt(125.0));
        NUMBER(minimum, -20); NUMBER(maximum, 10); NUMBER(delta, 30); NUMBER(average_slope, 10);
    });
    app_probe_case("humidity_0_100_edges_no_clamp", channel, app_probe_vector(2, 1, [](int i) { return 100.0 * i; }), [](const app_signal_result_t &r) {
        NUMBER(mean, 50); NUMBER(rms, std::sqrt(5000.0)); NUMBER(standard_deviation, 50);
        NUMBER(minimum, 0); NUMBER(maximum, 100); NUMBER(delta, 100); NUMBER(average_slope, 100);
    });
    app_probe_case("large_100000_samples", channel, app_probe_vector(100000, 0.01, [](int) { return 3.0; }), [](const app_signal_result_t &r) {
        app_probe_counts(r, 100000, 100000, 0); NUMBER(mean, 3); NUMBER(rms, 3);
        NUMBER(standard_deviation, 0); NUMBER(duration_s, 999.99); NUMBER(mean_dt_s, 0.01);
        NUMBER(effective_sample_rate_hz, 100); REASON(response_reason, "LOW_AMPLITUDE");
    });
    app_probe_case("invalid_reversed_window", channel, rise, [](const app_signal_result_t &r) {
        app_probe_counts(r, 0, 0, 0); REASON(selection_reason, "INVALID_WINDOW"); NA(mean, "INVALID_WINDOW");
    }, 20, 10);
    app_probe_case("empty_selected_window", channel, rise, [](const app_signal_result_t &r) {
        app_probe_counts(r, 0, 0, 0); REASON(selection_reason, "INSUFFICIENT_DATA"); NA(mean, "INSUFFICIENT_DATA");
    }, 1000, 2000);
    auto crossing_na = rise;
    crossing_na[19].valid = false;
    app_probe_case("threshold_crossing_NA_gap", channel, crossing_na, [](const app_signal_result_t &r) {
        app_probe_counts(r, 100, 99, 1); NUMBER(initial_level, 10); NUMBER(final_level, 20);
        NA(rise_time_10_90_s, "NO_10_PERCENT_CROSSING"); NUMBER(settling_time_2pct_s, 20);
        NUMBER(settling_time_5pct_s, 20); NUMBER(overshoot_pct, 0);
    });
    auto unstable = rise;
    for (int i = 90; i < 100; ++i) unstable[i].value = i % 2 ? 25 : 15;
    app_probe_case("unstable_final_plateau", channel, unstable, [](const app_signal_result_t &r) {
        NUMBER(final_level, 20); REASON(response_reason, "NO_STABLE_FINAL_VALUE");
        NA(settling_time_5pct_s, "NO_STABLE_FINAL_VALUE");
    });
    auto heavy_na = rise;
    for (int i = 40; i < 60; ++i) heavy_na[i].valid = false;
    app_probe_case("NA_heavy_window", channel, heavy_na, [](const app_signal_result_t &r) {
        app_probe_counts(r, 100, 80, 20); NUMBER(mean, 17.5);
        REASON(response_reason, "TOO_MANY_INVALID_SAMPLES"); NA(rise_time_10_90_s, "TOO_MANY_INVALID_SAMPLES");
    });
    app_probe_case("response_minimum_30_samples", channel, app_probe_vector(30, 1, [](int i) { return i < 6 ? 10.0 : 20.0; }), [](const app_signal_result_t &r) {
        app_probe_counts(r, 30, 30, 0); NUMBER(initial_level, 10); NUMBER(final_level, 20);
        NUMBER(rise_time_10_90_s, 0.8); NUMBER(settling_time_5pct_s, 5.95);
    });
    app_probe_case("response_29_samples_insufficient", channel, app_probe_vector(29, 1, [](int i) { return i < 6 ? 10.0 : 20.0; }), [](const app_signal_result_t &r) {
        app_probe_counts(r, 29, 29, 0); REASON(response_reason, "INSUFFICIENT_DATA"); NA(initial_level, "INSUFFICIENT_DATA");
    });
    app_probe_case("inclusive_window_selection", channel, app_probe_vector(100, 1, [](int i) { return double(i); }), [](const app_signal_result_t &r) {
        app_probe_counts(r, 11, 11, 0); NUMBER(first_value, 10); NUMBER(last_value, 20);
        NUMBER(mean, 15); NUMBER(standard_deviation, std::sqrt(10.0)); NUMBER(rms, std::sqrt(235.0));
        NUMBER(duration_s, 10); NUMBER(average_slope, 1);
    }, 10, 20);
    auto finite_values = rise;
    finite_values[50].value = std::numeric_limits<double>::infinity();
    app_probe_case("nonfinite_value_is_NA", channel, finite_values, [](const app_signal_result_t &r) {
        app_probe_counts(r, 100, 99, 1); NUMBER(mean, 1780.0 / 99); NUMBER(maximum, 20);
        NUMBER(duration_s, 99); NUMBER(settling_time_2pct_s, 51);
    });
    app_probe_case("finite_value_arithmetic_overflow", channel,
        app_probe_vector(2, 1, [](int i) { return i ? 1e308 : -1e308; }), [](const app_signal_result_t &r) {
        app_probe_counts(r, 2, 2, 0); NUMBER(rms, 1e308); NA(mean, "NUMERICAL_OVERFLOW");
        NA(standard_deviation, "NUMERICAL_OVERFLOW"); NA(delta, "NUMERICAL_OVERFLOW");
        NA(peak_to_peak, "NUMERICAL_OVERFLOW");
    });
    auto huge_time = app_probe_vector(3, 1, [](int i) { return double(i); });
    huge_time[0].time_s = -1e308; huge_time[1].time_s = 0; huge_time[2].time_s = 1e308;
    app_probe_case("finite_time_span_overflow", channel, huge_time, [](const app_signal_result_t &r) {
        app_probe_counts(r, 3, 3, 0); REASON(selection_reason, "INVALID_TIMESTAMPS");
        NA(mean, "INVALID_TIMESTAMPS"); NA(average_slope, "INVALID_TIMESTAMPS");
    }, -1e308, 1e308);
    auto missing90 = app_probe_vector(100, 1, [](int i) {
        return i < 20 ? 10.0 : (i < 40 ? 10 + 0.5 * (i - 19) : 20.0);
    });
    missing90[36].valid = false;
    app_probe_case("90_percent_crossing_NA_gap", channel, missing90, [](const app_signal_result_t &r) {
        app_probe_counts(r, 100, 99, 1); NA(rise_time_10_90_s, "NO_90_PERCENT_CROSSING");
        NUMBER(settling_time_2pct_s, 38.6);
    });
    auto ninety = rise;
    for (int i = 40; i < 50; ++i) ninety[i].valid = false;
    app_probe_case("exact_90_percent_valid_gate", channel, ninety, [](const app_signal_result_t &r) {
        app_probe_counts(r, 100, 90, 10); NUMBER(mean, 1600.0 / 90);
        NUMBER(rise_time_10_90_s, 0.8); NUMBER(settling_time_2pct_s, 50);
    });
    ninety[50].valid = false;
    app_probe_case("below_90_percent_valid_gate", channel, ninety, [](const app_signal_result_t &r) {
        app_probe_counts(r, 100, 89, 11); REASON(response_reason, "TOO_MANY_INVALID_SAMPLES");
        NA(overshoot_pct, "TOO_MANY_INVALID_SAMPLES");
    });
    app_probe_case("plateau_ceil_31_samples", channel, app_probe_vector(31, 1, [](int i) { return i < 10 ? 10.0 : 20.0; }), [](const app_signal_result_t &r) {
        app_probe_counts(r, 31, 31, 0); NUMBER(initial_level, 10); NUMBER(final_level, 20);
        NUMBER(rise_time_10_90_s, 0.8); NUMBER(settling_time_5pct_s, 9.95);
    });
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    app_probe_channel(APP_SIGNAL_TEMPERATURE);
    app_probe_channel(APP_SIGNAL_HUMIDITY);
    QJsonObject summary;
    summary.insert("status", app_probe_failed == 0 ? "PASS" : "FAIL");
    summary.insert("classification", "SOFTWARE_ONLY_FIXED_VECTORS_NOT_HARDWARE");
    summary.insert("case_count", app_probe_cases.size());
    summary.insert("checks_passed", app_probe_passed);
    summary.insert("checks_failed", app_probe_failed);
    summary.insert("cases", app_probe_cases);
    const QByteArray data = QJsonDocument(summary).toJson(QJsonDocument::Indented);
    if (application.arguments().size() > 1) {
        QFile file(application.arguments()[1]);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(data) != data.size() || !file.flush())
            return 2;
    }
    QTextStream output(stdout);
    output << "SIGNAL_ANALYSIS " << summary.value("status").toString()
           << " cases=" << app_probe_cases.size() << " passed=" << app_probe_passed
           << " failed=" << app_probe_failed << '\n';
    return app_probe_failed == 0 ? 0 : 1;
}
