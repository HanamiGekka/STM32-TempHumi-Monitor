"""Explicit SIM -> unchanged C TH3 -> growing file -> Qt software demo.

The launcher owns only its two children. Closing Qt or Ctrl+C stops the stream;
it never opens a serial port or selects a simulated MCU source.
"""
from pathlib import Path
import argparse
import ctypes
import json
import os
import signal
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone
from desktop_version import desktop_version, desktop_version_path
import uuid
import math

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build_verify/software_polish_20261006"
QT_SOURCE = ROOT / "UIDesktop"


def environment():
    # Existing kits are searched on E, C, D in that order; no installation.
    requested = os.environ.get("TEMP_HUMI_QT_ROOT")
    kits = ([Path(requested)] if requested else []) + [
        Path(f"{drive}:/Qt/Qt5.14.2") for drive in ("E", "C", "D")]
    for kit in kits:
        qt = kit / "5.14.2/mingw73_64"
        gcc = kit / "Tools/mingw730_64/bin"
        if (qt / "bin/qmake.exe").is_file() and (gcc / "gcc.exe").is_file():
            env = os.environ.copy()
            env["PATH"] = str(qt / "bin") + os.pathsep + str(gcc) + os.pathsep + env["PATH"]
            env["QT_PLUGIN_PATH"] = str(qt / "plugins")
            return qt, gcc, env
    raise RuntimeError("Qt 5.14.2 / MinGW64 kit unavailable; set TEMP_HUMI_QT_ROOT.")


def command_log(command, cwd, env, destination):
    with destination.open("wb") as log:
        p = subprocess.run([str(x) for x in command], cwd=cwd, env=env,
                           stdout=log, stderr=subprocess.STDOUT)
    if p.returncode:
        raise RuntimeError(f"Build failed ({p.returncode}); see {destination}")


def prepare_executables(qt, gcc, env, rebuild=False, configuration="debug"):
    if configuration not in ("debug", "release"):
        raise ValueError("configuration must be debug or release")
    qt_build = BUILD / ("qt_" + configuration)
    host_build = BUILD / "host"
    qt_build.mkdir(parents=True, exist_ok=True)
    host_build.mkdir(parents=True, exist_ok=True)
    exe = qt_build / configuration / "UIDesktop.exe"
    sources = [p for p in QT_SOURCE.rglob("*") if p.suffix in (".cpp", ".h", ".ui", ".pro", ".qrc", ".rc", ".html")]
    sources.append(ROOT / "VERSION")
    sources.append(desktop_version_path())
    if rebuild or not exe.exists() or any(p.stat().st_mtime > exe.stat().st_mtime for p in sources):
        command_log([qt / "bin/qmake.exe", QT_SOURCE / "UIDesktop.pro", "-spec", "win32-g++",
                     "CONFIG+=" + configuration, "CONFIG-=" + ("release" if configuration == "debug" else "debug")], qt_build, env, qt_build / "configure.log")
        command_log([gcc / "mingw32-make.exe", "-f", "Makefile." + configuration.capitalize(), "-j2"],
                    qt_build, env, qt_build / "build.log")
    sim = host_build / "simulation_stream.exe"
    c_sources = [ROOT / "tools/host_src/simulation_stream.c"] + [ROOT / f"Core/Src/{name}.c" for name in
        ("app_measurement_source", "app_measurement_source_sim", "app_protocol", "algo_sensor_convert")]
    if rebuild or not sim.exists() or any(p.stat().st_mtime > sim.stat().st_mtime for p in c_sources):
        command_log([gcc / "gcc.exe", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                     "-static-libgcc", "-include", "stddef.h", "-I", ROOT / "Core/Inc",
                     *c_sources, "-lm", "-o", sim], host_build, env, host_build / "build_stream.log")
    return exe, sim


def read_qt_status(path):
    """A startup/momentary telemetry gap is not a recording failure."""
    read_error = ""
    for attempt in range(3):
        try:
            value = json.loads(path.read_text(encoding="utf-8"))
            if isinstance(value, dict) and value:
                return value, ""
            read_error = "Qt status is not a populated JSON object"
        except (OSError, ValueError) as exc:
            read_error = str(exc)
        if attempt < 2:
            time.sleep(0.05)
    return {}, read_error


