#include "app_signal_analysis.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// These are conservative software observation gates, not sensor tolerances or
// calibration constants. A response needs 30 valid observations, >=90% valid
// records, and ceil(10%) samples in each plateau (at least 3). Plateau spread
// <=5% and population std <=2% of response amplitude are required. Amplitude
// <=1e-6 * max(1, abs(initial), abs(final)) has no reliable response denominator.
const int APP_SIGNAL_RESPONSE_MIN_VALID = 30;
const double APP_SIGNAL_RESPONSE_MIN_FRACTION = 0.90;
const double APP_SIGNAL_PLATEAU_FRACTION = 0.10;
const double APP_SIGNAL_PLATEAU_MAX_RANGE = 0.05;
const double APP_SIGNAL_PLATEAU_MAX_STD = 0.02;
const double APP_SIGNAL_AMPLITUDE_RELATIVE_FLOOR = 1e-6;

struct app_signal_stats_t {
    quint64 count = 0;
    double mean = 0;
    double m2 = 0;
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    double square_scale = 0;
    double scaled_square_sum = 0;

    void app_append_value(double value)
    {
        ++count;
        const double difference = value - mean;
        mean += difference / double(count);
        m2 += difference * (value - mean);
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
        // Scaled sum of squares avoids overflow for otherwise finite samples.
        const double magnitude = std::abs(value);
        if (magnitude > square_scale) {
            const double ratio = square_scale / magnitude;
            scaled_square_sum = 1 + scaled_square_sum * ratio * ratio;
            square_scale = magnitude;
        } else if (magnitude > 0) {
            const double ratio = magnitude / square_scale;
            scaled_square_sum += ratio * ratio;
        }
    }

    double app_population_std() const
    {
        // Overflow in Welford's accumulator must not become an apparent zero
        // variance through max(0, NaN/-infinity). Tiny finite roundoff is the
        // only negative quantity that can safely be clamped here.
        if (!std::isfinite(m2))
            return std::numeric_limits<double>::quiet_NaN();
        return std::sqrt(std::max(0.0, m2 / double(count)));
    }

    double app_rms() const
    {
        return square_scale * std::sqrt(scaled_square_sum / double(count));
    }
};

static void app_signal_set_metric(app_signal_metric_t &metric, double value)
{
    metric.available = std::isfinite(value);
    metric.value = metric.available ? value : 0;
    metric.reason = metric.available ? QString() : QStringLiteral("NUMERICAL_OVERFLOW");
}

static void app_signal_reject_metric(app_signal_metric_t &metric, const QString &reason)
{
    metric.available = false;
    metric.reason = reason;
}

static QVector<app_signal_metric_t *> app_signal_all_metrics(app_signal_result_t &result)
{
    return {&result.actual_tstart_s, &result.actual_tend_s, &result.duration_s,
            &result.first_value, &result.last_value, &result.delta, &result.mean,
            &result.rms, &result.standard_deviation, &result.minimum,
            &result.maximum, &result.peak_to_peak, &result.average_slope,
            &result.mean_dt_s, &result.min_dt_s, &result.max_dt_s, &result.std_dt_s,
            &result.effective_sample_rate_hz, &result.initial_level,
            &result.final_level, &result.response_amplitude,
            &result.rise_time_10_90_s, &result.fall_time_90_10_s,
            &result.overshoot_pct, &result.undershoot_pct,
            &result.settling_time_2pct_s, &result.settling_time_5pct_s};
}

