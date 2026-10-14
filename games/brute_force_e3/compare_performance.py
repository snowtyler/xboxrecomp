"""Compare completed fixed-view probes and their read-only GPU observations.

This reports evidence; a matching route must still be established from captures.
GPU Engine percentages are engine observations, not global GPU utilization.
"""
import argparse
import collections
import csv
import datetime as dt
import json
from pathlib import Path
import re
import statistics

ENGINE = re.compile(r"pid_(\d+)_(luid_.*_phys_\d+)_eng_\d+_engtype_3d$")


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def observation(project, name):
    analysis = project / "analysis"
    report = read_json(analysis / f"{name}-performance.json")
    result = read_json(analysis / f"{name}.json")
    metadata = report["metadata"]
    zero = dt.datetime.fromisoformat(metadata["elapsed_zero_utc"])
    pid = metadata["pid"]
    records = []
    paths = sorted(analysis.glob(f"{name}*-gpu-load.jsonl"))
    for path in paths:
        for line in path.read_text(encoding="utf-8-sig").splitlines():
            item = json.loads(line)
            item["elapsed"] = (dt.datetime.fromisoformat(item["utc"].replace("Z", "+00:00")) - zero).total_seconds()
            records.append(item)
    records.sort(key=lambda item: item["elapsed"])
    nvidia_path = analysis / f"{name}-nvidia.jsonl"
    nvidia = []
    if nvidia_path.exists():
        for line in nvidia_path.read_text(encoding="utf-8-sig").splitlines():
            item = json.loads(line)
            item["elapsed"] = (dt.datetime.fromisoformat(item["utc"].replace("Z", "+00:00")) - zero).total_seconds()
            nvidia.append(item)
    adapters = collections.Counter()
    for item in records:
        for engine in item["engines"]:
            match = ENGINE.fullmatch(engine["instance"])
            if match and int(match[1]) == pid:
                adapters[match[2]] += 1
    adapter = adapters.most_common(1)[0][0] if adapters else None
    windows = []
    for window in report["windows"]:
        start, end = window["requested_start_seconds"], window["requested_end_seconds"]
        samples = [item for item in records if start <= item["elapsed"] <= end]
        values = collections.defaultdict(list)
        errors = 0
        competing_load = []
        for item in samples:
            per_pid = collections.defaultdict(float)
            for engine in item["engines"]:
                match = ENGINE.fullmatch(engine["instance"])
                if match and match[2] == adapter:
                    # Avoid summing different queues of the same process.
                    engine_pid = int(match[1])
                    per_pid[engine_pid] = max(per_pid[engine_pid], engine["percent"])
            for engine_pid in set(values) | set(per_pid) | {pid}:
                if engine_pid not in values:
                    values[engine_pid] = [0.0] * (samples.index(item))
                values[engine_pid].append(per_pid.get(engine_pid, 0.0))
            competing_load.append(sum(value for engine_pid, value in per_pid.items() if engine_pid != pid))
            errors += len(item.get("counter_errors", []))
        positions = [start] + [item["elapsed"] for item in samples] + [end]
        window = dict(window)
        window["gpu_observation"] = {
            "sample_count": len(samples), "counter_errors": errors,
            "max_gap_seconds": round(max(b-a for a, b in zip(positions, positions[1:])), 3),
            "adapter": adapter,
            "competing_3d_engine_peak_sum_percent": max(competing_load) if competing_load else None,
            "process_3d_engine_percent": {
                str(engine_pid): {"owned": engine_pid == pid,
                                  "mean": round(sum(data) / len(data), 3),
                                  "peak": round(max(data), 3)}
                for engine_pid, data in sorted(values.items())
            },
        }
        hardware = [item for item in nvidia if start <= item["elapsed"] <= end]
        metrics = collections.defaultdict(list)
        for item in hardware:
            if item["exit_code"] != 0:
                continue
            for row in item["csv"]:
                fields = next(csv.reader([row], skipinitialspace=True))
                for key, value in zip(item["fields"][1:], fields[1:]):
                    try:
                        metrics[(fields[0], key)].append(float(value))
                    except ValueError:
                        pass  # Unsupported counters remain absent, not zero.
        window["nvidia_observation"] = {
            "sample_count": len(hardware),
            "query_errors": sum(item["exit_code"] != 0 for item in hardware),
            "metrics": {f"{device}/{key}": {"mean": round(sum(data)/len(data), 3),
                                           "min": min(data), "max": max(data)}
                        for (device, key), data in sorted(metrics.items())},
        }
        windows.append(window)
    seconds = sum(window["measured_seconds"] for window in windows)
    flips = sum(window["completed_flips"] for window in windows)
    work_flips = sum(window["vertex_work"]["weighted_flips"] for window in windows)
    counters = [window["hw_counters"]["totals"] for window in windows if window.get("hw_counters")]
    counter_flips = sum(item["flips"] for item in counters)
    return {"name": name, "sha256": metadata["executable_sha256"],
            "sha256_at_end": metadata.get("executable_sha256_at_end"),
            "terminal": result, "route": metadata["route_validation"],
            "gpu_files": [str(path) for path in paths],
            "nvidia_file": str(nvidia_path) if nvidia_path.exists() else None, "windows": windows,
            "aggregate_ms_per_frame": seconds * 1000 / flips if flips else None,
            "aggregate_fps": flips / seconds if seconds else None,
            "vertex_work_per_frame": {key: sum(window["vertex_work"][key] for window in windows) / work_flips
                                      if work_flips else None for key in ("program_batches", "program_vertices")},
            "executor_cpu_ms_per_frame": sum(item["executor_cpu_ms"] for item in counters) / counter_flips
                if counter_flips and all(item["executor_cpu_ms"] is not None for item in counters) else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, action="append")
    parser.add_argument("--candidate", required=True, action="append")
    parser.add_argument("--name", required=True)
    args = parser.parse_args()
    if len(args.baseline) != len(args.candidate):
        parser.error("Supply equally many baseline and candidate runs in chronological pair order")
    for value in args.baseline + args.candidate + [args.name]:
        if not re.fullmatch(r"[A-Za-z0-9_-]+", value):
            parser.error("Names must contain only letters, digits, underscores or hyphens")
    project = Path(__file__).resolve().parent
    baselines = [observation(project, name) for name in args.baseline]
    candidates = [observation(project, name) for name in args.candidate]
    baseline, candidate = baselines[0], candidates[0]
    before, after = baseline["aggregate_ms_per_frame"], candidate["aggregate_ms_per_frame"]
    output = {"baseline": baseline, "candidate": candidate,
              "frame_time_reduction_percent": 100 * (before-after) / before if before and after else None,
              "limits": ["Confirm matching completed-flip captures and vertex rows before interpreting the ratio.",
                         "GPU Engine telemetry is sampled; missing or invalid counters are not proof of idle hardware.",
                         "A single paired fixed view does not establish active-combat or campaign-wide performance."]}
    if len(baselines) > 1:
        before_values = [run["aggregate_ms_per_frame"] for run in baselines]
        after_values = [run["aggregate_ms_per_frame"] for run in candidates]
        if any(value is None for value in before_values + after_values):
            parser.error("Every repeat must have complete measurement windows")
        before_mean, after_mean = statistics.mean(before_values), statistics.mean(after_values)
        pooled_sd = ((statistics.variance(before_values) + statistics.variance(after_values)) / 2) ** 0.5
        improvement = before_mean - after_mean
        work_matches = all(
            first["vertex_work_per_frame"][key] is not None and second["vertex_work_per_frame"][key] is not None
            and first["vertex_work_per_frame"][key] > 0
            and abs(second["vertex_work_per_frame"][key] / first["vertex_work_per_frame"][key] - 1) <= 0.01
            for first, second in zip(baselines, candidates) for key in ("program_batches", "program_vertices"))
        cpu_consistent = (all(run["executor_cpu_ms_per_frame"] is not None for run in baselines + candidates)
                          and all(second["executor_cpu_ms_per_frame"] < first["executor_cpu_ms_per_frame"]
                                  for first, second in zip(baselines, candidates)))
        hardware_complete = all(
            window["gpu_observation"]["sample_count"] >= 3
            and window["gpu_observation"]["adapter"] is not None
            and window["gpu_observation"]["max_gap_seconds"] <= 20
            and any(value["owned"] for value in window["gpu_observation"]["process_3d_engine_percent"].values())
            and window["nvidia_observation"]["sample_count"] >= 3
            and all(any(key.endswith('/' + metric) for key in window["nvidia_observation"]["metrics"])
                    for metric in ("graphics_mhz", "memory_mhz", "power_w"))
            for run in baselines + candidates for window in run["windows"])
        low_competing_load = hardware_complete and all(
            window["gpu_observation"]["competing_3d_engine_peak_sum_percent"] <= 5
            for run in baselines + candidates for window in run["windows"])
        paired_context = []
        for first, second in zip(baselines, candidates):
            contexts = []
            for left, right in zip(first['windows'], second['windows']):
                a, b = left['gpu_observation'], right['gpu_observation']
                external_a = {pid: value['mean'] for pid, value in a['process_3d_engine_percent'].items() if not value['owned']}
                external_b = {pid: value['mean'] for pid, value in b['process_3d_engine_percent'].items() if not value['owned']}
                differences = {pid: external_b.get(pid, 0) - external_a.get(pid, 0)
                               for pid in external_a.keys() | external_b.keys()}
                clocks_a, clocks_b = left['nvidia_observation']['metrics'], right['nvidia_observation']['metrics']
                clock_keys = {key for key in clocks_a if key.endswith(('/graphics_mhz', '/memory_mhz'))}
                clocks_match = bool(clock_keys) and clock_keys == {key for key in clocks_b if key.endswith(('/graphics_mhz', '/memory_mhz'))}
                if clocks_match:
                    clocks_match = all(max(clocks_a[key]['min'], clocks_b[key]['min']) <= min(clocks_a[key]['max'], clocks_b[key]['max'])
                                       and clocks_a[key]['mean'] > 0
                                       and abs(clocks_b[key]['mean'] / clocks_a[key]['mean'] - 1) <= .10 for key in clock_keys)
                contexts.append(dict(start_seconds=left['requested_start_seconds'],
                                     competing_mean_difference_by_pid=differences,
                                     competing_mean_difference_sum=sum(external_b.values()) - sum(external_a.values()),
                                     clock_ranges_comparable=clocks_match,
                                     comparable=hardware_complete and a['adapter'] == b['adapter'] and clocks_match
                                     and a['counter_errors'] == b['counter_errors'] == 0
                                     and left['nvidia_observation']['query_errors'] == right['nvidia_observation']['query_errors'] == 0
                                     and all(abs(value) <= 5 for value in differences.values())
                                     and abs(sum(external_b.values()) - sum(external_a.values())) <= 5))
            paired_context.append(dict(baseline=first['name'], candidate=second['name'], windows=contexts))
        output["repeated_runs"] = {"baselines": baselines, "candidates": candidates,
            "pairs": len(baselines), "baseline_mean_ms": before_mean, "candidate_mean_ms": after_mean,
            "pooled_between_run_sd_ms": pooled_sd,
            "frame_time_reduction_percent": 100 * improvement / before_mean,
            "performance_threshold_passed": len(baselines) >= 3 and improvement > max(before_mean * 0.05, 2 * pooled_sd),
            "work_equal_within_one_percent": work_matches,
            "executor_cpu_reduction_consistent": cpu_consistent,
            "hardware_observation_complete": hardware_complete,
            "low_competing_gpu_load": low_competing_load,
            "competing_gpu_load_limit_percent": 5,
            "paired_hardware_context": paired_context,
            "concurrent_context_comparable": hardware_complete and all(window['comparable'] for pair in paired_context for window in pair['windows']),
            "concurrent_context_limits": "Sampled screening only: external per-process and total mean 3D load differ by at most 5 percentage points; clock ranges overlap and means differ by at most 10%. This does not establish idle-GPU or causal isolation.",
            "qualification": "Run means are independent units. Matching work, visual and correctness gates must also pass."}
    path = project / "analysis" / f"{args.name}.json"
    path.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(path)


if __name__ == "__main__":
    main()