def qt_status_failure(status, final=False):
    """Recording errors are fatal; temporary input errors may recover while live."""
    if not status:
        return "Final Qt status unavailable" if final else ""
    record = status.get("record_summary", {})
    if final and "record_summary" not in status:
        return "Final Qt recorder summary unavailable"
    if not isinstance(record, dict):
        return "Final Qt recorder summary unavailable" if final else ""
    error = record.get("recording_error")
    if error:
        return "Qt recording error: " + str(error)
    # A Stop/Draw window is legitimately inactive. Do not infer failure from
    # recording=false alone, or from recoverable file access/availability errors.
    if final and status.get("state") == "ERROR":
        return "Qt finished in ERROR: " + str(status.get("error") or "Unspecified error")
    if final and (status.get("recording") or record.get("active")):
        return "Qt recording did not finish cleanly"
    return ""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duration", type=float, default=0, help="Seconds; 0 until Qt exits / Ctrl+C.")
    parser.add_argument("--run-dir", type=Path, help="Fresh output directory; default creates a unique session.")
    parser.add_argument("--input", type=Path, default=ROOT / "tools/qt_sim_package/output/realtime_th3.txt")
    parser.add_argument("--headless", action="store_true")
    parser.add_argument("--exercise-restart", action="store_true", help="Automated software Stop/Draw halfway through a timed run.")
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--rebuild", action="store_true")
    parser.add_argument("--configuration", choices=("debug", "release"), default="debug")
    args = parser.parse_args()
    if not math.isfinite(args.duration) or args.duration < 0 or (args.duration and args.duration < 1):
        parser.error("duration must be 0 or at least 1 second")
    if args.exercise_restart and args.duration < 10:
        parser.error("exercise-restart requires a timed run of at least 10 seconds")
    qt, gcc, env = environment()
    exe, sim = prepare_executables(qt, gcc, env, args.rebuild, args.configuration)
    if args.build_only:
        print(json.dumps({"qt": str(exe), "simulator": str(sim), "classification": "SIMULATED"}))
        return 0
    if os.name == "nt":
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)
    if args.headless:
        env["QT_QPA_PLATFORM"] = "offscreen"
        env["QT_QPA_FONTDIR"] = str(Path(os.environ["SystemRoot"]) / "Fonts")
    run_dir = (args.run_dir or BUILD / "demo_runtime" /
               (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ_") + uuid.uuid4().hex[:8])).resolve()
    run_dir.mkdir(parents=True, exist_ok=True)
    input_path = args.input.resolve()
    input_path.parent.mkdir(parents=True, exist_ok=True)
    stop_path = run_dir / "stop.request"
    if stop_path.exists():
        raise RuntimeError(f"Use a fresh run directory (stale stop request): {stop_path}")
    stop = threading.Event()
    ready = threading.Event()
    failures = []
    counts = {"input_line_count": 0, "th3_count": 0}
    child_flag = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
    simulator = qt_process = None
    thread = None
    started = time.monotonic()
    samples = []
    status_failures = []
    status_read_errors = []
    final_qt_status = {}
    exit_reason = "Qt closed"
    qt_exit = sim_exit = None

    def copy_stream():
        try:
            # Exact C bytes, including its CRLF; no temperature/protocol conversion.
            with input_path.open("wb", buffering=0) as output:
                while not stop.is_set():
                    line = simulator.stdout.readline()
                    if not line:
                        break
                    output.write(line)
                    counts["input_line_count"] += 1
                    if line.startswith(b"TH3,"):
                        counts["th3_count"] += 1
                    ready.set()
        except Exception as exc:
            failures.append(str(exc))
        finally:
            simulator.stdout.close()  # C detects closed consumer and returns 0.

    def working_set(pid):
        if os.name != "nt":
            return None
        class COUNTERS(ctypes.Structure):
            _fields_ = [("cb", ctypes.c_ulong), ("PageFaultCount", ctypes.c_ulong)] + [
                (n, ctypes.c_size_t) for n in ("PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
                 "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]
        kernel = ctypes.windll.kernel32
        kernel.OpenProcess.restype = ctypes.c_void_p
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel.OpenProcess(0x0400 | 0x0010, False, pid)
        try:
            info = COUNTERS()
            info.cb = ctypes.sizeof(info)
            if handle and ctypes.windll.psapi.GetProcessMemoryInfo(ctypes.c_void_p(handle), ctypes.byref(info), info.cb):
                return {"working_set_bytes": info.WorkingSetSize, "private_bytes": info.PrivateUsage}
        finally:
            if handle:
                kernel.CloseHandle(handle)
        return None

    with (run_dir / "simulator.stderr.log").open("wb") as sim_log, (run_dir / "qt.stderr.log").open("wb") as qt_log:
        try:
            simulator = subprocess.Popen([str(sim)], cwd=sim.parent, env=env, stdout=subprocess.PIPE,
                                         stderr=sim_log, creationflags=child_flag)
            thread = threading.Thread(target=copy_stream, name="C_TH3_file_writer")
            thread.start()
            if not ready.wait(10) or failures or simulator.poll() is not None:
                raise RuntimeError(f"C SIM produced no ready output: {failures}")
            command = [str(exe), "--input", str(input_path), "--input-mode", "FILE_STREAM", "--autostart",
                       "--record-dir", str(run_dir / "sessions"), "--status-file", str(run_dir / "qt_status.json"),
                       "--stop-file", str(stop_path), "--screenshot", str(run_dir / "simulated.png")]
            if args.exercise_restart:
                command += ["--restart-after", str(args.duration / 2)]
            qt_process = subprocess.Popen(command, cwd=exe.parent, env=env, stdout=qt_log, stderr=qt_log,
                                          creationflags=child_flag)
            next_sample = 0.0
            while qt_process.poll() is None:
                elapsed = time.monotonic() - started
                if simulator.poll() is not None or failures:
                    raise RuntimeError(f"Simulator stopped unexpectedly: {simulator.poll()}, {failures}")
                if args.duration and elapsed >= args.duration:
                    exit_reason = "Duration reached"
                    break
                if elapsed >= next_sample:
                    status_path = run_dir / "qt_status.json"
                    status, read_error = read_qt_status(status_path)
                    if read_error and (status_path.exists() or elapsed > 2):
                        status_read_errors.append({"elapsed_s": elapsed, "error": read_error})
                    if not status:
                        # Retry soon instead of creating a false good sample or
                        # leaving a 60-second telemetry gap after an atomic rename.
                        next_sample = elapsed + 0.25
                        time.sleep(0.1)
                        continue
                    samples.append({"elapsed_s": elapsed, "qt_alive": True, "simulator_alive": True,
                                    "input_th3": counts["th3_count"], "memory": working_set(qt_process.pid),
                                    "qt_status": status})
                    status_error = qt_status_failure(status)
                    if status_error:
                        status_failures.append({"phase": "runtime", "elapsed_s": elapsed,
                                                "error": status_error, "qt_status": status})
                        raise RuntimeError(status_error)
                    print(f"[SIMULATED] {elapsed:.1f}s input={counts['th3_count']} Qt running", flush=True)
                    next_sample += 30
                time.sleep(0.1)
        except KeyboardInterrupt:
            exit_reason = "Ctrl+C requested"
        except Exception as exc:
            failures.append(str(exc))
            exit_reason = "launcher error"
        finally:
            # Stop producer before asking Qt to drain the final complete input lines.
            stop.set()
            if thread:
                thread.join(timeout=3)
            if simulator:
                try:
                    sim_exit = simulator.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    failures.append("Simulator required forced termination")
                    simulator.terminate()
                    sim_exit = simulator.wait(timeout=3)
            if qt_process and qt_process.poll() is None:
                try:
                    stop_path.write_text("STOP\n", encoding="ascii")
                except OSError as exc:
                    # Cleanup must still reap our Qt child when the control file
                    # cannot be written; the failure stays visible in the summary.
                    failures.append("Stop request unavailable: " + str(exc))
                    qt_process.terminate()
                try:
                    qt_exit = qt_process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    failures.append("Qt required forced termination")
                    qt_process.terminate()
                    qt_exit = qt_process.wait(timeout=3)
            elif qt_process:
                qt_exit = qt_process.returncode
            if qt_process:
                # Check after clean-exit/drain, when the final status includes
                # stop-time raw/CSV/metadata failures even if Qt itself exits 0.
                final_qt_status, read_error = read_qt_status(run_dir / "qt_status.json")
                if read_error:
                    status_read_errors.append({"phase": "final", "error": read_error})
                status_error = qt_status_failure(final_qt_status, final=True)
                if status_error:
                    status_failures.append({"phase": "final", "error": status_error,
                                            "qt_status": final_qt_status})
                    if status_error not in failures:
                        failures.append(status_error)
    summary = {"classification": "SIMULATED", "protocol": "TH3", "duration_s": time.monotonic() - started,
               "software_version": desktop_version(),
               "configuration": args.configuration,
               "input_path": str(input_path), **counts, "qt_exit": qt_exit, "simulator_exit": sim_exit,
               "qt_pid": qt_process.pid if qt_process else None, "simulator_pid": simulator.pid if simulator else None,
               "orphan_process_count": int(bool(qt_process and qt_process.poll() is None)) + int(bool(simulator and simulator.poll() is None)),
               "memory_samples": samples, "failures": failures, "exit_reason": exit_reason,
               "final_qt_status": final_qt_status, "qt_status_failures": status_failures,
               "status_read_errors": status_read_errors,
               "result": "PASS" if not failures and qt_exit == 0 and sim_exit == 0 else "FAIL"}
    (run_dir / "launcher_summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in summary.items() if k != "memory_samples"}, indent=2), flush=True)
    return 0 if not failures and qt_exit == 0 and sim_exit == 0 else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        raise SystemExit(1)
