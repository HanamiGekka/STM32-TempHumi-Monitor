"""PHASE E: actual QtCore recorder, exact fixture and I/O failure reconciliation.

Invoke only after PHASE D has completed. Runtime sessions are kept under ignored
build_verify; an immutable run-numbered evidence copy is exported to this EXP.
"""
from pathlib import Path
import argparse
import csv
import hashlib
import json
import os
import shutil
import subprocess
from datetime import datetime

ROOT = Path(__file__).resolve().parents[2]
EXP = ROOT / "build_verify/recorder_evidence"
EXP.mkdir(parents=True, exist_ok=True)
import sys
sys.path.insert(0, str(ROOT / "tools"))
from run_software_demo import environment
QT, MINGW, _ = environment()
FIXTURE = ROOT / "tools/qt_sim_package/samples/TH3_sample.txt"
FIELDS = ["frame_source", "source_timestamp_ms", "valid", "valid_flags", "error_flags",
          "calibration_id", "sample_count", "temperature_c", "humidity_rh", "v_ntc",
          "v_chs1101", "rh_pwm", "rh_adc", "raw_ntc_code", "raw_chs1101_code",
          "pwm_period_ticks", "pwm_high_ticks", "temperature_source", "humidity_source",
          "temperature_valid", "humidity_valid", "mode_generation"]


def command(args, cwd, log, env):
    result = subprocess.run([str(x) for x in args], cwd=cwd, env=env, capture_output=True,
                            timeout=120)
    log.write_bytes(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"Command failed {result.returncode}: {args}; see {log}")
    return result


def csv_rows(directory):
    with (directory / "parsed.csv").open(encoding="utf-8", newline="") as file:
        return list(csv.DictReader(file))


def metadata(directory):
    return json.loads((directory / "metadata.json").read_text(encoding="utf-8"))


