"""Observe a running gameplay probe without controlling its process.

Example (run before the first measurement window):
  python games/brute_force_e3/performance.py monitor --name perf_baseline123 \
    --windows 500:530,540:555,600:615,640:660,710:730,750:770,800:830 \
    --sample 540:555

The monitor timestamps newly appended stderr reports, using process.json file
creation as elapsed zero. Reports already present at attachment are skipped:
their log does not contain host timestamps. GPU reporting intervals must fit
entirely inside a measurement window and must not overlap sample/capture
exclusions. The summaries therefore measure complete reporting intervals, not
individual frames or percentiles. No game is started, suspended, or stopped.

Rebuild summaries from the saved event stream:
  python games/brute_force_e3/performance.py summarize --name perf_baseline123
"""

import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import time


DEFAULT_WINDOWS = "500:530,540:555,600:615,640:660,710:730,750:770,800:830"
GPU_RE = re.compile(r"^\[GPU\] ([\d.]+) fps \((\d+) flips\)")
WORK_RE = re.compile(r"^\[GPU\] vertex programs: (\d+) batches, (\d+) vertices;")
INPUT_RE = re.compile(
    r"^\[HW INPUT\] packing ([\d.]+) ms/frame submit ([\d.]+) ms/frame rows (\d+)/frame"
)
PHASE_RE = re.compile(r"^\[HW PHASE\] us/draw (.*)")
SYNC_RE = re.compile(r"^\[HW SYNC\] (\d+) syncs, ([\d.]+) ms/sync, draws=(\d+)")
SYNC_INTERVAL_RE = re.compile(
    r"^\[HW SYNC\] syncs=(\d+) interval=(\d+) (?:interval_ms=([\d.]+) )?"
    r"ms/sync=([\d.]+) draws=(\d+) "
    r"color=(\d+) wait_ms=([\d.]+) copy_ms=([\d.]+) "
    r"depth=(\d+) wait_ms=([\d.]+) copy_ms=([\d.]+) "
    r"fence=(\d+) wait_ms=([\d.]+) color_alloc=(\d+) color_reuse=(\d+) "
    r"depth_alloc=(\d+) depth_reuse=(\d+)"
)
INPUT_ERROR_RE = re.compile(r'PAD: unknown button "([^"]+)"')
PHASE_NAMES = ("shader", "target", "texture", "stream", "uniforms", "bind/draw")
COUNTER_KEYS = ("draws", "segments", "color", "depth", "fences", "trap_ms",
                "texture_compare_bytes", "snapshot_compare_bytes", "vertex_upload_bytes",
                "releases", "traps", "wait_for_idle", "flips", "clears", "ack_idle")
OPTIONAL_COUNTER_KEYS = ("color_readback_bytes", "depth_readback_bytes", "executor_cycles")
OPTIONAL_SYNC_KEYS = ("work_ms", "color_readbacks", "depth_readbacks", "readback_bytes", "cpu_ms", "cpu_cycles", "cpu_errors")


def utc_text(epoch):
    return dt.datetime.fromtimestamp(epoch, dt.timezone.utc).isoformat()


def ranges(text):
    result = []
    for item in text.split(","):
        if not item:
            continue
        start, end = map(float, item.split(":"))
        if start < 0 or end <= start:
            raise ValueError("Ranges must be nonnegative START:END with END > START")
        result.append([start, end])
    return result


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_script(log_path, note=None):
    errors = []
    with log_path.open(encoding="utf-8", errors="replace") as log:
        for line_number, line in enumerate(log, 1):
            match = INPUT_ERROR_RE.search(line)
            if match:
                errors.append({"line": line_number, "button": match[1]})
    return {"status": "unsupported_controls_logged" if errors else "no_unknown_buttons_logged",
            "log_path": str(log_path), "unknown_buttons": errors,
            "note": note or ("Script rejected controls; qualify the gameplay route from completed captures."
                             if errors else "Confirm the scripted route against completed gameplay captures.")}