static void app_signal_set_units(app_signal_result_t &result)
{
    const QString value_unit = result.channel == APP_SIGNAL_TEMPERATURE
            ? QStringLiteral("degC") : QStringLiteral("%RH");
    for (app_signal_metric_t *metric : app_signal_all_metrics(result)) {
        metric->unit = value_unit;
        metric->reason = QStringLiteral("INSUFFICIENT_DATA");
    }
    for (app_signal_metric_t *metric : {&result.actual_tstart_s, &result.actual_tend_s,
         &result.duration_s, &result.mean_dt_s, &result.min_dt_s, &result.max_dt_s,
         &result.std_dt_s, &result.rise_time_10_90_s, &result.fall_time_90_10_s,
         &result.settling_time_2pct_s, &result.settling_time_5pct_s}) {
        metric->unit = QStringLiteral("s");
    }
    result.average_slope.unit = value_unit + QStringLiteral("/s");
    result.effective_sample_rate_hz.unit = QStringLiteral("Hz");
    result.overshoot_pct.unit = result.undershoot_pct.unit = QStringLiteral("%");
}

static void app_signal_reject_selection(app_signal_result_t &result, const QString &reason)
{
    result.selection_valid = false;
    result.selection_reason = reason;
    result.response_reason = reason;
    for (app_signal_metric_t *metric : app_signal_all_metrics(result))
        app_signal_reject_metric(*metric, reason);
}

static void app_signal_reject_response(app_signal_result_t &result, const QString &reason)
{
    result.response_valid = false;
    result.response_reason = reason;
    for (app_signal_metric_t *metric : {&result.rise_time_10_90_s,
         &result.fall_time_90_10_s, &result.overshoot_pct, &result.undershoot_pct,
         &result.settling_time_2pct_s, &result.settling_time_5pct_s}) {
        app_signal_reject_metric(*metric, reason);
    }
}

static bool app_signal_value_usable(const app_signal_sample_t &sample)
{
    return sample.valid && std::isfinite(sample.value);
}

static app_signal_metric_t app_signal_find_crossing(
        const QVector<const app_signal_sample_t *> &selected,
        double initial, double amplitude, double fraction)
{
    app_signal_metric_t crossing;
    crossing.unit = QStringLiteral("s");
    crossing.reason = fraction < 0.5 ? QStringLiteral("NO_10_PERCENT_CROSSING")
                                    : QStringLiteral("NO_90_PERCENT_CROSSING");
    for (int i = 1; i < selected.size(); ++i) {
        const app_signal_sample_t &previous = *selected[i - 1];
        const app_signal_sample_t &current = *selected[i];
        // Never invent a threshold time across a missing/nonfinite value.
        if (!app_signal_value_usable(previous) || !app_signal_value_usable(current))
            continue;
        const double before = (previous.value - initial) / amplitude;
        const double after = (current.value - initial) / amplitude;
        if (before < fraction && after >= fraction) {
            const double weight = (fraction - before) / (after - before);
            app_signal_set_metric(crossing, previous.time_s
                                  + weight * (current.time_s - previous.time_s));
            return crossing;
        }
    }
    return crossing;
}

static app_signal_metric_t app_signal_find_settling(
        const QVector<const app_signal_sample_t *> &selected,
        double final, double amplitude, double band_fraction, int plateau_count)
{
    app_signal_metric_t settling;
    settling.unit = QStringLiteral("s");
    settling.reason = QStringLiteral("NOT_SETTLED");
    const double band = std::abs(amplitude) * band_fraction;
    int suffix_start = selected.size();
    while (suffix_start > 0) {
        const app_signal_sample_t &sample = *selected[suffix_start - 1];
        if (!app_signal_value_usable(sample) || std::abs(sample.value - final) > band)
            break;
        --suffix_start;
    }
    // Requiring a full final-plateau length prevents a final single in-band
    // observation from being reported as sustained settling. NA breaks the
    // suffix; settling may be established only after that last gap.
    if (selected.size() - suffix_start < plateau_count)
        return settling;
    double settled_at_s = selected[suffix_start]->time_s;
    if (suffix_start > 0) {
        const app_signal_sample_t &previous = *selected[suffix_start - 1];
        const app_signal_sample_t &current = *selected[suffix_start];
        if (app_signal_value_usable(previous)) {
            const double boundary = final + (previous.value > final ? band : -band);
            const double weight = (boundary - previous.value) / (current.value - previous.value);
            settled_at_s = previous.time_s + weight * (current.time_s - previous.time_s);
        }
    }
    // All response times use source time and are relative to the first selected
    // record, not inferred excitation time or host receive time.
    app_signal_set_metric(settling, settled_at_s - selected.first()->time_s);
    return settling;
}

