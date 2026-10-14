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
            errors += len(item.get("counter_errors", []))
        positions = [start] + [item["elapsed"] for item in samples] + [end]
        window = dict(window)
        window["gpu_observation"] = {
            "sample_count": len(samples), "counter_errors": errors,
            "max_gap_seconds": round(max(b-a for a, b in zip(positions, positions[1:])), 3),
            "adapter": adapter,
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
    return {"name": name, "sha256": metadata["executable_sha256"],
            "sha256_at_end": metadata.get("executable_sha256_at_end"),
            "terminal": result, "route": metadata["route_validation"],
            "gpu_files": [str(path) for path in paths],
            "nvidia_file": str(nvidia_path) if nvidia_path.exists() else None, "windows": windows,
            "aggregate_ms_per_frame": seconds * 1000 / flips if flips else None,
            "aggregate_fps": flips / seconds if seconds else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--name", required=True)
    args = parser.parse_args()
    for value in (args.baseline, args.candidate, args.name):
        if not re.fullmatch(r"[A-Za-z0-9_-]+", value):
            parser.error("Names must contain only letters, digits, underscores or hyphens")
    project = Path(__file__).resolve().parent
    baseline, candidate = observation(project, args.baseline), observation(project, args.candidate)
    before, after = baseline["aggregate_ms_per_frame"], candidate["aggregate_ms_per_frame"]
    output = {"baseline": baseline, "candidate": candidate,
              "frame_time_reduction_percent": 100 * (before-after) / before if before and after else None,
              "limits": ["Confirm matching completed-flip captures and vertex rows before interpreting the ratio.",
                         "GPU Engine telemetry is sampled; missing or invalid counters are not proof of idle hardware.",
                         "A single paired fixed view does not establish active-combat or campaign-wide performance."]}
    path = project / "analysis" / f"{args.name}.json"
    path.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(path)


if __name__ == "__main__":
    main()