def parse_line(line):
    if line.startswith("[HW COUNTERS] "):
        try:
            data = json.loads(line[len("[HW COUNTERS] "):])
            if data.get("version") == 1:
                return {"type": "hw_counters", "counters": data}
        except (ValueError, AttributeError):
            pass  # Truncated or future diagnostic reports are not measurements.
        return None
    match = GPU_RE.match(line)
    if match:
        return {"type": "gpu", "reported_fps": float(match[1]), "flips": int(match[2])}
    match = WORK_RE.match(line)
    if match:
        return {"type": "work", "program_batches": int(match[1]), "program_vertices": int(match[2])}
    match = INPUT_RE.match(line)
    if match:
        return {"type": "input", "packing_ms_per_frame": float(match[1]),
                "submit_ms_per_frame": float(match[2]), "rows_per_frame": int(match[3])}
    match = PHASE_RE.match(line)
    if match:
        values = dict(re.findall(r"([a-z/]+)=([\d.]+)", match[1]))
        if all(key in values for key in PHASE_NAMES):
            return {"type": "phase", "batch_draws": 5000,
                    "us_per_draw": {key: float(values[key]) for key in PHASE_NAMES}}
    match = SYNC_INTERVAL_RE.match(line)
    if match:
        return {"type": "sync_interval", "cumulative_syncs": int(match[1]),
                "interval_syncs": int(match[2]),
                "interval_sync_ms": float(match[3]) if match[3] else int(match[2]) * float(match[4]),
                "sync_time_source": "reported_interval_ms" if match[3] else "rounded_mean_times_count",
                "reported_mean_ms_per_sync": float(match[4]), "cumulative_draws": int(match[5]),
                "color_readbacks": int(match[6]), "color_map_ms": float(match[7]),
                "color_copy_ms": float(match[8]), "depth_readbacks": int(match[9]),
                "depth_map_ms": float(match[10]), "depth_copy_ms": float(match[11]),
                "event_fences": int(match[12]), "event_fence_wait_ms": float(match[13]),
                "color_allocations": int(match[14]), "color_reuses": int(match[15]),
                "depth_allocations": int(match[16]), "depth_reuses": int(match[17])}
    match = SYNC_RE.match(line)
    if match:
        return {"type": "sync", "syncs": int(match[1]),
                "cumulative_mean_ms_per_sync": float(match[2]), "draws": int(match[3])}
    match = INPUT_ERROR_RE.search(line)
    if match:
        return {"type": "input_error", "button": match[1]}
    return None


def overlap(start, end, exclusion):
    return start < exclusion["end"] and end > exclusion["start"]


def full_interval(start, end, window, exclusions):
    return (end > start and start >= window[0] and end <= window[1]
            and not any(overlap(start, end, exclusion) for exclusion in exclusions))


def counter_delta(before, after):
    """Subtract cumulative counters only within one uninterrupted executor run."""
    if (before["thread_id"] != after["thread_id"] or after["report"] != before["report"] + 1
            or after["qpc_ms"] <= before["qpc_ms"]):
        return None
    delta = {key: after[key] - before[key] for key in COUNTER_KEYS}
    delta.update({key: after[key]-before[key] if after.get(key) is not None and before.get(key) is not None else None
                  for key in OPTIONAL_COUNTER_KEYS})
    delta["qpc_ms"] = after["qpc_ms"] - before["qpc_ms"]
    delta["executor_cpu_ms"] = (after["executor_cpu_ms"] - before["executor_cpu_ms"]
                                if after["executor_cpu_ms"] is not None
                                and before["executor_cpu_ms"] is not None else None)
    delta["release_histogram"] = [b-a for a, b in zip(before["release_histogram"], after["release_histogram"])]
    delta["sync"] = {}
    for cause, current in after["sync"].items():
        previous = before["sync"][cause]
        delta["sync"][cause] = {key: current[key] - previous[key] for key in ("calls", "failed", "ms")}
        delta["sync"][cause].update({key: current[key]-previous[key]
            if current.get(key) is not None and previous.get(key) is not None else None
            for key in OPTIONAL_SYNC_KEYS})
        # Legacy per-call GetThreadTimes sums can exceed wall time. Preserve
        # original reports, but never aggregate them as qualified CPU time.
        delta["sync"][cause]["cpu_ms"] = None
        delta["sync"][cause]["outcomes"] = [b-a for a, b in zip(previous["outcomes"], current["outcomes"])]
    old_targets = {(item["width"], item["height"], item["bpp"]): item["count"] for item in before["targets"]}
    delta["targets"] = [{**item, "count": item["count"] - old_targets.get((item["width"], item["height"], item["bpp"]), 0)}
                        for item in after["targets"]]
    delta["target_overflow"] = after["target_overflow"] - before["target_overflow"]
    def nonnegative(value):
        if isinstance(value, dict):
            return all(nonnegative(item) for item in value.values())
        if isinstance(value, list):
            return all(nonnegative(item) for item in value)
        return value is None or value >= 0
    return delta if nonnegative(delta) else None


