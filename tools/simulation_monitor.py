"""实时消费 TH3；默认启动现有 C simulation stream，也可从 stdin 接收 TH3。"""

from __future__ import annotations

import argparse
import csv
import math
import queue
import shutil
import signal
import subprocess
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass, replace
from datetime import datetime
from pathlib import Path
from typing import TextIO

try:
    import matplotlib
    import matplotlib.pyplot as plt
    import numpy as np
    from matplotlib.animation import FuncAnimation
except ImportError as error:
    print(f"缺少绘图依赖：{error.name}。请运行：python -m pip install matplotlib numpy", file=sys.stderr)
    raise SystemExit(2) from error


ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build_verify" / "simulation_monitor"
STREAM_EXE = BUILD_DIR / ("simulation_stream.exe" if sys.platform == "win32" else "simulation_stream")
LOG_DIR = ROOT / "logs"
MAX_HISTORY_SECONDS = 480
SAMPLE_PERIOD_SECONDS = 0.1
MAX_SAMPLES = int(MAX_HISTORY_SECONDS / SAMPLE_PERIOD_SECONDS)
REFRESH_INTERVAL = 0.2
PAUSE_TIMEOUT_SECONDS = 2.0
RIGHT_PADDING_RATIO = 0.25
MAX_PENDING_LINES = 128


@dataclass(frozen=True)
class Measurement:
    source: str
    timestamp_ms: int
    valid: bool
    error_flags: int
    temperature_c: float | None
    humidity_rh: float | None
    plot_time_s: float = 0.0
    temperature_source: str = "UNKNOWN"
    humidity_source: str = "UNKNOWN"
    generation: int = 0


def parse_th3_line(line: str) -> Measurement:
    """Strict TH3-only consumer; no conversion, calibration or fallback."""
    fields = line.rstrip("\r\n").split(",")
    if fields[0] in ("TH2", "TH"):
        raise ValueError("UNSUPPORTED_LEGACY_PROTOCOL")
    if len(fields) != 23 or fields[0] != "TH3":
        raise ValueError("TH3 field count or prefix")
    import re
    def uint(text, maximum):
        if not re.fullmatch(r"[0-9]+", text) or int(text) > maximum:
            raise ValueError("Unsigned field out of range")
        return int(text)
    timestamp = uint(fields[2], 0xFFFFFFFF)
    valid = uint(fields[3], 1)
    flags, error, _calibration, _samples = (uint(x, 65535) for x in fields[4:8])
    engineering = []
    for text in fields[8:14]:
        if text == "NA": engineering.append(None); continue
        if not re.fullmatch(r"[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?", text):
            raise ValueError("Invalid engineering field")
        value = float(text)
        if not math.isfinite(value) or abs(value) > 3.402823466e38:
            raise ValueError("Nonfinite engineering field")
        engineering.append(value)
    for text in fields[14:16]:
        if text != "NA" and (not re.fullmatch(r"-?[0-9]+", text) or not -32768 <= int(text) <= 32767):
            raise ValueError("Raw code out of range")
    for text in fields[16:18]:
        if text != "NA": uint(text, 0xFFFFFFFF)
    t_source, h_source = fields[18:20]
    if t_source not in ("REAL", "SIM") or h_source not in ("REAL", "SIM"):
        raise ValueError("Invalid channel source")
    if fields[1] != (t_source if t_source == h_source else "MIXED"):
        raise ValueError("Inconsistent channel sources")
    tv, hv = (uint(x, 1) for x in fields[20:22])
    generation = uint(fields[22], 0xFFFFFFFF)
    if tv != bool(flags & 1 and engineering[0] is not None) or hv != bool(flags & 2 and engineering[1] is not None) or valid != (tv & hv):
        raise ValueError("Inconsistent channel validity")
    if t_source == "SIM" and (fields[10] != "NA" or fields[14] != "NA" or flags & (4 | 64)):
        raise ValueError("SIM temperature physical data")
    if h_source == "SIM" and (any(fields[i] != "NA" for i in (11,12,13,15,16,17)) or flags & (8|16|32|128|256)):
        raise ValueError("SIM humidity physical data")
    return Measurement(fields[1], timestamp, bool(valid), error,
                       engineering[0] if tv else None, engineering[1] if hv else None,
                       temperature_source=t_source, humidity_source=h_source, generation=generation)


def update_buffer(history: deque[Measurement], reading: Measurement,
                  runtime_s: float) -> bool:
    """滑动窗口只保留近期数据，长时间运行也不让内存或退出 CSV 增长。"""
    if reading.temperature_c is None and reading.humidity_rh is None:
        return False
    history.append(replace(reading, plot_time_s=runtime_s))
    prune_buffer(history, runtime_s)
    return True


def prune_buffer(history: deque[Measurement], runtime_s: float) -> None:
    """暂停或进程退出后也淘汰过期样本，避免把旧值称为最近数据。"""
    while history and runtime_s - history[0].plot_time_s > MAX_HISTORY_SECONDS:
        history.popleft()


