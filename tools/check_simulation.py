"""Build the production C simulation on the host and inspect its dynamic output.

Run from any directory: python tools/check_simulation.py
Only Python's standard library and a C compiler on PATH are required.
"""

from __future__ import annotations

import csv
import math
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build_verify" / "simulation_check"
PROBE = OUTPUT / ("simulation_probe.exe" if sys.platform == "win32" else "simulation_probe")
TRACE_CSV = OUTPUT / "simulation_trace.csv"
TH3_TEXT = OUTPUT / "th3_sim.txt"
CHART_SVG = OUTPUT / "simulation_trace.svg"
SUMMARY = OUTPUT / "summary.txt"
EXPECTED_STEPS = 1200


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def build_probe() -> None:
    compiler = shutil.which("gcc") or shutil.which("clang")
    require(compiler is not None, "No gcc/clang found on PATH")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    command = [
        compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-include", "stddef.h", "-I", str(ROOT / "Core" / "Inc"),
        str(ROOT / "tools" / "host_src" / "simulation_probe.c"),
        *[str(ROOT / "Core" / "Src" / name) for name in (
            "app_measurement_source.c", "app_measurement_source_sim.c",
            "app_protocol.c", "algo_sensor_convert.c",
        )],
        "-lm", "-o", str(PROBE),
    ]
    subprocess.run(command, check=True, cwd=ROOT)


def inspect(raw_output: str) -> tuple[list[tuple[int, float, float]], list[str], list[str]]:
    trace: list[tuple[int, float, float]] = []
    frames: list[str] = []
    vectors: list[list[str]] = []
    for line in raw_output.splitlines():
        if not line:
            continue
        fields = next(csv.reader([line]))
        if fields[0] == "TRACE":
            require(len(fields) == 7, "Malformed TRACE row")
            timestamp_ms, temp_c, rh = int(fields[1]), float(fields[2]), float(fields[3])
            require(timestamp_ms == len(trace) * 100, "TRACE timestamp gap")
            require(math.isfinite(temp_c) and math.isfinite(rh), "Nonfinite simulation value")
            require(fields[4:] == ["1", "3", "0"], "Simulation validity flags changed")
            trace.append((timestamp_ms, temp_c, rh))
        elif fields[0] == "TH3":
            require(len(fields) == 23, "TH3 field count changed")
            require(fields[1] == "SIM", "TH3 source is not SIM")
            require(fields[3:8] == ["1", "3", "0", "0", "1"], "TH3 status changed")
            require(fields[10:18] == ["NA"] * 8, "SIM frame claims physical measurements")
            require(fields[18:]==["SIM","SIM","1","1","0"], "TH3 channel metadata changed")
            require(int(fields[2]) == len(frames) * 1000, "TH3 timestamp gap")
            frames.append(line)
        elif fields[0] == "ALGO":
            vectors.append(fields)
        else:
            raise ValueError(f"Unexpected probe row: {line}")

    require(len(trace) == EXPECTED_STEPS + 1, "TRACE sample count changed")
    require(len(frames) == 121, "TH3 frame count changed")
    for frame, sample in zip(frames, trace[::10]):
        parts = frame.split(",")
        require(abs(float(parts[8]) - sample[1]) <= 0.006, "TH3 temperature differs from trace")
        require(abs(float(parts[9]) - sample[2]) <= 0.006, "TH3 humidity differs from trace")

    max_temp_step = max(abs(b[1] - a[1]) for a, b in zip(trace, trace[1:]))
    max_rh_step = max(abs(b[2] - a[2]) for a, b in zip(trace, trace[1:]))
    # Conservative per-100 ms bounds from the current model: k <= 0.08,
    # disturbance target +/-15 C or 20..90 %RH, plus the small random walk.
    require(max_temp_step <= 2.1, "Temperature jumped beyond model step bound")
    require(max_rh_step <= 5.7, "Humidity jumped beyond model step bound")
    require(all(25 <= t <= 35 and 40 <= rh <= 80 for _, t, rh in trace[:100]),
            "Pre-event summer baseline left its configured range")
    # The fixed seed gives a reproducible disturbance in each minute. These
    # checkpoints guard against accidentally replacing the C model with flat data.
    require(abs(trace[500][1] - 38.448986) < 0.01, "Minute 1 temperature checkpoint changed")
    require(abs(trace[800][1] - 23.242195) < 0.01, "Minute 2 temperature checkpoint changed")
    require(abs(trace[500][1] - trace[400][1]) > 5.0, "Minute 1 disturbance missing")
    require(abs(trace[800][1] - trace[700][1]) > 5.0, "Minute 2 disturbance missing")

    require(len(vectors) == 6, "Algorithm vector count changed")
    for kind, expected, voltage, actual, reason in (row[1:] for row in vectors[:4]):
        value, volts, result = float(expected), float(voltage), float(actual)
        if kind == "NTC":
            kelvin = value + 273.15
            resistance = 5000.0 * math.exp(3950.0 * (1.0 / kelvin - 1.0 / 298.15))
            expected_volts = (3.3 * (1.0 + 10000.0 / 61900.0)) * resistance / (8200.0 + resistance)
        else:
            require(kind == "RH", "Unexpected algorithm vector")
            expected_volts = 2.731 + 0.00538 * value
        require(abs(volts - expected_volts) < 0.0001, f"{kind} forward conversion failed")
        require(abs(result - value) < 0.02 and reason == "ok", f"{kind} inverse conversion failed")
    require(vectors[4] == ["ALGO", "NTC_INVALID", "0.000000", "input_too_low"],
            "NTC invalid-input diagnostic changed")
    require(vectors[5] == ["ALGO", "RH_INVALID", "2.500000", "below_baseline"],
            "RH below-baseline diagnostic changed")

    report = [
        "PASS: production C simulation, 120 s at 100 ms, seed 0x12345678",
        f"TRACE: {len(trace)} samples; max step {max_temp_step:.6f} C / {max_rh_step:.6f} %RH",
        "Disturbance: fixed-seed change detected in both simulated minutes",
        f"TH3: {len(frames)} SIM frames; 23 fields; physical fields NA",
        "ALGO: NTC 25/30 C and RH 50/90 % vectors, plus two invalid-input diagnostics PASS",
        "Scope: host numerical/protocol checks only; NOT TESTED ON HARDWARE; Qt not tested",
    ]
    return trace, frames, report


