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
             "event_wall_ms", "cpu_errors")


def wait_window(rows, window, zero, exclusions=()):
    previous = {}
    totals = dict.fromkeys(WAIT_KEYS, 0)
    cycles = dict.fromkeys(("poll_cycles", "event_cycles", "thread_cycles", "thread_cpu_ms"), 0)
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
        for key in cycles:
            value=row[key]-before[key] if row.get(key) is not None and before.get(key) is not None else None
            if value is not None and value<0:raise ValueError("SDK cycle/long CPU clock reset")
            cycles[key]=cycles[key]+value if cycles[key] is not None and value is not None else None
    totals.update(cycles)
    totals.update(poll_cpu_ms=None,event_cpu_ms=None) # Legacy short-span thread times are unqualified.
    shares=None
    if len(tids)==1 and cycles['thread_cycles']:
        shares={key.replace('_cycles','_cycle_share'):
                   cycles[key]/cycles['thread_cycles']
                   for key in ('poll_cycles','event_cycles') if cycles[key] is not None}
    # Seconds summed across guest threads is not one wall-clock denominator.
    return {"totals": totals, "thread_observation_seconds": seconds,
            "cpu_cycle_shares": shares,
            "cpu_limits": "Legacy per-call GetThreadTimes sums are discarded. Scheduled cycles are not converted to elapsed time. Long-window thread CPU remains directly measured.",
            "tids": sorted(tids), "single_thread_wall_fraction":
            ((totals["poll_wall_ms"] + totals["event_wall_ms"]) / (seconds * 1000)
             if seconds and len(tids) == 1 else None)}


def renderer_window(rows, window, zero, exclusions=()):
    total = {"flips": 0, "draws": 0, "executor_cpu_ms": 0, "release_ms": 0,
             "release_calls": 0, "release_work_calls": 0, "release_work_ms": 0,
             "segments": 0, "segments_with_releases": 0, "color": 0, "depth": 0,
             "all_sync_ms": 0, "trap_ms": 0, "texture_compare_bytes": 0,
             "snapshot_compare_bytes": 0, "vertex_upload_bytes": 0}
    optional = dict.fromkeys(("color_readback_bytes", "depth_readback_bytes", "release_cycles", "executor_cycles"), 0)
    seconds = 0
    for before, after in zip(rows, rows[1:]):
        start, end = before["qpc_ms"] / 1000 - zero, after["qpc_ms"] / 1000 - zero
        if not full_interval(start, end, window, exclusions):
            continue
        delta = counter_delta(before, after)
        if delta is None:
            continue
        seconds += end - start
        for key in ("flips", "draws", "trap_ms", "segments", "color", "depth",
                    "texture_compare_bytes", "snapshot_compare_bytes", "vertex_upload_bytes"):
            total[key] += delta[key]
        cpu = delta["executor_cpu_ms"]
        total["executor_cpu_ms"] = (total["executor_cpu_ms"] + cpu
                                    if total["executor_cpu_ms"] is not None and cpu is not None else None)
        total["release_ms"] += delta["sync"]["release"]["ms"]
        total["release_calls"] += delta["sync"]["release"]["calls"]
        total["release_work_calls"] += sum(delta["sync"]["release"]["outcomes"][1:])
        work_ms = delta["sync"]["release"].get("work_ms")
        total["release_work_ms"] += work_ms if work_ms is not None else delta["sync"]["release"]["ms"]
        total["segments_with_releases"] += sum(delta["release_histogram"][1:])
        for key in optional:
            value = delta["sync"]["release"].get("cpu_cycles") if key == "release_cycles" else delta.get(key)
            optional[key] = optional[key]+value if optional[key] is not None and value is not None else None
        total["all_sync_ms"] += sum(v["ms"] for v in delta["sync"].values())
    total.update(optional)
    total['release_cpu_ms']=None # Never interpret coarse per-call legacy sums as CPU time.
    calls, work, flips = total["release_calls"], total["release_work_calls"], total["flips"]
    removable = max(0, calls-total["segments_with_releases"])
    return {"seconds": seconds, "totals": total,
            "release_cycle_share": total['release_cycles']/total['executor_cycles']
                if total['release_cycles'] is not None and total['executor_cycles'] else None,
            "release_ms_per_call": total["release_ms"]/calls if calls else None,
            "release_ms_per_work_call": total["release_work_ms"]/work if work else None,
            "optimistic_constant_call_cost_saving_ms_per_flip":
            total["release_ms"]/calls*removable/flips if calls and flips else None,
            "coalescing_model_limits": "Mean-cost model retaining one release call per nonempty segment; forced drains, replacement syncs, different target sizes and no-op distribution can invalidate the estimate. Not a guaranteed saving or strict mathematical bound.",
            "per_flip": {key: value / total["flips"] if value is not None else None for key, value in total.items()
                         if key not in ("flips", "draws")} if total["flips"] else {},
            "fps": total["flips"] / seconds if seconds else None}


