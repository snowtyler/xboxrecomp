"""Separate SDK wait wall/CPU time, executor synchronization and device pacing.

Uses cumulative native QPC counters; never adds overlapping guest/executor wall
times. Device rate is inferred only from long, apparently continuously fed
intervals. A dry-queue count alone cannot measure playback gaps.
"""
import argparse
import json
from pathlib import Path
import re
import statistics

from performance import counter_delta, full_interval, ranges


def records(text, prefix):
    rows = []
    for line in text.splitlines():
        if line.startswith(prefix):
            try:
                rows.append(json.loads(line[len(prefix):]))
            except ValueError:
                pass
    return rows


WAIT_KEYS = ("completed", "poll_count", "event_count", "poll_wall_ms",
             "event_wall_ms", "poll_cpu_ms", "event_cpu_ms", "cpu_errors")


def wait_window(rows, window, zero, exclusions=()):
    previous = {}
    totals = dict.fromkeys(WAIT_KEYS, 0)
    seconds = 0
    tids = set()
    for row in rows:
        before = previous.get(row["tid"])
        previous[row["tid"]] = row
        if before is None:
            continue
        start, end = (before["qpc_ms"] / 1000 - zero, row["qpc_ms"] / 1000 - zero)
        if not full_interval(start, end, window, exclusions):
            continue
        delta = {key: row[key] - before[key] for key in WAIT_KEYS}
        if any(value < 0 for value in delta.values()):
            raise ValueError("SDK wait counters reset within a measurement window")
        tids.add(row["tid"])
        seconds += end - start
        for key, value in delta.items():
            totals[key] += value
    # Seconds summed across guest threads is not one wall-clock denominator.
    return {"totals": totals, "thread_observation_seconds": seconds,
            "tids": sorted(tids), "single_thread_wall_fraction":
            ((totals["poll_wall_ms"] + totals["event_wall_ms"]) / (seconds * 1000)
             if seconds and len(tids) == 1 else None)}


def renderer_window(rows, window, zero, exclusions=()):
    total = {"flips": 0, "draws": 0, "executor_cpu_ms": 0, "release_ms": 0,
             "release_calls": 0, "all_sync_ms": 0, "trap_ms": 0}
    seconds = 0
    for before, after in zip(rows, rows[1:]):
        start, end = before["qpc_ms"] / 1000 - zero, after["qpc_ms"] / 1000 - zero
        if not full_interval(start, end, window, exclusions):
            continue
        delta = counter_delta(before, after)
        if delta is None:
            continue
        seconds += end - start
        for key in ("flips", "draws", "trap_ms"):
            total[key] += delta[key]
        if delta["executor_cpu_ms"] is not None:
            total["executor_cpu_ms"] += delta["executor_cpu_ms"]
        total["release_ms"] += delta["sync"]["release"]["ms"]
        total["release_calls"] += delta["sync"]["release"]["calls"]
        total["all_sync_ms"] += sum(v["ms"] for v in delta["sync"].values())
    return {"seconds": seconds, "totals": total,
            "per_flip": {key: value / total["flips"] for key, value in total.items()
                         if key not in ("flips", "draws")} if total["flips"] else {},
            "fps": total["flips"] / seconds if seconds else None}


DEVICE_RE = re.compile(r"\[XA2 DEVICE\] qpc=(\d+) qpc_hz=(\d+) played=(\d+) queued=(\d+) submitted=(\d+) staged=(\d+) underruns=(\d+)")


def device_rows(text):
    return [dict(zip(("qpc", "hz", "played", "queued", "submitted", "staged", "underruns"),
                     map(int, values))) for values in DEVICE_RE.findall(text)]


def clock_estimate(rows, minimum_seconds=10):
    spans, start = [], None
    for before, after in zip(rows, rows[1:]):
        valid = (before["hz"] == after["hz"] and after["qpc"] > before["qpc"]
                 and after["played"] >= before["played"]
                 and after["underruns"] == before["underruns"]
                 and before["queued"] > 0 and after["queued"] > 0)
        if valid:
            start = before if start is None else start
        if start is not None and (not valid or after is rows[-1]):
            end = before if not valid else after
            seconds = (end["qpc"] - start["qpc"]) / start["hz"]
            if seconds >= minimum_seconds:
                spans.append({"seconds": seconds, "samples_per_second":
                              (end["played"] - start["played"]) / seconds})
            start = None
    rate = statistics.median(v["samples_per_second"] for v in spans) if spans else None
    return {"fed_spans": spans, "estimated_samples_per_second": rate,
            "estimated_drift_ppm": (rate / 48000 - 1) * 1e6 if rate else None,
            "status": "fed_span_estimate" if rate else "unidentifiable_no_long_fed_span",
            "limits": "Endpoint queue checks plus unchanged dry-spell count are evidence of feeding, not a hardware clock calibration. Starved effective playback rate is not clock drift."}