def write_chart(trace: list[tuple[int, float, float]]) -> None:
    def polyline(field: int, minimum: float, maximum: float) -> str:
        return " ".join(
            f"{50 + row[0] / 120000 * 900:.1f},{300 - (row[field] - minimum) / (maximum - minimum) * 240:.1f}"
            for row in trace
        )

    CHART_SVG.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" width="1000" height="340" viewBox="0 0 1000 340">\n'
        '<rect width="1000" height="340" fill="white"/>\n'
        '<text x="50" y="24" font-family="sans-serif" font-size="18">SIM source: 120 s, 100 ms samples</text>\n'
        '<path d="M50 300H950 M50 60V300" fill="none" stroke="#888"/>\n'
        f'<polyline points="{polyline(1, 10, 50)}" fill="none" stroke="#d44" stroke-width="2"/>\n'
        f'<polyline points="{polyline(2, 0, 100)}" fill="none" stroke="#27c" stroke-width="2"/>\n'
        '<text x="55" y="326" font-family="sans-serif" fill="#d44">temperature C (scale 10..50)</text>\n'
        '<text x="390" y="326" font-family="sans-serif" fill="#27c">humidity %RH (scale 0..100)</text>\n'
        '<text x="920" y="326" font-family="sans-serif">120 s</text>\n'
        '</svg>\n', encoding="utf-8",
    )


def main() -> int:
    try:
        build_probe()
        first_bytes = subprocess.run([str(PROBE)], cwd=ROOT, capture_output=True, check=True).stdout
        second_bytes = subprocess.run([str(PROBE)], cwd=ROOT, capture_output=True, check=True).stdout
        require(first_bytes == second_bytes, "Fixed-seed output is not reproducible")
        # The C serializer supplies CRLF itself; Windows text stdout would add an extra CR.
        require(b"\r\r\n" not in first_bytes, "TH3 stdout contains doubled CR")
        raw_frames = [line for line in first_bytes.split(b"\n") if line.startswith(b"TH3,")]
        require(len(raw_frames) == 121 and all(line.endswith(b"\r") for line in raw_frames),
                "TH3 stdout does not contain 121 CRLF frames")
        trace, frames, report = inspect(first_bytes.decode("ascii"))
        with TRACE_CSV.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(["timestamp_ms", "temperature_c", "humidity_rh", "source_type"])
            writer.writerows((timestamp, temp, rh, "SIM") for timestamp, temp, rh in trace)
        TH3_TEXT.write_bytes(("\r\n".join(frames) + "\r\n").encode("ascii"))
        write_chart(trace)
        report.append("Determinism: second run byte-for-byte identical")
        report.append("TH3 raw stdout: 121 CRLF frames; no doubled CR")
        report.append(f"Files: {TRACE_CSV.relative_to(ROOT)}, {TH3_TEXT.relative_to(ROOT)}, {CHART_SVG.relative_to(ROOT)}")
        SUMMARY.write_text("\n".join(report) + "\n", encoding="utf-8")
        print("\n".join(report))
        return 0
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        if OUTPUT.is_dir():
            SUMMARY.write_text(f"FAIL: {error}\n", encoding="utf-8")
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