DEVICE_RE = re.compile(r"\[XA2 DEVICE\] qpc=(\d+) qpc_hz=(\d+) played=(\d+) queued=(\d+) submitted=(\d+) staged=(\d+) underruns=(\d+)")


def device_rows(text):
    return [dict(zip(("qpc", "hz", "played", "queued", "submitted", "staged", "underruns"),
                     map(int, values))) for values in DEVICE_RE.findall(text)]


def renderer_flip_window(rows, window):
    """Whole counter intervals inside an absolute completed-flip range."""
    selected=[(a,b) for a,b in zip(rows,rows[1:])
              if window[0]<=a["flips"]<b["flips"]<=window[1]
              and counter_delta(a,b) is not None]
    if not selected:
        return {"requested_flips": window,"status": "insufficient_complete_intervals"}
    first,last=selected[0][0],selected[-1][1]
    return {"requested_flips": window,"measured_flips": [first["flips"],last["flips"]],
            **renderer_window(rows,[first["qpc_ms"]/1000,last["qpc_ms"]/1000],0)}


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


def apu_window(rows, window, zero):
    selected = [r for r in rows if window[0] <= r["qpc_ms"]/1000-zero <= window[1]]
    if len(selected)<2:
        return {"status": "insufficient_reports"}
    a,b=selected[0],selected[-1]
    if len({r["tid"] for r in selected})!=1 or any(
            y["submitted"]<x["submitted"] or
            (x["cpu_ms"] is not None and y["cpu_ms"] is not None and y["cpu_ms"]<x["cpu_ms"])
            for x,y in zip(selected,selected[1:])):
        raise ValueError("APU producer identity/counters changed")
    seconds=(b["qpc_ms"]-a["qpc_ms"])/1000
    return {"seconds": seconds, "tid": a["tid"],
            "cpu_ms": b["cpu_ms"]-a["cpu_ms"] if a["cpu_ms"] is not None and b["cpu_ms"] is not None else None,
            "source_frames_per_second": (b["submitted"]-a["submitted"])/seconds}


def readback_accounting_errors(rows):
    errors=[]
    for row in rows:
        if "color_readback_bytes" not in row:
            continue  # Legacy archives did not measure byte volume.
        payload=row["color_readback_bytes"]+row["depth_readback_bytes"]
        attributed=sum(reason["readback_bytes"] for reason in row["sync"].values())
        if payload!=attributed:
            errors.append("readback byte total differs from sync-reason attribution")
            break
    return errors