def require(condition, description, checks):
    checks.append({"check": description, "result": "PASS" if condition else "FAIL"})
    if not condition:
        raise AssertionError(description)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase-d-completed", action="store_true", required=True)
    parser.add_argument("--run", type=int, default=1)
    parser.add_argument("--before-metadata-retry", action="store_true", help="Build the preserved recorder to capture native-lock RED.")
    args = parser.parse_args()
    if args.before_metadata_retry:
        parser.error("Historical RED source is recoverable from the history index; this runner tests current sources only.")
    work = ROOT / f"build_verify/software_polish_20261006/recorder_run{args.run}"
    evidence = EXP / "raw" / f"phase_e_recorder_run{args.run}"
    if work.exists() or evidence.exists():
        raise FileExistsError("Use a new run number; do not replace prior test evidence")
    work.mkdir(parents=True)
    evidence.mkdir(parents=True)
    env = dict(os.environ)
    env["PATH"] = str(QT / "bin") + os.pathsep + str(MINGW) + os.pathsep + env.get("PATH", "")
    project = work / "recorder_probe.pro"
    recorder_source = EXP / "raw/metadata_retry_before" if args.before_metadata_retry else ROOT / "UIDesktop"
    project.write_text(
        "QT += core\nQT -= gui\nCONFIG += console c++11 debug\nCONFIG -= app_bundle\n"
        "TARGET = recorder_probe\n"
        f'DEFINES += APP_SOFTWARE_VERSION=\\\\\\"{(ROOT / "VERSION").read_text(encoding="utf-8").strip()}\\\\\\"\n'
        f'INCLUDEPATH += "{recorder_source.as_posix()}" "{(ROOT / "UIDesktop").as_posix()}"\n'
        f'SOURCES += "{(ROOT / "tools/tests/recorder_probe.cpp").as_posix()}" '
        f'"{(ROOT / "UIDesktop/app_th3.cpp").as_posix()}" '
        f'"{(recorder_source / "app_session_recorder.cpp").as_posix()}"\n', encoding="utf-8")
    checks = []
    canonical_version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    summary = {"phase": "E", "data_classification": "FIXTURE / REPLAY",
               "hardware": "NOT TESTED ON HARDWARE", "checks": checks,
               "source_fixture": str(FIXTURE.relative_to(ROOT)),
               "source_fixture_sha256": hashlib.sha256(FIXTURE.read_bytes()).hexdigest()}
    try:
        command([QT / "bin/qmake.exe", project, "-spec", "win32-g++", "CONFIG+=debug"],
                work, evidence / "configure.log", env)
        command([MINGW / "mingw32-make.exe", "-j2"], work, evidence / "build.log", env)
        build_text = (evidence / "build.log").read_text(encoding="utf-8", errors="replace")
        warning_lines = [line for line in build_text.splitlines() if "warning:" in line.lower()]
        summary["build_warning_lines"] = warning_lines
        require(not warning_lines, "Recorder Debug build has zero warning diagnostics", checks)
        sessions = work / "sessions"
        command([work / "debug/recorder_probe.exe", sessions, FIXTURE], work,
                evidence / "probe.log", env)
        probe = json.loads((sessions / "recorder_probe_summary.json").read_text(encoding="utf-8"))
        shutil.copy2(sessions / "recorder_probe_summary.json", evidence)
        for native_label in ("metadata_lock_transient", "metadata_lock_persistent"):
            shutil.copytree(Path(probe[native_label]["session"]), evidence / f"{native_label}_fixture")
        accepted = Path(probe["acceptance_session"])
        accepted_rows = csv_rows(accepted)
        accepted_meta = metadata(accepted)
        require(accepted_meta["software_version"] == canonical_version, "Canonical software version in recorder metadata", checks)
        source_bytes = FIXTURE.read_bytes()
        lines = source_bytes.splitlines(keepends=True)
        require(len(lines) == 121 and len(accepted_rows) == 121,
                "121 input frames and 121 CSV rows", checks)
        require((accepted / "raw_input.log").read_bytes() == source_bytes,
                "Raw input is byte-identical to the 121-frame source", checks)
        offset = 0
        for index, (line, row) in enumerate(zip(lines, accepted_rows), 1):
            values = line.decode("utf-8").rstrip("\r\n").split(",")[1:]
            require([row[name] for name in FIELDS] == values,
                    f"Frame {index}: every TH3 field equals original producer lexeme", checks)
            require(row["parse_status"] == "PARSED" and row["input_mode"] == "REPLAY"
                    and row["classification"] == "FIXTURE" and row["record_kind"] == "LINE",
                    f"Frame {index}: accepted and explicitly FIXTURE / REPLAY", checks)
            require(int(row["raw_record_index"]) == index and int(row["raw_byte_offset"]) == offset,
                    f"Frame {index}: raw byte mapping is exact", checks)
            require(abs(float(row["session_elapsed_s"]) - int(values[1]) / 1000.0) < 1e-6,
                    f"Frame {index}: elapsed seconds use source timestamp", checks)
            datetime.fromisoformat(row["host_receive_time"].replace("Z", "+00:00"))
            offset += len(line)
        require(all(accepted_meta[key] == 121 for key in
                    ("parsed_th3_count", "csv_parsed_count", "raw_line_count", "csv_row_count")),
                "Input / parser / CSV / raw counts all equal 121", checks)
        require(all(accepted_meta[key] == 0 for key in
                    ("malformed_count", "invalid_frame_count", "ignored_diagnostic_count", "raw_fragment_count")),
                "121-frame acceptance has no invented errors or fragments", checks)
        require(accepted_meta["protocol"] == "TH3" and not accepted_meta["active"]
                and bool(accepted_meta["session_start_wall_time"])
                and bool(accepted_meta["session_end_wall_time"])
                and bool(accepted_meta["git_head"]) and bool(accepted_meta["qt_build_identity"]),
                "Session metadata has protocol, start/end, build identity and completed state", checks)
        mix = Path(probe["mixed_session"])
        mixed_rows = csv_rows(mix)
        mixed_meta = metadata(mix)
        require(mixed_meta["software_version"] == canonical_version, "Version retained in mixed fixture metadata", checks)
        extra = b"TH3,SIM,0,1,3,0,0,1,20.00,40.00,NA,NA,NA,NA,NA,NA,NA,NA,SIM,SIM,1,1,0\r\n"
        require((mix / "raw_input.log").read_bytes() ==
                (sessions / "recorder_mixed_input.txt").read_bytes() + extra,
                "Mixed diagnostic / legacy / malformed / TH3 raw bytes all retained", checks)
        require(len(mixed_rows) == 10 and mixed_meta["raw_line_count"] == 10
                and mixed_meta["parsed_th3_count"] == 6 and mixed_meta["malformed_count"] == 1
                and mixed_meta["ignored_diagnostic_count"] == 2 and mixed_meta["legacy_count"] == 1
                and mixed_meta["invalid_frame_count"] == 2,
                "Every mixed raw line has exactly one classified CSV row and reconciled counters", checks)
        require(mixed_rows[4]["error_flags"] == "256" and mixed_rows[4]["valid"] == "1"
                and mixed_rows[4]["temperature_c"] == "-5.50" and mixed_rows[4]["humidity_rh"] == "0.00",
                "Unrelated decimal error flags preserve valid negative temperature and real zero humidity", checks)
        require(mixed_rows[5]["valid"] == "0" and mixed_rows[5]["humidity_rh"] == "NA"
                and mixed_rows[5]["error_flags"] == "259" and mixed_rows[5]["valid_flags"] == "1",
                "Invalid frame, NA and multiple error bits remain explicit", checks)
        require(mixed_rows[6]["temperature_c"] == "NA" and mixed_rows[6]["parse_status"] == "MALFORMED"
                and "broken" in mixed_rows[6]["raw_line"],
                "Malformed values do not become zero and remain traceable", checks)
        expected_numeric = ["25.50", "50.25", "1.001", "2.002", "49.5", "50.25", "-12", "32767", "244800", "97920"]
        require([mixed_rows[7][name] for name in FIELDS[7:17]] == expected_numeric,
                "Constructed REAL fixture physical / signed raw / tick fields retained exactly", checks)
        require(mixed_rows[8]["valid"] == "0" and mixed_rows[8]["temperature_c"] == "NA"
                and mixed_rows[8]["humidity_rh"] == "80.00" and mixed_meta["invalid_frame_count"] == 2,
                "A missing engineering field cannot become valid through a claimed valid flag", checks)
        require(int(mixed_rows[-1]["segment_id"]) > int(mixed_rows[-2]["segment_id"])
                and mixed_rows[-1]["session_elapsed_s"] == "0.000000"
                and mixed_meta["restart_count"] >= 1,
                "Explicit input restart has a new segment and independent zero elapsed time", checks)
        require(all(row["classification"] == "REPLAY_FIXTURE" for row in mixed_rows
                    if row["frame_source"] == "REAL"),
                "All constructed REAL rows are REPLAY_FIXTURE", checks)
        real_stream = Path(probe["real_in_stream_session"])
        require(csv_rows(real_stream)[0]["classification"] == "REPLAY_FIXTURE"
                and metadata(real_stream)["classification"] == "REPLAY_FIXTURE",
                "A REAL frame in FILE_STREAM cannot claim SIMULATED or LIVE REAL", checks)
        fragments = Path(probe["overflow_fragment_session"])
        fragment_meta = metadata(fragments)
        require((fragments / "raw_input.log").read_bytes() == (sessions / "overflow_original.bin").read_bytes()
                and fragment_meta["raw_line_count"] == 1 and fragment_meta["raw_record_count"] == 2
                and fragment_meta["raw_fragment_count"] == 1 and fragment_meta["malformed_count"] == 2
                and len(csv_rows(fragments)) == 2,
                "Oversized input fragments preserve every byte without inventing a newline; counts distinguish records from lines", checks)
        require(probe["bad_output_rejected"] and bool(probe["bad_output_error"]),
                "Output directory failure is explicit", checks)
        require(probe["metadata_write_failure_rejected"] and bool(probe["metadata_write_failure_error"])
                and not probe["metadata_failure_active"],
                "Metadata write/commit failure is returned and recording stops", checks)
        require(len({probe["acceptance_session"], probe["mixed_session"], probe["real_in_stream_session"]}) == 3,
                "Restart creates unique sessions and preserves earlier records", checks)
        transient = probe["metadata_lock_transient"]
        transient_dir = Path(transient["session"])
        persistent = probe["metadata_lock_persistent"]
        persistent_dir = Path(persistent["session"])
        summary["metadata_lock_transient"] = transient
        summary["metadata_lock_persistent"] = persistent
        require(transient["record_succeeded"] and transient["active_after_record"]
                and transient["remaining_records_succeeded"] and transient["stop_succeeded"]
                and not transient["error"] and transient["counts"]["metadata_retry_count"] > 0
                and transient["counts"]["metadata_recovered_write_count"] >= 1,
                "Real Windows no-FILE_SHARE_DELETE lock released after 80ms recovers without stopping recording", checks)
        require(transient["record_elapsed_ms"] <= 250,
                "Transient atomic replacement stays within 150ms retry budget plus OS scheduling tolerance", checks)
        require((transient_dir / "raw_input.log").read_bytes() == source_bytes
                and len(csv_rows(transient_dir)) == 121
                and metadata(transient_dir)["parsed_th3_count"] == 121,
                "Transient metadata retry does not replay or drop raw/CSV: all 121 source frames retained once", checks)
        for index, row in enumerate(csv_rows(transient_dir)):
            require([row[name] for name in FIELDS] == lines[index].decode("ascii").rstrip("\r\n").split(",")[1:],
                    f"Transient lock frame {index+1}: all 22 source fields exact", checks)
        require(persistent["record_failed"] and not persistent["active_after_failure"]
                and persistent["subsequent_record_rejected"] and persistent["stop_reports_failure"]
                and "COMMIT" in persistent["error"] and persistent["counts"]["metadata_retry_count"] > 0,
                "Persistent Windows metadata lock remains an explicit COMMIT failure and stops recording", checks)
        require(100 <= persistent["record_elapsed_ms"] <= 250,
                "Persistent lock consumes one bounded retry budget, not a second error-handler budget", checks)
        require(persistent["original_atomic_metadata_preserved"] and persistent["previous_metadata_valid_json"],
                "Persistent lock preserves the complete preceding metadata JSON with no direct-write fallback", checks)
        require((persistent_dir / "raw_input.log").read_bytes() == b"".join(lines[:2])
                and len(csv_rows(persistent_dir)) == 2 and persistent["counts"]["raw_record_count"] == 2
                and persistent["counts"]["csv_parsed_count"] == 2,
                "Persistent metadata failure retains both already-written frames exactly once and rejects further writes", checks)
        for label, directory in (("acceptance_121", accepted), ("mixed_fixture", mix),
                                 ("real_file_fixture", real_stream), ("overflow_fragment_fixture", fragments)):
            shutil.copytree(directory, evidence / label)
        shutil.copy2(sessions / "recorder_mixed_input.txt", evidence)
        summary.update({"result": "PASS", "input_th3_count": 121, "parsed_count": 121,
                        "csv_parsed_count": 121, "raw_th3_count": 121,
                        "strict_field_comparisons": 121 * len(FIELDS),
                        "fixture_sessions_checked": 6, "io_failure_cases": 3, "native_metadata_lock_cases": 2})
    except Exception as error:
        summary.update({"result": "FAIL", "error": str(error)})
        raise
    finally:
        (evidence / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    # Windows pipe encodings need not support the Unicode paths exercised by
    # these tests. Keep the evidence UTF-8 and the console JSON ASCII-safe.
    print(json.dumps({key: value for key, value in summary.items() if key != "checks"}, ensure_ascii=True))


if __name__ == "__main__":
    main()