static QJsonObject app_signal_metric_json(const app_signal_metric_t &metric)
{
    QJsonObject json;
    json.insert(QStringLiteral("available"), metric.available);
    json.insert(QStringLiteral("value"), metric.available
                ? QJsonValue(metric.value) : QJsonValue(QJsonValue::Null));
    json.insert(QStringLiteral("reason"), metric.reason);
    json.insert(QStringLiteral("unit"), metric.unit);
    return json;
}

} // namespace

app_signal_result_t app_signal_analyze_window(
    const QVector<app_signal_sample_t> &samples, app_signal_channel_t channel,
    double t1_s, double t2_s, quint64 segment_id)
{
    app_signal_result_t result;
    result.channel = channel;
    result.segment_id = segment_id;
    result.requested_t1_s = t1_s;
    result.requested_t2_s = t2_s;
    app_signal_set_units(result);
    if ((channel != APP_SIGNAL_TEMPERATURE && channel != APP_SIGNAL_HUMIDITY)
            || !std::isfinite(t1_s) || !std::isfinite(t2_s) || t2_s < t1_s) {
        app_signal_reject_selection(result, QStringLiteral("INVALID_WINDOW"));
        return result;
    }

    QVector<const app_signal_sample_t *> selected;
    selected.reserve(samples.size());
    QString invalid_selection;
    for (const app_signal_sample_t &sample : samples) {
        // Membership cannot be established for a nonfinite time. Conservatively
        // reject the requested segment instead of silently dropping the record.
        if (!std::isfinite(sample.time_s)) {
            if (sample.segment_id == segment_id)
                invalid_selection = QStringLiteral("INVALID_TIMESTAMPS");
            continue;
        }
        if (sample.time_s < t1_s || sample.time_s > t2_s)
            continue;
        if (sample.segment_id != segment_id && invalid_selection.isEmpty())
            invalid_selection = QStringLiteral("MULTIPLE_SEGMENTS");
        if (!selected.isEmpty() && (!(sample.time_s > selected.last()->time_s)
             || !std::isfinite(sample.time_s - selected.last()->time_s)))
            invalid_selection = QStringLiteral("INVALID_TIMESTAMPS");
        selected.append(&sample);
        ++result.sample_count;
        if (app_signal_value_usable(sample))
            ++result.valid_sample_count;
        else
            ++result.invalid_sample_count;
    }
    if (!invalid_selection.isEmpty()) {
        app_signal_reject_selection(result, invalid_selection);
        return result;
    }
    if (selected.isEmpty()) {
        app_signal_reject_selection(result, QStringLiteral("INSUFFICIENT_DATA"));
        return result;
    }
    // Individually finite adjacent dt values can still have an unrepresentable
    // total span. Reject before division could turn that overflow into slope 0.
    if (!std::isfinite(selected.last()->time_s - selected.first()->time_s)) {
        app_signal_reject_selection(result, QStringLiteral("INVALID_TIMESTAMPS"));
        return result;
    }
    result.selection_valid = true;
    app_signal_set_metric(result.actual_tstart_s, selected.first()->time_s);
    app_signal_set_metric(result.actual_tend_s, selected.last()->time_s);

    app_signal_stats_t values;
    app_signal_stats_t timing;
    QVector<const app_signal_sample_t *> valid;
    valid.reserve(selected.size());
    for (int i = 0; i < selected.size(); ++i) {
        if (i > 0)
            timing.app_append_value(selected[i]->time_s - selected[i - 1]->time_s);
        if (app_signal_value_usable(*selected[i])) {
            values.app_append_value(selected[i]->value);
            valid.append(selected[i]);
        }
    }
    if (selected.size() >= 2) {
        const double duration = selected.last()->time_s - selected.first()->time_s;
        app_signal_set_metric(result.duration_s, duration);
        app_signal_set_metric(result.mean_dt_s, timing.mean);
        app_signal_set_metric(result.min_dt_s, timing.minimum);
        app_signal_set_metric(result.max_dt_s, timing.maximum);
        app_signal_set_metric(result.std_dt_s, timing.app_population_std());
        app_signal_set_metric(result.effective_sample_rate_hz, double(timing.count) / duration);
    }
    if (!valid.isEmpty()) {
        app_signal_set_metric(result.first_value, valid.first()->value);
        app_signal_set_metric(result.last_value, valid.last()->value);
        app_signal_set_metric(result.mean, values.mean);
        app_signal_set_metric(result.rms, values.app_rms());
        app_signal_set_metric(result.minimum, values.minimum);
        app_signal_set_metric(result.maximum, values.maximum);
        app_signal_set_metric(result.peak_to_peak, values.maximum - values.minimum);
        if (valid.size() >= 2) {
            app_signal_set_metric(result.delta, valid.last()->value - valid.first()->value);
            app_signal_set_metric(result.standard_deviation, values.app_population_std());
            app_signal_set_metric(result.average_slope,
                (valid.last()->value - valid.first()->value)
                / (valid.last()->time_s - valid.first()->time_s));
        }
    }

    if (valid.size() < APP_SIGNAL_RESPONSE_MIN_VALID) {
        app_signal_reject_response(result, QStringLiteral("INSUFFICIENT_DATA"));
        return result;
    }
    if (double(valid.size()) / selected.size() < APP_SIGNAL_RESPONSE_MIN_FRACTION) {
        app_signal_reject_response(result, QStringLiteral("TOO_MANY_INVALID_SAMPLES"));
        return result;
    }
    const int plateau_count = int(std::ceil(APP_SIGNAL_PLATEAU_FRACTION * valid.size()));
    app_signal_stats_t initial;
    app_signal_stats_t final;
    for (int i = 0; i < plateau_count; ++i) {
        initial.app_append_value(valid[i]->value);
        final.app_append_value(valid[valid.size() - plateau_count + i]->value);
    }
    const double amplitude = final.mean - initial.mean;
    app_signal_set_metric(result.initial_level, initial.mean);
    app_signal_set_metric(result.final_level, final.mean);
    app_signal_set_metric(result.response_amplitude, amplitude);
    if (!result.initial_level.available || !result.final_level.available
            || !result.response_amplitude.available) {
        app_signal_reject_response(result, QStringLiteral("NUMERICAL_OVERFLOW"));
        return result;
    }
    const double magnitude = std::abs(amplitude);
    const double scale = std::max(1.0, std::max(std::abs(initial.mean), std::abs(final.mean)));
    if (magnitude <= APP_SIGNAL_AMPLITUDE_RELATIVE_FLOOR * scale) {
        app_signal_reject_response(result, QStringLiteral("LOW_AMPLITUDE"));
        return result;
    }
    if (initial.maximum - initial.minimum > APP_SIGNAL_PLATEAU_MAX_RANGE * magnitude
            || initial.app_population_std() > APP_SIGNAL_PLATEAU_MAX_STD * magnitude) {
        app_signal_reject_response(result, QStringLiteral("NO_STABLE_INITIAL_VALUE"));
        return result;
    }
    if (final.maximum - final.minimum > APP_SIGNAL_PLATEAU_MAX_RANGE * magnitude
            || final.app_population_std() > APP_SIGNAL_PLATEAU_MAX_STD * magnitude) {
        app_signal_reject_response(result, QStringLiteral("NO_STABLE_FINAL_VALUE"));
        return result;
    }
    result.response_valid = true;
    result.response_direction = amplitude > 0 ? QStringLiteral("RISING") : QStringLiteral("FALLING");
    const app_signal_metric_t crossing10 = app_signal_find_crossing(selected, initial.mean, amplitude, 0.1);
    const app_signal_metric_t crossing90 = app_signal_find_crossing(selected, initial.mean, amplitude, 0.9);
    app_signal_metric_t &transition = amplitude > 0 ? result.rise_time_10_90_s
                                                   : result.fall_time_90_10_s;
    app_signal_metric_t &opposite = amplitude > 0 ? result.fall_time_90_10_s
                                                 : result.rise_time_10_90_s;
    app_signal_reject_metric(opposite, amplitude > 0 ? QStringLiteral("NOT_FALLING_RESPONSE")
                                                   : QStringLiteral("NOT_RISING_RESPONSE"));
    if (!crossing10.available)
        app_signal_reject_metric(transition, crossing10.reason);
    else if (!crossing90.available)
        app_signal_reject_metric(transition, crossing90.reason);
    else if (crossing90.value < crossing10.value)
        app_signal_reject_metric(transition, QStringLiteral("INVALID_CROSSING_ORDER"));
    else
        app_signal_set_metric(transition, crossing90.value - crossing10.value);
    double maximum_progress = 0;
    double minimum_progress = 0;
    for (const app_signal_sample_t *sample : valid) {
        const double progress = (sample->value - initial.mean) / amplitude;
        maximum_progress = std::max(maximum_progress, progress);
        minimum_progress = std::min(minimum_progress, progress);
    }
    app_signal_set_metric(result.overshoot_pct, std::max(0.0, maximum_progress - 1) * 100);
    app_signal_set_metric(result.undershoot_pct, std::max(0.0, -minimum_progress) * 100);
    result.settling_time_2pct_s = app_signal_find_settling(selected, final.mean, amplitude, 0.02, plateau_count);
    result.settling_time_5pct_s = app_signal_find_settling(selected, final.mean, amplitude, 0.05, plateau_count);
    return result;
}