def display_window(runtime_s: float) -> tuple[float, float]:
    """早期按 10/20/60 s 展开；随后随数据增长到 5/8 min。"""
    if runtime_s <= 10:
        visible_s = 10.0
    elif runtime_s <= 20:
        visible_s = 20.0
    elif runtime_s <= 60:
        visible_s = 60.0
    elif runtime_s <= 300:
        visible_s = runtime_s  # 100 s -> 0..125 s，满足右侧留白示例。
    else:
        visible_s = min(runtime_s, MAX_HISTORY_SECONDS)
    left_s = max(0.0, runtime_s - visible_s)
    return left_s, left_s + visible_s * (1.0 + RIGHT_PADDING_RATIO)


def save_csv(history: deque[Measurement], source: str) -> Path:
    """只导出最近八分钟；运行日志与工程阶段日志分开。"""
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    prefix = "real_monitor" if source == "REAL" else "simulation_monitor"
    filename = f"{prefix}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    destination = LOG_DIR / filename
    with destination.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(("timestamp_ms", "temperature_c", "humidity_rh"))
        writer.writerows((item.timestamp_ms, (f"{item.temperature_c:.2f}" if item.temperature_c is not None else "NA"),
                          (f"{item.humidity_rh:.2f}" if item.humidity_rh is not None else "NA")) for item in history)
    return destination


def build_simulation_stream() -> None:
    """Python 只构建 C 数据源，不实现随机模型或改写 TH3。"""
    from run_software_demo import environment
    try:
        _qt, gcc, _env = environment()
        compiler = str(gcc / "gcc.exe")
    except RuntimeError:
        compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise RuntimeError("未找到 GCC/Clang；请安装 C 编译器并加入 PATH")
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    command = [
        compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-include", "stddef.h", "-I", str(ROOT / "Core" / "Inc"),
        str(ROOT / "tools" / "host_src" / "simulation_stream.c"),
        *[str(ROOT / "Core" / "Src" / name) for name in (
            "app_measurement_source.c", "app_measurement_source_sim.c",
            "app_protocol.c", "algo_sensor_convert.c",
        )],
        "-lm", "-o", str(STREAM_EXE),
    ]
    subprocess.run(command, cwd=ROOT, check=True)


def read_lines(stream: TextIO, pending: queue.Queue[tuple[str, str]],
               process: subprocess.Popen[str] | None) -> None:
    """后台读管道，有限队列防止窗口暂停时积压无限数据。"""
    def offer(kind: str, content: str) -> None:
        try:
            pending.put_nowait((kind, content))
        except queue.Full:
            try:
                pending.get_nowait()
            except queue.Empty:
                pass
            try:
                pending.put_nowait((kind, content))
            except queue.Full:
                pass

    try:
        for line in stream:
            offer("line", line)
    except OSError as error:
        offer("error", str(error))
    finally:
        detail = process.stderr.read().strip() if process and process.stderr else ""
        offer("end", detail)


def update_plot(runtime_s: float, history: deque[Measurement], temp_line,
                rh_line, temp_axis, rh_axis, info_text, status_text,
                source: str, sample_count: int, status: str) -> None:
    """两条曲线只消费同一个已解析的 TH3 数据缓冲。"""
    left_s, right_s = display_window(runtime_s)
    temp_axis.set_xlim(left_s, right_s)
    rh_axis.set_xlim(left_s, right_s)
    if history:
        times = np.fromiter((item.plot_time_s for item in history), dtype=float)
        temperatures = np.fromiter((item.temperature_c for item in history), dtype=float)
        humidities = np.fromiter((item.humidity_rh for item in history), dtype=float)
        temp_line.set_data(times, temperatures)
        rh_line.set_data(times, humidities)
        finite_t=temperatures[np.isfinite(temperatures)];finite_h=humidities[np.isfinite(humidities)]
        if finite_t.size:
            low_c, high_c = float(np.min(finite_t)), float(np.max(finite_t))
            margin_c = max(1.0, (high_c - low_c) * 0.15)
            temp_axis.set_ylim(low_c - margin_c, high_c + margin_c)
        if not finite_h.size or (float(np.min(finite_h)) >= 0 and float(np.max(finite_h)) <= 100):
            rh_axis.set_ylim(0, 100)
        else:
            rh_axis.set_ylim(min(0, float(np.min(finite_h)) - 5),
                             max(100, float(np.max(finite_h)) + 5))
        current = history[-1]
        if status == "RUNNING":
            temp=f"{current.temperature_c:.2f}" if current.temperature_c is not None else "NA"
            hum=f"{current.humidity_rh:.2f}" if current.humidity_rh is not None else "NA"
            info_text.set_text(f"Temperature: {temp} °C ({current.temperature_source})     "
                               f"Humidity: {hum} %RH ({current.humidity_source})")
        else:
            info_text.set_text("Temperature: --     Humidity: --")
    else:
        info_text.set_text("Temperature: --     Humidity: --")
    status_text.set_text(f"SOURCE: {source}     Runtime: {runtime_s:.0f} s     "
                         f"Samples: {sample_count}     {status}")