def summarize_counters(intervals):
    if not intervals:
        return None
    values = [item["delta"] for item in intervals]
    optional_totals = {key: sum(item[key] for item in values) if all(item.get(key) is not None for item in values) else None
                       for key in OPTIONAL_COUNTER_KEYS}
    totals = {key: sum(item[key] for item in values) for key in COUNTER_KEYS}
    totals["qpc_ms"] = sum(item["qpc_ms"] for item in values)
    totals["executor_cpu_ms"] = (sum(item["executor_cpu_ms"] for item in values)
                                 if all(item["executor_cpu_ms"] is not None for item in values) else None)
    totals["release_histogram"] = [sum(item["release_histogram"][i] for item in values) for i in range(9)]
    totals.update(optional_totals)
    totals["sync"] = {}
    for cause in values[0]["sync"]:
        totals["sync"][cause] = {key: sum(item["sync"][cause][key] for item in values)
                                for key in ("calls", "failed", "ms")}
        totals["sync"][cause]["outcomes"] = [sum(item["sync"][cause]["outcomes"][i] for item in values) for i in range(8)]
        totals["sync"][cause].update({key: sum(item["sync"][cause][key] for item in values)
            if all(item["sync"][cause].get(key) is not None for item in values) else None
            for key in OPTIONAL_SYNC_KEYS})
        totals["sync"][cause]["cpu_ms"] = None
    targets = {}
    for item in values:
        for target in item["targets"]:
            key = (target["width"], target["height"], target["bpp"])
            targets[key] = targets.get(key, 0) + target["count"]
    totals["targets"] = [{"width": key[0], "height": key[1], "bpp": key[2], "count": count}
                         for key, count in sorted(targets.items()) if count]
    totals["target_overflow"] = sum(item["target_overflow"] for item in values)
    flips = totals["flips"]
    return {"complete_report_intervals": len(intervals), "totals": totals,
            "per_frame": {key: value / flips if flips and value is not None else None
                          for key, value in totals.items() if key in COUNTER_KEYS + OPTIONAL_COUNTER_KEYS + ("executor_cpu_ms", "qpc_ms")},
            "sync_per_frame": {cause: {"calls": item["calls"] / flips if flips else None,
                                       "color_readbacks": sum(count for mask, count in enumerate(item["outcomes"]) if mask & 1) / flips if flips else None,
                                       "ms": item["ms"] / flips if flips else None}
                               for cause, item in totals["sync"].items()}}