QJsonObject app_signal_analysis_json(const app_signal_result_t &result,
                                    const QString &software_version)
{
    QJsonObject json;
    json.insert(QStringLiteral("classification"), QStringLiteral("SELECTED_WINDOW_SOFTWARE_ANALYSIS"));
    json.insert(QStringLiteral("channel"), result.channel == APP_SIGNAL_TEMPERATURE
                ? QStringLiteral("Temperature") : QStringLiteral("Humidity"));
    json.insert(QStringLiteral("software_version"), software_version);
    json.insert(QStringLiteral("segment_id"), QString::number(result.segment_id));
    json.insert(QStringLiteral("requested_t1_s"), std::isfinite(result.requested_t1_s)
                ? QJsonValue(result.requested_t1_s) : QJsonValue(QJsonValue::Null));
    json.insert(QStringLiteral("requested_t2_s"), std::isfinite(result.requested_t2_s)
                ? QJsonValue(result.requested_t2_s) : QJsonValue(QJsonValue::Null));
    json.insert(QStringLiteral("sample_count"), double(result.sample_count));
    json.insert(QStringLiteral("valid_sample_count"), double(result.valid_sample_count));
    json.insert(QStringLiteral("invalid_sample_count"), double(result.invalid_sample_count));
    json.insert(QStringLiteral("selection_valid"), result.selection_valid);
    json.insert(QStringLiteral("selection_reason"), result.selection_reason);
    json.insert(QStringLiteral("response_valid"), result.response_valid);
    json.insert(QStringLiteral("response_reason"), result.response_reason);
    json.insert(QStringLiteral("response_direction"), result.response_direction);
    QJsonObject basic;
    QJsonObject timing;
    QJsonObject response;
#define APP_SIGNAL_JSON(group, field) group.insert(QStringLiteral(#field), app_signal_metric_json(result.field))
    APP_SIGNAL_JSON(basic, actual_tstart_s); APP_SIGNAL_JSON(basic, actual_tend_s);
    APP_SIGNAL_JSON(basic, duration_s); APP_SIGNAL_JSON(basic, first_value);
    APP_SIGNAL_JSON(basic, last_value); APP_SIGNAL_JSON(basic, delta);
    APP_SIGNAL_JSON(basic, mean); APP_SIGNAL_JSON(basic, rms);
    APP_SIGNAL_JSON(basic, standard_deviation); APP_SIGNAL_JSON(basic, minimum);
    APP_SIGNAL_JSON(basic, maximum); APP_SIGNAL_JSON(basic, peak_to_peak);
    APP_SIGNAL_JSON(basic, average_slope);
    APP_SIGNAL_JSON(timing, mean_dt_s); APP_SIGNAL_JSON(timing, min_dt_s);
    APP_SIGNAL_JSON(timing, max_dt_s); APP_SIGNAL_JSON(timing, std_dt_s);
    APP_SIGNAL_JSON(timing, effective_sample_rate_hz);
    APP_SIGNAL_JSON(response, initial_level); APP_SIGNAL_JSON(response, final_level);
    APP_SIGNAL_JSON(response, response_amplitude); APP_SIGNAL_JSON(response, rise_time_10_90_s);
    APP_SIGNAL_JSON(response, fall_time_90_10_s); APP_SIGNAL_JSON(response, overshoot_pct);
    APP_SIGNAL_JSON(response, undershoot_pct); APP_SIGNAL_JSON(response, settling_time_2pct_s);
    APP_SIGNAL_JSON(response, settling_time_5pct_s);
#undef APP_SIGNAL_JSON
    json.insert(QStringLiteral("basic"), basic);
    json.insert(QStringLiteral("timing"), timing);
    json.insert(QStringLiteral("response"), response);
    QJsonObject definitions;
    definitions.insert(QStringLiteral("standard_deviation"), QStringLiteral("Population sqrt(sum((x-mean)^2)/N), Welford; N>=2"));
    definitions.insert(QStringLiteral("values"), QStringLiteral("Only valid finite values; no NA-to-zero or physical clamping"));
    definitions.insert(QStringLiteral("low_sample_count"), QStringLiteral("N=1: first/last/mean/RMS/min/max/peak-to-peak available; delta/std/slope N/A; time interval metrics need 2 records"));
    definitions.insert(QStringLiteral("timing"), QStringLiteral("All selected timestamps including invalid-value records; mean/min/max/population std of adjacent dt; rate=(records-1)/duration"));
    definitions.insert(QStringLiteral("average_slope"), QStringLiteral("(last valid value-first valid value)/(last valid source time-first valid source time)"));
    definitions.insert(QStringLiteral("plateau"), QStringLiteral("First/last ceil(10% valid samples), minimum 3; spread<=5% amplitude and population std<=2% amplitude"));
    definitions.insert(QStringLiteral("response_gate"), QStringLiteral("Software observations only: >=30 valid, >=90% valid, positive time span; abs(amplitude)>1e-6*max(1,abs(initial),abs(final))"));
    definitions.insert(QStringLiteral("crossings"), QStringLiteral("First oriented 10% and 90% crossings; linear interpolation only between adjacent valid selected records, never across NA"));
    definitions.insert(QStringLiteral("overshoot_undershoot"), QStringLiteral("progress=(x-initial)/signed amplitude; overshoot=max(0,max(progress)-1)*100; undershoot=max(0,-min(progress))*100"));
    definitions.insert(QStringLiteral("settling"), QStringLiteral("final +/- 2% or 5% abs(response amplitude); continuous valid in-band suffix with at least a plateau length; interpolate boundary only across adjacent valid records; seconds from first selected record"));
    definitions.insert(QStringLiteral("segment_id"), QStringLiteral("Decimal string preserves quint64 identity"));
    definitions.insert(QStringLiteral("hardware_status"), QStringLiteral("NOT TESTED ON HARDWARE; no system identification or sensor accuracy claim"));
    json.insert(QStringLiteral("metric_definitions"), definitions);
    return json;
}