def analyze(prefix, windows, clock_log=None, flip_windows=()):
    def side(s): return prefix.with_name(prefix.name+s)
    manifest = json.loads(side("-process.json").read_text(encoding="utf-8-sig"))
    zero = manifest["launch_monotonic_seconds"]
    text = side(".stderr.log").read_text(errors="replace")
    waits = records(text, "[SDK WAIT PROFILE] ")
    renderer = records(text, "[HW COUNTERS] ")
    device = device_rows(text)
    apu = records(text, "[APU CPU] ")
    exclusions = [{"start": a, "end": b} for a, b in manifest["capture_windows"]]
    lean = manifest["configuration"].get("RECOMP_COMBAT_PROFILE_LEAN") == "1"
    sample_from=manifest.get("sample_from",540)
    sample_required=sample_from>=0
    if sample_required:
        exclusions.append({"start": sample_from, "end": sample_from+manifest.get("sample_for",20)+5})
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
    errors.extend(readback_accounting_errors(renderer))
    if len(waits)<2 or any(row["cpu_errors"] for row in waits):
        errors.append("SDK wait profiler missing or CPU clock reads failed")
    if len(renderer)<2:
        errors.append("executor counters missing")
    config = manifest["configuration"]
    if config.get("RECOMP_BRUTE_FORCE_WAIT_PROFILE") != "1":
        errors.append("wait profiling was not explicitly enabled")
    sample = json.loads(side("-cpu-samples.json").read_text()) if side("-cpu-samples.json").exists() else None
    if sample_required and (not sample or not sample.get("sample_wall_seconds")):
        errors.append("CPU sample interval missing")
    if lean and (sample or manifest["capture_windows"] or config.get("RECOMP_FB_DUMP")
                 or config.get("RECOMP_AUDIO_WAV")):
        errors.append("lean profile contains sampling, captures or WAV output")
    if lean and any(prefix.parent.glob(prefix.name+'*.bmp')):
        errors.append("lean profile unexpectedly wrote surface images")
    if config.get("RECOMP_AUDIO_DEVICE_TRACE") == "1" and len(device)<2:
        errors.append("requested device timing missing")
    if config.get("RECOMP_AUDIO_CPU_TRACE") == "1" and (len(apu)<2 or any(r["cpu_ms"] is None for r in apu)):
        errors.append("requested APU thread CPU timing missing")
    if config.get("RECOMP_HW_SYNC_CPU") == "1" and any(
            (reason.get("cpu_ms") is None and reason.get("cpu_cycles") is None) or reason.get("cpu_errors")
            for row in renderer for reason in row["sync"].values()):
        errors.append("requested sync CPU timing missing or failed")
    warnings=[]
    if any(r.get('poll_cpu_ms') is not None for r in waits) or any(
            r.get('cpu_ms') is not None for row in renderer for r in row['sync'].values()):
        warnings.append('Discarded legacy short-span GetThreadTimes sums: clock granularity can overcount phase CPU time. Wall/count/byte and long-window thread CPU measurements are retained.')
    window_results=[{"requested": window,
                     "wait": wait_window(waits, window, zero, exclusions),
                     "renderer": renderer_window(renderer, window, zero, exclusions),
                     "apu": apu_window(apu, window, zero),
                     "audio": audio_window(device, window, zero,
                                           clock["estimated_samples_per_second"] or 48000)}
                    for window in windows]
    if not any(w["renderer"]["totals"]["flips"] for w in window_results):
        errors.append("no complete flip intervals in requested measurement windows")
    return {"executable_sha256": manifest["executable_sha256"],
            "profile_observations_valid": not errors, "errors": errors,
            "warnings": warnings,
            "guest_wait_thread_ids": sorted({r["tid"] for r in waits}),
            "cpu_sample": sample,
            "windows": window_results,
            "flip_windows": [renderer_flip_window(renderer,window) for window in flip_windows],
            "device_clock": clock,
            "gameplay_only_clock": gameplay_clock,
            "audio_long_windows": [{"requested": [n, n+60],
                                    "renderer": renderer_window(renderer, [n,n+60], zero, exclusions),
                                    "apu": apu_window(apu, [n,n+60], zero),
                                    **audio_window(device, [n, n+60], zero,
                                                   clock["estimated_samples_per_second"] or 48000)}
                                   for n in range(0, 600, 60)],
            "wait_reports": len(waits),
            "limits": ["Diagnostic build; captures must confirm combat before interpreting its windows.",
                       "Poll and event times are guest-thread stages. Executor sync wall time overlaps them and must not be added to them.",
                       "Short-span CPU attribution uses scheduled cycles only. Long thread CPU uses GetThreadTimes; legacy per-call sums are discarded. Completed waits only; boundary waits can shift attribution.",
                       "GPU timing queries and ordered tracing are off; CPU timing, monitor, WAV and sampling add diagnostic overhead."]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path)
    parser.add_argument("--windows", default="500:530,565:585,600:615,620:635")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--clock-log", type=Path)
    parser.add_argument("--flip-windows", default="")
    args = parser.parse_args()
    result = analyze(args.prefix, ranges(args.windows), args.clock_log, ranges(args.flip_windows))
    args.output.write_text(json.dumps(result, indent=2)+"\n")
    raise SystemExit(0 if result["profile_observations_valid"] else 1)