def summarize_events(metadata, events):
    """Use exact flip deltas for complete observed reporting intervals."""
    exclusions = metadata["exclusions"]
    gpu_intervals = []
    prior_gpu = None
    prior_work = None
    current_interval = None
    phase_intervals = []
    prior_phase = None
    syncs = []
    sync_intervals = []
    prior_sync_interval = None
    counter_intervals = []
    prior_counter = None
    for event in events:
        kind = event["type"]
        if kind == "hw_counters":
            if prior_counter:
                delta = counter_delta(prior_counter["counters"], event["counters"])
                if delta is not None:
                    counter_intervals.append({"start": prior_counter["elapsed_seconds"],
                                              "end": event["elapsed_seconds"], "delta": delta})
            prior_counter = event
        if kind == "gpu":
            current_interval = None
            if prior_gpu and event["flips"] >= prior_gpu["flips"]:
                current_interval = {"start": prior_gpu["elapsed_seconds"],
                                    "end": event["elapsed_seconds"],
                                    "flips": event["flips"] - prior_gpu["flips"],
                                    "reported_fps": event["reported_fps"]}
                gpu_intervals.append(current_interval)
            prior_gpu = event
        elif kind == "input" and current_interval is not None:
            current_interval["input"] = event
        elif kind == "work":
            # Associate only adjacent report packets. Work counters are always
            # printed, so normal-graphics runs need no GPU profiling queries.
            if (current_interval is not None and prior_work is not None
                    and 0 <= event["elapsed_seconds"] - current_interval["end"] <= 0.5
                    and 0 <= prior_work["elapsed_seconds"] - current_interval["start"] <= 0.5
                    and all(event[key] >= prior_work[key] for key in ("program_batches", "program_vertices"))):
                current_interval["work"] = {key: event[key] - prior_work[key]
                                            for key in ("program_batches", "program_vertices")}
            prior_work = event
        elif kind == "phase":
            if prior_phase:
                phase_intervals.append({"start": prior_phase["elapsed_seconds"],
                                        "end": event["elapsed_seconds"], "event": event})
            prior_phase = event
        elif kind == "sync":
            syncs.append(event)
        elif kind == "sync_interval":
            if prior_sync_interval and event["cumulative_syncs"] >= prior_sync_interval["cumulative_syncs"]:
                sync_intervals.append({"start": prior_sync_interval["elapsed_seconds"],
                                       "end": event["elapsed_seconds"], "event": event})
            prior_sync_interval = event
    windows = []
    for window in metadata["windows"]:
        selected = [item for item in gpu_intervals
                    if full_interval(item["start"], item["end"], window, exclusions)]
        seconds = sum(item["end"] - item["start"] for item in selected)
        flips = sum(item["flips"] for item in selected)
        result = {"requested_start_seconds": window[0], "requested_end_seconds": window[1],
                  "complete_report_intervals": len(selected), "measured_seconds": round(seconds, 6),
                  "completed_flips": flips,
                  "fps": round(flips / seconds, 4) if seconds else None,
                  "mean_ms_per_frame": round(seconds * 1000 / flips, 4) if flips else None,
                  "reported_fps_time_weighted": round(sum(item["reported_fps"] *
                      (item["end"] - item["start"]) for item in selected) / seconds, 4)
                      if seconds else None,
                  "first_interval_start_seconds": selected[0]["start"] if selected else None,
                  "last_interval_end_seconds": selected[-1]["end"] if selected else None,
                  "overlapping_exclusions": [item for item in exclusions
                                             if overlap(window[0], window[1], item)]}
        inputs = [item for item in selected if "input" in item]
        input_flips = sum(item["flips"] for item in inputs)
        result["hw_input"] = {"weighted_flips": input_flips}
        for key in ("packing_ms_per_frame", "submit_ms_per_frame", "rows_per_frame"):
            result["hw_input"][key] = round(sum(item["input"][key] * item["flips"]
                for item in inputs) / input_flips, 4) if input_flips else None
        work = [item for item in selected if "work" in item]
        work_flips = sum(item["flips"] for item in work)
        result["vertex_work"] = {"weighted_flips": work_flips}
        for key in ("program_batches", "program_vertices"):
            total = sum(item["work"][key] for item in work)
            result["vertex_work"][key] = total
            result["vertex_work"][key + "_per_frame"] = round(total / work_flips, 4) if work_flips else None
        phases = [item for item in phase_intervals
                  if full_interval(item["start"], item["end"], window, exclusions)]
        phase_draws = sum(item["event"]["batch_draws"] for item in phases)
        result["hw_phase"] = {"weighted_draws": phase_draws, "batch_reports": len(phases),
            "us_per_draw": {key: round(sum(item["event"]["us_per_draw"][key] *
                item["event"]["batch_draws"] for item in phases) / phase_draws, 4)
                if phase_draws else None for key in PHASE_NAMES}}
        selected_syncs = [event for event in syncs
                          if window[0] <= event["elapsed_seconds"] <= window[1]
                          and not any(overlap(event["elapsed_seconds"],
                              event["elapsed_seconds"] + 0.000001, item) for item in exclusions)]
        result["hw_sync_cumulative_reports"] = selected_syncs
        detailed = [item for item in sync_intervals
                    if full_interval(item["start"], item["end"], window, exclusions)]
        sync_seconds = sum(item["end"] - item["start"] for item in detailed)
        sync_summary = {"complete_report_intervals": len(detailed),
                        "host_seconds": round(sync_seconds, 6),
                        "time_sources": sorted({item["event"]["sync_time_source"] for item in detailed})}
        for key in ("interval_syncs", "color_readbacks", "depth_readbacks", "event_fences",
                    "color_allocations", "color_reuses", "depth_allocations", "depth_reuses"):
            sync_summary[key] = sum(item["event"][key] for item in detailed)
        for key in ("interval_sync_ms", "color_map_ms", "color_copy_ms", "depth_map_ms",
                    "depth_copy_ms", "event_fence_wait_ms"):
            sync_summary[key] = round(sum(item["event"][key] for item in detailed), 6)
        sync_summary["mean_ms_per_sync"] = round(sync_summary["interval_sync_ms"] /
            sync_summary["interval_syncs"], 6) if sync_summary["interval_syncs"] else None
        result["hw_sync_intervals"] = sync_summary
        result["hw_counters"] = summarize_counters([item for item in counter_intervals
            if full_interval(item["start"], item["end"], window, exclusions)])
        windows.append(result)
    return {"schema_version": 1, "metadata": metadata, "windows": windows,
            "method": "Exact flip deltas / host seconds, using only full report intervals; "
                      "HW INPUT is weighted by flips; HW PHASE by 5000-draw batches; "
                      "detailed HW SYNC totals sum complete sync-report intervals.",
            "limits": ["Host timestamps are report-observation times, quantized by polling/log buffering.",
                       "No per-frame timings or frame-time percentiles are available.",
                       "Legacy HW SYNC cumulative means are stored separately and cannot be compared "
                       "as interval latency totals. New interval total times retain the logger's precision; "
                       "rows without interval_ms are explicitly marked as rounded-mean-derived.",
                       "Reports preceding monitor attachment have no recoverable host timestamps."]}