def audio_window(rows, window, zero, rate=48000):
    selected = [r for r in rows if window[0] <= r["qpc"] / r["hz"] - zero <= window[1]]
    if len(selected) < 2:
        return {"status": "insufficient_reports"}
    a, b = selected[0], selected[-1]
    if a["hz"] != b["hz"] or b["played"] < a["played"]:
        raise ValueError("Audio clock/counters reset in a measurement window")
    seconds = (b["qpc"] - a["qpc"]) / a["hz"]
    played, submitted = b["played"] - a["played"], b["submitted"] - a["submitted"]
    return {"seconds": seconds, "played_frames": played, "submitted_frames": submitted,
            "played_rate": played / seconds, "submitted_rate": submitted / seconds,
            "underrun_count_delta": b["underruns"] - a["underruns"],
            "device_unplayed_wall_ms": (seconds - played / rate) * 1000,
            "drift_sensitivity_1000ppm_ms": seconds,  # 0.1% of this wall interval
            "limits": "Device deficit includes dry time and rate uncertainty; the submission WAV omits device-inserted silence."}


def analyze(prefix, windows, clock_log=None):
    def side(s): return prefix.with_name(prefix.name+s)
    manifest = json.loads(side("-process.json").read_text(encoding="utf-8-sig"))
    zero = manifest["launch_monotonic_seconds"]
    text = side(".stderr.log").read_text(errors="replace")
    waits = records(text, "[SDK WAIT PROFILE] ")
    renderer = records(text, "[HW COUNTERS] ")
    device = device_rows(text)
    exclusions = [{"start": a, "end": b} for a, b in manifest["capture_windows"]]
    exclusions.append({"start": 540, "end": 565})
    gameplay_clock = clock_estimate(device)
    clock = gameplay_clock
    if clock_log is not None:
        calibration_text = clock_log.read_text(errors="replace")
        if "[CLOCK PROBE]" not in calibration_text:
            raise ValueError("Separate clock probe did not finish")
        clock = clock_estimate(device_rows(calibration_text))
        clock["source"] = str(clock_log)
        clock["limits"] += " Separate silent default-device run after gameplay; endpoint processing quantization remains."
    errors = []
    if len(waits)<2 or any(row["cpu_errors"] for row in waits):
        errors.append("SDK wait profiler missing or CPU clock reads failed")
    if len(renderer)<2:
        errors.append("executor counters missing")
    config = manifest["configuration"]
    if config.get("RECOMP_BRUTE_FORCE_WAIT_PROFILE") != "1":
        errors.append("wait profiling was not explicitly enabled")
    sample = json.loads(side("-cpu-samples.json").read_text()) if side("-cpu-samples.json").exists() else None
    if not sample or not sample.get("sample_wall_seconds"):
        errors.append("CPU sample interval missing")
    if config.get("RECOMP_AUDIO_DEVICE_TRACE") == "1" and len(device)<2:
        errors.append("requested device timing missing")
    return {"executable_sha256": manifest["executable_sha256"],
            "profile_observations_valid": not errors, "errors": errors,
            "guest_wait_thread_ids": sorted({r["tid"] for r in waits}),
            "cpu_sample": sample,
            "windows": [{"requested": window,
                         "wait": wait_window(waits, window, zero, exclusions),
                         "renderer": renderer_window(renderer, window, zero, exclusions),
                         "audio": audio_window(device, window, zero,
                                               clock["estimated_samples_per_second"] or 48000)}
                        for window in windows],
            "device_clock": clock,
            "gameplay_only_clock": gameplay_clock,
            "audio_long_windows": [{"requested": [n, n+60],
                                    **audio_window(device, [n, n+60], zero,
                                                   clock["estimated_samples_per_second"] or 48000)}
                                   for n in range(0, 600, 60)],
            "wait_reports": len(waits),
            "limits": ["Diagnostic build; captures must confirm combat before interpreting its windows.",
                       "Poll and event times are guest-thread stages. Executor sync wall time overlaps them and must not be added to them.",
                       "CPU times use GetThreadTimes resolution. Completed waits only; an active wait at a window boundary can shift attribution.",
                       "GPU timing queries and ordered tracing are off; CPU timing, monitor, WAV and sampling add diagnostic overhead."]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path)
    parser.add_argument("--windows", default="500:530,565:585,600:615,620:635")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--clock-log", type=Path)
    args = parser.parse_args()
    result = analyze(args.prefix, ranges(args.windows), args.clock_log)
    args.output.write_text(json.dumps(result, indent=2)+"\n")
    raise SystemExit(0 if result["profile_observations_valid"] else 1)
