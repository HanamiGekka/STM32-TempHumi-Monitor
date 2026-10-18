#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QThread>
#include <thread>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "app_session_recorder.h"

static bool write_bytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

static app_session_options_t options(const QString &output, const QString &input,
                                     const QString &mode, const QString &classification)
{
    app_session_options_t result;
    result.output_directory = output;
    result.input_path = input;
    result.input_mode = mode;
    result.classification = classification;
    result.git_head = "0f057baa4df0d549545e4dceec47e2bfe945a5f4";
    result.qt_build_identity = "RECORDER_FIXTURE_QT_5.14.2_MINGW64_DEBUG";
    return result;
}

static QByteArray read_bytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

static bool metadata_lock_cases(app_session_recorder_t &recorder, const QString &output,
                                const QString &fixture_path, const QList<QByteArray> &lines,
                                QJsonObject &summary)
{
#ifdef Q_OS_WIN
    if (lines.size() != 121) return false;
    if (!recorder.app_start_session(options(output, fixture_path, "REPLAY", "FIXTURE"))) return false;
    const QString transient_dir = recorder.app_session_directory();
    const QString transient_path = transient_dir + "/metadata.json";
    app_th3_timeline_t transient_timeline;
    auto frame = app_th3_parse_line(lines[0]);
    if (!recorder.app_record_line(frame, transient_timeline.app_th3_append_frame(frame), QDateTime::currentDateTimeUtc())) return false;
    QThread::msleep(1050); // Force the next record to take the normal periodic metadata path.
    HANDLE transient_lock = CreateFileW(reinterpret_cast<LPCWSTR>(transient_path.utf16()), GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr); // Deliberately no FILE_SHARE_DELETE.
    if (transient_lock == INVALID_HANDLE_VALUE) return false;
    std::thread releaser([transient_lock]() { Sleep(80); CloseHandle(transient_lock); });
    QElapsedTimer clock;
    clock.start();
    frame = app_th3_parse_line(lines[1]);
    const bool recovered = recorder.app_record_line(frame, transient_timeline.app_th3_append_frame(frame), QDateTime::currentDateTimeUtc());
    const qint64 recovered_ms = clock.elapsed();
    releaser.join();
    QJsonObject transient;
    transient["session"] = transient_dir;
    transient["lock_release_delay_ms"] = 80;
    transient["record_elapsed_ms"] = static_cast<double>(recovered_ms);
    transient["record_succeeded"] = recovered;
    transient["active_after_record"] = recorder.app_session_active();
    transient["error"] = recorder.app_session_error();
    bool all_records = recovered;
    if (recovered) {
        for (int index = 2; index < lines.size(); ++index) {
            frame = app_th3_parse_line(lines[index]);
            all_records &= recorder.app_record_line(frame, transient_timeline.app_th3_append_frame(frame), QDateTime::currentDateTimeUtc());
        }
    }
    transient["remaining_records_succeeded"] = all_records;
    transient["stop_succeeded"] = recorder.app_stop_session("TRANSIENT_LOCK_RECOVERED");
    transient["counts"] = recorder.app_session_summary();
    summary["metadata_lock_transient"] = transient;

    if (!recorder.app_start_session(options(output, fixture_path, "REPLAY", "FIXTURE"))) return false;
    const QString persistent_dir = recorder.app_session_directory();
    const QString persistent_path = persistent_dir + "/metadata.json";
    app_th3_timeline_t persistent_timeline;
    frame = app_th3_parse_line(lines[0]);
    if (!recorder.app_record_line(frame, persistent_timeline.app_th3_append_frame(frame), QDateTime::currentDateTimeUtc())) return false;
    QThread::msleep(1050);
    const QByteArray previous_metadata = read_bytes(persistent_path);
    HANDLE persistent_lock = CreateFileW(reinterpret_cast<LPCWSTR>(persistent_path.utf16()), GENERIC_READ,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                         FILE_ATTRIBUTE_NORMAL, nullptr);
    if (persistent_lock == INVALID_HANDLE_VALUE) return false;
    clock.restart();
    frame = app_th3_parse_line(lines[1]);
    const bool persistent_result = recorder.app_record_line(frame, persistent_timeline.app_th3_append_frame(frame), QDateTime::currentDateTimeUtc());
    const qint64 persistent_ms = clock.elapsed();
    const QByteArray still_locked_metadata = read_bytes(persistent_path);
    CloseHandle(persistent_lock);
    QJsonObject persistent;
    persistent["session"] = persistent_dir;
    persistent["record_elapsed_ms"] = static_cast<double>(persistent_ms);
    persistent["record_failed"] = !persistent_result;
    persistent["active_after_failure"] = recorder.app_session_active();
    persistent["error"] = recorder.app_session_error();
    persistent["original_atomic_metadata_preserved"] = previous_metadata == still_locked_metadata;
    persistent["previous_metadata_valid_json"] = QJsonDocument::fromJson(still_locked_metadata).isObject();
    frame = app_th3_parse_line(lines[2]);
    persistent["subsequent_record_rejected"] = !recorder.app_record_line(frame, persistent_timeline.app_th3_append_frame(frame), QDateTime::currentDateTimeUtc());
    persistent["stop_reports_failure"] = !recorder.app_stop_session("PERSISTENT_LOCK_EXPECTED_FAILURE");
    persistent["counts"] = recorder.app_session_summary();
    summary["metadata_lock_persistent"] = persistent;
    summary["metadata_lock_mechanism"] = "Windows CreateFileW GENERIC_READ + FILE_SHARE_READ|FILE_SHARE_WRITE; FILE_SHARE_DELETE intentionally omitted";
    return true;
#else
    Q_UNUSED(recorder); Q_UNUSED(output); Q_UNUSED(fixture_path); Q_UNUSED(lines); Q_UNUSED(summary);
    return false;
#endif
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationVersion(QStringLiteral(APP_SOFTWARE_VERSION));
    if (argc != 3) return 2;
    const QString output = QString::fromLocal8Bit(argv[1]);
    const QString fixture_path = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(output);
    QFile fixture(fixture_path);
    if (!fixture.open(QIODevice::ReadOnly)) return 3;

    app_session_recorder_t recorder;
    QJsonObject summary;
    if (!recorder.app_start_session(options(output, fixture_path, "REPLAY", "FIXTURE"))) return 4;
    summary["acceptance_session"] = recorder.app_session_directory();
    app_th3_timeline_t timeline;
    quint64 sequence = 0;
    while (!fixture.atEnd()) {
        const auto frame = app_th3_parse_line(fixture.readLine());
        const auto time = timeline.app_th3_append_frame(frame);
        const auto received = QDateTime::fromMSecsSinceEpoch(1791216000000LL + sequence++, Qt::UTC);
        if (!recorder.app_record_line(frame, time, received)) return 5;
    }
    if (!recorder.app_stop_session("FIXTURE_COMPLETE")) return 6;
    summary["acceptance_counts"] = recorder.app_session_summary();

    const QByteArray mix =
        "BOOT OK\r\n"
        "[ADS_DBG] value=NA, reason=\"fixture\"\r\n"
        "TH,25.00,50.00\r\n"
        "TH3,SIM,0,1,3,0,0,1,29.00,60.00,NA,NA,NA,NA,NA,NA,NA,NA,SIM,SIM,1,1,0\r\n"
        "TH3,REAL,4294967290,1,3,256,7,5,-5.50,0.00,NA,NA,NA,NA,NA,NA,NA,NA,REAL,REAL,1,1,0\r\n"
        "TH3,REAL,5,0,1,259,7,5,25.00,NA,NA,NA,NA,NA,NA,NA,NA,NA,REAL,REAL,1,0,0\r\n"
        "TH3,SIM,10,1,3,0,0,1,broken,60.00,NA,NA,NA,NA,NA,NA,NA,NA,SIM,SIM,1,1,0\r\n"
        "TH3,REAL,10,1,511,256,7,8,25.50,50.25,1.001,2.002,49.5,50.25,-12,32767,244800,97920,REAL,REAL,1,1,0\r\n"
        "TH3,REAL,20,0,3,256,7,5,NA,80.00,NA,NA,NA,NA,NA,NA,NA,NA,REAL,REAL,0,1,0\r\n";
    const QString mix_path = output + "/recorder_mixed_input.txt";
    if (!write_bytes(mix_path, mix)) return 7;
    if (!recorder.app_start_session(options(output, mix_path, "REPLAY", "FIXTURE"))) return 8;
    summary["mixed_session"] = recorder.app_session_directory();
    app_th3_timeline_t mixed_timeline;
    for (const QByteArray &piece : mix.split('\n')) {
        if (piece.isEmpty()) continue;
        const auto frame = app_th3_parse_line(piece + '\n');
        const auto time = mixed_timeline.app_th3_append_frame(frame);
        if (!recorder.app_record_line(frame, time, QDateTime::currentDateTimeUtc())) return 9;
    }
    mixed_timeline.app_th3_reset_segment("INPUT_REPLACED");
    const QByteArray after_reset = "TH3,SIM,0,1,3,0,0,1,20.00,40.00,NA,NA,NA,NA,NA,NA,NA,NA,SIM,SIM,1,1,0\r\n";
    const auto reset_frame = app_th3_parse_line(after_reset);
    if (!recorder.app_record_line(reset_frame, mixed_timeline.app_th3_append_frame(reset_frame),
                                  QDateTime::currentDateTimeUtc())) return 10;
    if (!recorder.app_stop_session("STOP_RESTART_REGRESSION")) return 11;
    summary["mixed_counts"] = recorder.app_session_summary();

    if (!recorder.app_start_session(options(output, fixture_path, "FILE_STREAM", "SIMULATED"))) return 12;
    summary["real_in_stream_session"] = recorder.app_session_directory();
    const auto real = app_th3_parse_line("TH3,REAL,0,1,3,256,0,1,24.00,55.00,NA,NA,NA,NA,NA,NA,NA,NA,REAL,REAL,1,1,0\r\n");
    app_th3_timeline_t real_timeline;
    if (!recorder.app_record_line(real, real_timeline.app_th3_append_frame(real), QDateTime::currentDateTimeUtc())) return 13;
    if (!recorder.app_stop_session("FILE_IS_NOT_LIVE_REAL")) return 14;

    if (!recorder.app_start_session(options(output, fixture_path, "REPLAY", "FIXTURE"))) return 20;
    summary["overflow_fragment_session"] = recorder.app_session_directory();
    const QByteArray oversized = QByteArray(70000, 'x') + "\r\n";
    if (!write_bytes(output + "/overflow_original.bin", oversized)) return 21;
    for (const QByteArray &bytes : {oversized.left(65536), oversized.mid(65536)}) {
        app_th3_frame_t fragment;
        fragment.raw_line = bytes;
        fragment.parse_status = APP_TH3_MALFORMED;
        fragment.diagnostic = "LINE_TOO_LONG";
        if (!recorder.app_record_line(fragment, app_th3_time_t(), QDateTime::currentDateTimeUtc())) return 22;
    }
    if (!recorder.app_stop_session("OVERSIZED_INPUT_RETAINED")) return 23;

    const QString blocker = output + "/output_is_file";
    if (!write_bytes(blocker, "fixture")) return 15;
    summary["bad_output_rejected"] = !recorder.app_start_session(options(blocker, fixture_path, "REPLAY", "FIXTURE"));
    summary["bad_output_error"] = recorder.app_session_error();
    if (!recorder.app_start_session(options(output, fixture_path, "REPLAY", "FIXTURE"))) return 16;
    summary["metadata_failure_session"] = recorder.app_session_directory();
    const QString metadata_path = recorder.app_session_directory() + "/metadata.json";
    if (!QFile::rename(metadata_path, metadata_path + ".original")) return 17;
    if (!QDir().mkdir(metadata_path)) return 18;
    summary["metadata_write_failure_rejected"] = !recorder.app_stop_session("EXPECTED_METADATA_ERROR");
    summary["metadata_write_failure_error"] = recorder.app_session_error();
    summary["metadata_failure_active"] = recorder.app_session_active();
    fixture.seek(0);
    QList<QByteArray> lock_lines;
    while (!fixture.atEnd()) lock_lines.append(fixture.readLine());
    if (!metadata_lock_cases(recorder, output, fixture_path, lock_lines, summary)) return 24;
    QFile summary_file(output + "/recorder_probe_summary.json");
    if (!summary_file.open(QIODevice::WriteOnly)) return 19;
    summary_file.write(QJsonDocument(summary).toJson(QJsonDocument::Indented));
    QTextStream(stdout) << QJsonDocument(summary).toJson(QJsonDocument::Compact) << '\n';
    return 0;
}