def save_summary(base, metadata, events):
    summary = summarize_events(metadata, events)
    json_path = base.with_suffix(".json")
    json_temp = base.with_suffix(".json.tmp")
    json_temp.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    json_temp.replace(json_path)
    rows = ["# Gameplay performance observation", "", f"Probe: `{metadata['name']}`",
            f"Executable SHA256: `{metadata['executable_sha256']}`", "",
            "FPS and mean frame time use completed flips over complete host-observed report intervals.",
            "Capture/sampling intervals are excluded; unavailable windows are shown as n/a.", "",
            "| Requested seconds | Measured seconds | Flips | FPS | Mean ms/frame | Pack ms/frame | Submit ms/frame |",
            "| --- | ---: | ---: | ---: | ---: | ---: | ---: |"]
    route = metadata.get("route_validation")
    if route:
        rows[5:5] = [f"Route qualification: {route['note']}",
                     f"Unknown script controls logged: {len(route['unknown_buttons'])}.", ""]
    def display(value):
        return "n/a" if value is None else f"{value:.2f}"
    for item in summary["windows"]:
        rows.append(f"| {item['requested_start_seconds']:g}-{item['requested_end_seconds']:g} | "
            f"{item['measured_seconds']:.2f} | {item['completed_flips']} | {display(item['fps'])} | "
            f"{display(item['mean_ms_per_frame'])} | {display(item['hw_input']['packing_ms_per_frame'])} | "
            f"{display(item['hw_input']['submit_ms_per_frame'])} |")
    if any(item["hw_sync_intervals"]["complete_report_intervals"] for item in summary["windows"]):
        rows += ["", "Detailed synchronization totals over complete report intervals:", "",
                 "| Requested seconds | Host seconds | Syncs | Sync ms | Color maps / map ms / copy ms | "
                 "Depth maps / map ms / copy ms | Fences / wait ms | Color alloc / reuse | Depth alloc / reuse |",
                 "| --- | ---: | ---: | ---: | --- | --- | --- | --- | --- |"]
        for item in summary["windows"]:
            sync = item["hw_sync_intervals"]
            rows.append(f"| {item['requested_start_seconds']:g}-{item['requested_end_seconds']:g} | "
                f"{sync['host_seconds']:.2f} | {sync['interval_syncs']} | {sync['interval_sync_ms']:.3f} | "
                f"{sync['color_readbacks']} / {sync['color_map_ms']:.3f} / {sync['color_copy_ms']:.3f} | "
                f"{sync['depth_readbacks']} / {sync['depth_map_ms']:.3f} / {sync['depth_copy_ms']:.3f} | "
                f"{sync['event_fences']} / {sync['event_fence_wait_ms']:.3f} | "
                f"{sync['color_allocations']} / {sync['color_reuses']} | "
                f"{sync['depth_allocations']} / {sync['depth_reuses']} |")
    rows += ["", "Observation limits:", ""] + [f"- {item}" for item in summary["limits"]]
    base.with_suffix(".md").write_text("\n".join(rows) + "\n", encoding="utf-8")
    return summary