def main() -> int:
    parser = argparse.ArgumentParser(description="实时查看现有 TH3 温湿度输出")
    parser.add_argument("--input", choices=("sim", "stdin"), default="sim",
                        help="sim 启动 C 主机流；stdin 消费外部 TH3（可用于未来 REAL UART 转发）")
    args = parser.parse_args()
    if matplotlib.get_backend().lower() in {"agg", "pdf", "svg", "ps", "cairo", "template"}:
        print("当前 matplotlib 后端没有交互窗口；请配置 QtAgg 或 TkAgg 后重试", file=sys.stderr)
        return 2

    process: subprocess.Popen[str] | None = None
    if args.input == "sim":
        try:
            build_simulation_stream()
            process = subprocess.Popen([str(STREAM_EXE)], cwd=ROOT, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True, bufsize=1)
        except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
            print(f"无法启动 C 模拟流：{error}", file=sys.stderr)
            return 2
        assert process.stdout is not None
        stream = process.stdout
    else:
        stream = sys.stdin

    pending: queue.Queue[tuple[str, str]] = queue.Queue(maxsize=MAX_PENDING_LINES)
    reader = threading.Thread(target=read_lines, args=(stream, pending, process), daemon=True)
    reader.start()
    history: deque[Measurement] = deque(maxlen=MAX_SAMPLES)
    fig, (temp_axis, rh_axis) = plt.subplots(2, 1, figsize=(11, 7), sharex=True)
    fig.canvas.manager.set_window_title("TH3 Temperature / Humidity Monitor")
    temp_line, = temp_axis.plot([], [], color="tab:red", linewidth=1.5)
    rh_line, = rh_axis.plot([], [], color="tab:blue", linewidth=1.5)
    temp_axis.set_ylabel("Temperature (°C)")
    rh_axis.set_ylabel("Humidity (%RH)")
    rh_axis.set_xlabel("Runtime (s)")
    temp_axis.grid(True, alpha=0.25)
    rh_axis.grid(True, alpha=0.25)
    temp_axis.set_ylim(20, 40)
    rh_axis.set_ylim(0, 100)
    fig.subplots_adjust(top=0.85, hspace=0.12)
    info_text = fig.text(0.98, 0.98, "Temperature: --     Humidity: --",
                         ha="right", va="top", fontsize=11)
    status_text = fig.text(0.98, 0.935, "SOURCE: WAITING     Runtime: 0 s     Samples: 0",
                           ha="right", va="top", fontsize=10)

    started_at = time.monotonic()
    latest_at = started_at
    source = "WAITING"
    sample_count = 0
    status = "WAITING"
    ended = False
    malformed_count = 0
    segment_key = None

    def refresh(_frame: int) -> None:
        nonlocal latest_at, source, sample_count, status, ended, malformed_count, segment_key
        now = time.monotonic()
        for _ in range(MAX_PENDING_LINES):
            try:
                kind, content = pending.get_nowait()
            except queue.Empty:
                break
            if kind == "end":
                ended = True
                status = f"SOURCE STOPPED {content}".strip()
            elif kind == "error":
                status = f"READ ERROR: {content}"
            else:
                try:
                    reading = parse_th3_line(content)
                except (ValueError, StopIteration):
                    malformed_count += 1
                    status = f"BAD TH3: {malformed_count}"
                    continue
                key=(reading.temperature_source, reading.humidity_source, reading.generation)
                if segment_key is not None and key != segment_key:
                    history.clear()  # REAL/SIM 不混画，也不混存退出 CSV。
                    sample_count = 0
                segment_key = key
                source = reading.source
                latest_at = now
                if update_buffer(history, reading, now - started_at):
                    sample_count += 1
                    status = "RUNNING"
                else:
                    status = f"INVALID (error_flags={reading.error_flags})"
        if ended:
            status = "SOURCE STOPPED"
        elif now - latest_at >= PAUSE_TIMEOUT_SECONDS:
            status = "PAUSED: no TH3 for 2 s"
        prune_buffer(history, now - started_at)
        update_plot(now - started_at, history, temp_line, rh_line, temp_axis, rh_axis,
                    info_text, status_text, source, sample_count, status)

    animation = FuncAnimation(fig, refresh, interval=int(REFRESH_INTERVAL * 1000),
                              cache_frame_data=False)
    previous_sigint = signal.getsignal(signal.SIGINT)
    signal.signal(signal.SIGINT, lambda _signum, _frame: plt.close(fig))
    print("监视窗口已启动；关闭窗口或按 Ctrl+C 保存最近 8 分钟 CSV。")
    try:
        plt.show()
    except KeyboardInterrupt:
        pass
    finally:
        signal.signal(signal.SIGINT, previous_sigint)
        if process is not None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        prune_buffer(history, time.monotonic() - started_at)
        destination = save_csv(history, source)
        print(f"已保存 {len(history)} 个最近采样：{destination}")
        del animation  # 保持动画存活至窗口退出。
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
