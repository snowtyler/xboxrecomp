"""Summarise GET invariants and SDK waits from a bounded candidate probe.

Process exit alone is never a correctness pass. Captures require visual review;
this file reports only the trace, monitor, configuration and failure checks.
"""
import argparse
import csv
import json
import re
from pathlib import Path

def summarize_monitor(prefix: Path):
    """Long-route monitor checks; does not claim SDK-wait trace coverage."""
    def side(suffix):
        return prefix.with_name(prefix.name + suffix)
    config = json.loads(side("-process.json").read_text())["configuration"]
    process = json.loads(side(".json").read_text())
    log = side(".stderr.log").read_text(errors="replace")
    rows = re.findall(
        r"\[PB GET\] reads=(\d+) far=(\d+) invalid=(\d+) untrusted=(\d+) max_lag=([0-9A-F]+) "
        r"publications=(\d+) freezes=(\d+) backward=(\d+) ring_calls=(\d+)", log)
    keys = ["reads", "far_reads", "invalid_get", "untrusted_reads", "max_lag",
            "publications", "freezes", "backward_jumps", "ring_calls"]
    monitors = [dict(zip(keys, [int(v, 16 if i == 4 else 10) for i, v in enumerate(row)])) for row in rows]
    monitor = monitors[-1] if monitors else {}
    snapshot = json.loads(side("-guest-inspection.json").read_text())
    gpu = snapshot.get("gpu", {})
    errors = []
    if config.get("RECOMP_NV2A_GET_MONITOR") != "1" or not monitor.get("reads"):
        errors.append("monitor missing or did not observe ring reads")
    if config.get("RECOMP_NV2A_GET_PROGRESS") not in (None, "0", "1"):
        errors.append("ambiguous GET progress flag; use 0 or 1")
    if gpu.get("pfb_trap_active") is not True or gpu.get("pfb_trap_matches_guest") is not True:
        errors.append("PFB trap not verified active")
    if any(config.get(k) is not None for k in ("RECOMP_NV2A_IRQ_GATE", "RECOMP_NV2A_PFB_POLL",
                                  "RECOMP_GPU_TIMING", "RECOMP_HW_GPU_TRACE")):
        errors.append("IRQ gate, PFB polling or GPU profiling enabled")
    if process.get("exit_code") != 3 or process.get("stopped") or process.get("timed_out"):
        errors.append("probe did not reach its normal watchdog exit")
    for row in monitors:
        if any(row[k] for k in ("invalid_get", "untrusted_reads", "freezes", "backward_jumps", "ring_calls")):
            errors.append("monitor reports unsupported or failed execution")
            break
    progress = config.get("RECOMP_NV2A_GET_PROGRESS") == "1"
    waits = re.findall(r"\[PB WAIT\] checks=(\d+) events=(\d+) read_past=(\d+)", log)
    wait_monitor = dict(zip(("checks", "event_decisions", "event_read_past"), map(int, waits[-1]))) if waits else {}
    snapshot_wait_monitor = gpu.get("wait_monitor", {})
    if not wait_monitor.get("checks"):
        errors.append("post-arm observer missing or did not observe SDK checks")
    if not snapshot_wait_monitor.get("checks"):
        errors.append("post-arm observer not verified in process snapshot")
    if progress and any(int(row[2]) for row in waits):
        errors.append("SDK selected event wait after reserved words were consumed")
    if progress and snapshot_wait_monitor.get("event_read_past"):
        errors.append("snapshot reports event wait after reserved fetch")
    if progress and (not monitor.get("publications") or any(row["far_reads"] or row["max_lag"] > 0x3FC for row in monitors)):
        errors.append("GET progress publication or lag invariant failed")
    if not progress and not monitor.get("far_reads"):
        errors.append("control did not expose stale GET")
    failures = [line for line in log.splitlines() if any(p in line for p in
                ("[PB ERROR]", "[PB RESUME]", "[HW FENCE] batch failed", "[HW FENCE] rejected",
                 "[FAULT]", "EXCEPTION_ACCESS_VIOLATION"))]
    if failures:
        errors.append("runtime, pushbuffer or fence failure")
    return dict(name=prefix.name, progress=progress, configuration=config, monitor=monitor,
                wait_monitor=wait_monitor,
                snapshot_wait_monitor=snapshot_wait_monitor,
                reports=len(monitors), automated_checks_passed=not errors, errors=errors,
                failure_lines=failures[:20],
                qualification_status="failed" if errors else "monitor_checks_passed_pending_visual_review",
                limits="O(1) post-arm observation without ordered trace; guest-state transitions and captures still require review.")