def monitor(args, project, base):
    process_path = project / "analysis" / f"{args.name}-process.json"
    process = json.loads(process_path.read_text(encoding="utf-8-sig"))
    stat = process_path.stat()
    zero_epoch = getattr(stat, "st_birthtime", stat.st_ctime)
    executable = project / "build" / "RelWithDebInfo" / process["executable"]
    log_path = project / "analysis" / f"{args.name}.stderr.log"
    exclusions = [{"kind": "sample", "start": start, "end": end}
                  for start, end in ranges(args.sample)]
    exclusions += [{"kind": "explicit", "start": start, "end": end}
                   for start, end in ranges(args.exclude)]
    if "capture_windows" in process:
        exclusions += [{"kind": "capture", "start": start, "end": end}
                       for start, end in process["capture_windows"]]
    elif process.get("capture_from", -1) >= 0 and process.get("capture_for", 0) > 0:
        start = process["capture_from"]
        exclusions.append({"kind": "capture", "start": start, "end": start + process["capture_for"]})
    events_path = base.parent / (base.name + "-events.jsonl")
    if events_path.exists():
        raise RuntimeError(f"Observation already exists: {events_path}; use summarize to rebuild reports")
    result_path = project / "analysis" / f"{args.name}.json"
    events = []
    with log_path.open("rb") as log, events_path.open("x", encoding="utf-8") as output:
        log.seek(0, 2)
        monotonic_zero = time.monotonic() - (time.time() - zero_epoch)
        metadata = {"type": "metadata", "schema_version": 1, "name": args.name,
                    "pid": process["pid"], "elapsed_zero_source": "process.json file creation time",
                    "elapsed_zero_utc": utc_text(zero_epoch), "monitor_started_utc": utc_text(time.time()),
                    "monitor_started_elapsed_seconds": round(time.monotonic() - monotonic_zero, 6),
                    "termination_source": "probe terminal result.json, or monitor elapsed limit",
                    "poll_seconds": args.poll, "preexisting_log_bytes_skipped": log.tell(),
                    "process_manifest": process, "process_manifest_path": str(process_path),
                    "executable_path": str(executable.resolve()), "executable_bytes": executable.stat().st_size,
                    "executable_sha256": sha256(executable), "windows": ranges(args.windows),
                    "exclusions": exclusions, "monitor_source_sha256": sha256(Path(__file__)),
                    "route_validation": validate_script(log_path, args.route_note)}
        output.write(json.dumps(metadata) + "\n")
        output.flush()
        print(f"Observing {args.name} pid {process['pid']} from elapsed "
              f"{metadata['monitor_started_elapsed_seconds']:.2f}s; {events_path}", flush=True)
        pending = b""
        last_summary = time.monotonic()
        stopped_at = None
        while True:
            data = log.read()
            if data:
                pending += data
                lines = pending.split(b"\n")
                pending = lines.pop()
                observed = round(time.monotonic() - monotonic_zero, 6)
                for raw in lines:
                    event = parse_line(raw.decode("utf-8", errors="replace").strip())
                    if event:
                        event["elapsed_seconds"] = observed
                        events.append(event)
                        output.write(json.dumps(event) + "\n")
                        if event["type"] == "input_error":
                            metadata["route_validation"] = validate_script(log_path, args.route_note)
                output.flush()
            now = time.monotonic()
            if now - last_summary >= 10:
                save_summary(base, metadata, events)
                last_summary = now
            if result_path.exists() and stopped_at is None and result_path.stat().st_mtime >= zero_epoch:
                try:
                    metadata["probe_result"] = json.loads(result_path.read_text(encoding="utf-8-sig"))
                    stopped_at = now
                except (OSError, json.JSONDecodeError):
                    pass  # Wrapper may still be writing the terminal result.
            if stopped_at is not None and now - stopped_at >= 1:
                metadata["terminal_result_observed_elapsed_seconds"] = round(now - monotonic_zero, 6)
                break
            if args.until is not None and now - monotonic_zero >= args.until:
                metadata["monitor_limit_elapsed_seconds"] = args.until
                break
            time.sleep(args.poll)
        metadata["monitor_ended_utc"] = utc_text(time.time())
        metadata["route_validation"] = validate_script(log_path, args.route_note)
        metadata["executable_sha256_at_end"] = sha256(executable)
        output.write(json.dumps({"type": "metadata_end", "metadata": metadata}) + "\n")
        save_summary(base, metadata, events)
        print(f"Saved {base.with_suffix('.json')} and {base.with_suffix('.md')}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("monitor", "summarize"))
    parser.add_argument("--name", required=True, help="Existing probe name")
    parser.add_argument("--windows", default=DEFAULT_WINDOWS, help="Comma-separated START:END seconds")
    parser.add_argument("--sample", default="", help="Sampling ranges to exclude")
    parser.add_argument("--exclude", default="", help="Other diagnostic ranges to exclude")
    parser.add_argument("--poll", type=float, default=0.1)
    parser.add_argument("--until", type=float, help="Stop monitoring at this elapsed time; game keeps running")
    parser.add_argument("--route-note", help="Evidence-based qualification of the observed gameplay route")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.name):
        parser.error("Name must contain only letters, digits, underscores, or hyphens")
    if args.poll <= 0:
        parser.error("Polling interval must be positive")
    project = Path(__file__).resolve().parent
    base = project / "analysis" / f"{args.name}-performance"
    if args.command == "monitor":
        monitor(args, project, base)
    else:
        records = [json.loads(line) for line in (base.parent / (base.name + "-events.jsonl"))
                   .read_text(encoding="utf-8").splitlines() if line]
        metadata = records[0]
        end = next((item for item in reversed(records) if item["type"] == "metadata_end"), None)
        if end:
            metadata = end["metadata"]
        previous_note = metadata.get("route_validation", {}).get("note")
        metadata["route_validation"] = validate_script(project / "analysis" / f"{args.name}.stderr.log",
                                                        args.route_note or previous_note)
        events = [item for item in records if item["type"] not in ("metadata", "metadata_end")]
        save_summary(base, metadata, events)
        print(f"Rebuilt {base.with_suffix('.json')} and {base.with_suffix('.md')}")


if __name__ == "__main__":
    main()