def summarize(prefix: Path):
    def side(suffix):
        return prefix.with_name(prefix.name + suffix)
    config = json.loads(side("-process.json").read_text())["configuration"]
    observer = json.loads(side("-order.json").read_text())
    process = json.loads(side(".json").read_text())
    log = side(".stderr.log").read_text(errors="replace")
    monitors = re.findall(
        r"\[PB GET\] reads=(\d+) far=(\d+) invalid=(\d+) untrusted=(\d+) max_lag=([0-9A-F]+) "
        r"publications=(\d+) freezes=(\d+) backward=(\d+) ring_calls=(\d+)", log)
    keys = ["reads", "far_reads", "invalid_get", "untrusted_reads", "max_lag",
            "publications", "freezes", "backward_jumps", "ring_calls"]
    monitor = dict(zip(keys, [int(v, 16 if i == 4 else 10) for i, v in enumerate(monitors[-1])])) if monitors else {}
    snapshot = json.loads(side("-guest-inspection.json").read_text())
    pfb = snapshot.get("gpu", {}).get("pfb_trap_active")
    pfb_matches = snapshot.get("gpu", {}).get("pfb_trap_matches_guest")
    releases, fetches, event_waits, protocol = {}, {}, {}, {}
    checks = polls = event_paths = already_read = old_fetch = unmatched_end = 0
    poll_us = []; wait_us = []; unsafe = []; last_time = 0.0
    with side("-order.csv").open(newline="") as stream:
        for row in csv.DictReader(stream):
            event, tid, sequence = (int(row[k]) for k in ("event", "tid", "sequence"))
            a, b, c, d = [int(row[k], 16) for k in "abcd"]
            last_time = float(row["qpc_seconds"])
            if event == 7 and b == 0x1D70:
                releases[c] = (sequence, a - 4)
            elif event == 16:
                fetches[a] = (sequence, b, c)
            elif event == 1 and a == 0x003AAC9C:
                if b == 2:
                    event_waits[tid] = last_time
                elif b == 3:
                    event_waits.pop(tid, None)
            elif event == 13:
                protocol[tid] = (a, sequence)
            elif event == 14:
                checks += 1
                if d >= 0x2000:
                    event_paths += 1
                    release = releases.get(a)
                    fetch = fetches.get(b + 20)
                    if release and release[1] == b and fetch and fetch[0] > release[0]:
                        already_read += 1
                        if fetch[1:] != (0x100, 5):
                            old_fetch += 1
                            if len(unsafe) < 20:
                                unsafe.append(dict(sequence=sequence, serial=hex(a), command=hex(b),
                                                   get=hex(c), distance=hex(d), fetch_sequence=fetch[0]))
            elif event == 17:
                polls += 1
            elif event == 18:
                if tid not in protocol or protocol[tid][0] != a:
                    unmatched_end += 1
                protocol.pop(tid, None)
                wait_us.append(c)
                if b == 1:
                    poll_us.append(d)
    waiting_age = max((last_time - t for t in event_waits.values()), default=0)
    failures = [line for line in log.splitlines() if any(p in line for p in
                ("[PB ERROR]", "[PB RESUME]", "[HW FENCE] batch failed", "[HW FENCE] rejected"))]
    errors = []
    if not observer.get("records") or any(observer.get(k) for k in ("discarded", "overwritten", "read_error")):
        errors.append("incomplete order trace")
    if not monitor.get("reads"):
        errors.append("monitor did not observe ring reads")
    if pfb is not True or pfb_matches is not True:
        errors.append("PFB trap not verified active")
    if config.get("RECOMP_NV2A_IRQ_GATE") or config.get("RECOMP_NV2A_PFB_POLL"):
        errors.append("IRQ gate or PFB polling enabled")
    if config.get("RECOMP_GPU_TIMING") or config.get("RECOMP_HW_GPU_TRACE"):
        errors.append("GPU profiling enabled")
    if process.get("exit_code") != 3 or process.get("stopped") or process.get("timed_out"):
        errors.append("probe did not reach its normal watchdog exit")
    if not checks or not wait_us or unmatched_end:
        errors.append("SDK wait hooks missing or inconsistent")
    if failures:
        errors.append("pushbuffer or fence failure")
    if any(monitor.get(k) for k in ("invalid_get", "untrusted_reads", "freezes", "backward_jumps", "ring_calls")):
        errors.append("monitor reports unsupported or failed execution")
    progress = config.get("RECOMP_NV2A_GET_PROGRESS") == "1"
    wait_rows = re.findall(r"\[PB WAIT\] checks=(\d+) events=(\d+) read_past=(\d+)", log)
    wait_monitor = dict(zip(("checks", "event_decisions", "event_read_past"), map(int, wait_rows[-1]))) if wait_rows else {}
    if progress and wait_monitor.get("event_read_past"):
        errors.append("direct observer reports event wait after reserved fetch")
    if progress and (monitor.get("far_reads") or monitor.get("max_lag",0)>0x3FC or already_read or waiting_age > 5):
        errors.append("GET progress invariant or graphics wait failed")
    if not progress and not monitor.get("far_reads"):
        errors.append("control did not expose stale GET")
    return dict(name=prefix.name, progress=progress, configuration=config, monitor=monitor,
                wait_monitor=wait_monitor,
                pfb_trap_active=pfb, pfb_trap_matches_guest=pfb_matches,
                checks=checks, poll_entries=polls, event_paths=event_paths,
                waits_after_slot_fetch=already_read, waits_after_placeholder_fetch=old_fetch,
                unsafe_examples=unsafe, event_wait_age_seconds=waiting_age,
                graphics_event_stall_detected=waiting_age>5,
                completed_waits=len(wait_us), completed_poll_waits=len(poll_us),
                poll_spin_total_us=sum(poll_us), poll_spin_max_us=max(poll_us, default=0),
                wait_max_us=max(wait_us, default=0), failure_lines=failures[:20],
                automated_checks_passed=not errors, errors=errors,
                qualification_status="failed" if errors else
                    ("automated_GET_checks_passed_pending_visual_review" if progress else "diagnostic_control_only"),
                captures="require visual review")

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("prefix", type=Path)
    p.add_argument("--output", type=Path)
    p.add_argument("--monitor-only", action="store_true",
                   help="Long-route monitor checks without an ordered SDK-wait trace")
    args = p.parse_args()
    result = (summarize_monitor if args.monitor_only else summarize)(args.prefix)
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(text)
    else:
        print(text)
    raise SystemExit(0 if result["automated_checks_passed"] else 1)
