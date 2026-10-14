"""Capture a bounded, hidden startup probe; runtime options are inherited."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--seconds", type=int, default=15)
parser.add_argument("--name", default="probe")
parser.add_argument("--exe", default="brute_force_e3_recomp.exe")
parser.add_argument("--stop-file", type=Path,
                    help="End this probe when this initially absent file appears")
parser.add_argument("--capture-from", type=float, default=-1,
                    help="Start completed-flip capture this many seconds after launch")
parser.add_argument("--capture-for", type=float, default=20)
parser.add_argument("--captures", default="", help="Additional START:DURATION completed-frame capture windows")
parser.add_argument("--sample-from", type=float, default=-1)
parser.add_argument("--sample-for", type=float, default=10)
parser.add_argument("--measure", action="store_true",
                    help="Record comparable host-timed gameplay report intervals")
parser.add_argument("--inspect-from", type=float, default=-1,
                    help="Read-only guest/APU snapshot at this elapsed time")
args = parser.parse_args()
capture_windows = []
if args.capture_from >= 0 and args.capture_for > 0:
    capture_windows.append((args.capture_from, args.capture_from + args.capture_for))
try:
    for item in filter(None, args.captures.split(',')):
        begin, duration = map(float, item.split(':'))
        if begin < 0 or duration <= 0:raise ValueError('Invalid capture range')
        capture_windows.append((begin, begin + duration))
except ValueError:
    parser.error('Captures must be nonnegative START:positive DURATION pairs')
capture_windows.sort()
if args.stop_file and args.stop_file.exists():
    parser.error("stop file already exists; choose a fresh path")
if not args.name.replace("-", "").replace("_", "").isalnum():
    parser.error("name must contain only letters, digits, underscores, or hyphens")
p = Path(__file__).resolve().parent
env = os.environ.copy()
env.setdefault("BRUTE_FORCE_E3_DIR", "D:/Downloads/Brute Force E3 Prototype")
env.setdefault("BRUTE_FORCE_E3_SAVE_DIR", str(p / "saves"))
env["RECOMP_WATCHDOG_SECS"] = str(args.seconds)
capture_flag = p / f"analysis/{args.name}-capture.flag"
if capture_windows:
    if capture_flag.exists():
        parser.error("capture flag already exists; choose a fresh probe name")
    env["RECOMP_FB_DUMP_FLIPS"] = str(capture_flag)
capturing = False
startup = subprocess.STARTUPINFO()
startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
start = time.monotonic()
metrics_proc = None
with (p / f"analysis/{args.name}.stdout.log").open("w") as out, (p / f"analysis/{args.name}.stderr.log").open("w") as err:
    proc = subprocess.Popen([str(p / "build/RelWithDebInfo" / args.exe)],
                            cwd=p, env=env, stdout=out, stderr=err,
                            startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    config_keys = ["RECOMP_PB_EXEC", "RECOMP_NV2A_NOTIFY", "RECOMP_HW_RASTER", "RECOMP_HW_VERTEX",
                   "RECOMP_FB_WINDOW", "RECOMP_KEYBOARD", "RECOMP_AC97_READY", "RECOMP_APU_DSP_ACK",
                   "RECOMP_AUDIO_MUTE", "RECOMP_QUIET", "RECOMP_HW_GPU_TRACE", "RECOMP_GPU_TIMING",
                   "RECOMP_HW_PRESENT", "RECOMP_HW_COLOR_RELOAD", "RECOMP_HW_TARGET_POOL_DISABLE", "RECOMP_HW_TARGET_POOL",
                   "RECOMP_HW_BULK_READBACK", "RECOMP_HW_READBACK_FENCE", "RECOMP_HW_VSH_GATHER_ALL", "RECOMP_PB_REPORT_MS", "RECOMP_PERF_WINDOWS",
                   "RECOMP_AUDIO_TRACE", "RECOMP_PAD_SCRIPT", "RECOMP_WATCHDOG_SECS"]
    (p / f"analysis/{args.name}-process.json").write_text(json.dumps({"pid": proc.pid,
        "executable": args.exe, "configuration": {key: env.get(key) for key in config_keys},
        "capture_from": args.capture_from, "capture_for": args.capture_for,
        "capture_windows": capture_windows}, indent=2))
    if args.measure:
        measure_command = [os.sys.executable, str(p / "performance.py"), "monitor",
                           "--name", args.name, "--until", str(args.seconds + 15),
                           "--windows", env.get("RECOMP_PERF_WINDOWS",
                               "500:530,600:615,640:660,710:730,750:770,800:830")]
        if args.sample_from >= 0:
            measure_command += ["--sample",
                f"{args.sample_from}:{args.sample_from + args.sample_for + 5}"]
        metrics_proc = subprocess.Popen(measure_command, stdout=out, stderr=err,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
    timed_out = False
    stopped = False
    sampled = False
    inspected = False
    try:
        deadline = start + args.seconds + 10
        while proc.poll() is None:
            elapsed = time.monotonic() - start
            if args.inspect_from >= 0 and not inspected and elapsed >= args.inspect_from:
                inspected = True
                subprocess.run([os.sys.executable, str(p / "inspect_guest.py"),
                                "--pid", str(proc.pid), "--output",
                                str(p / f"analysis/{args.name}-guest-inspection.json")],
                               stdout=out, stderr=err, timeout=30)
            if args.sample_from >= 0 and not sampled and elapsed >= args.sample_from:
                sampled = True
                subprocess.run([os.sys.executable, str(p / "sample_cpu.py"),
                                "--pid", str(proc.pid), "--seconds", str(args.sample_for),
                                "--output", str(p / f"analysis/{args.name}-cpu-samples.json")],
                               stdout=out, stderr=err, timeout=args.sample_for + 30)
            should_capture = any(begin <= elapsed < end for begin, end in capture_windows)
            if should_capture and not capturing:
                capture_flag.write_text("Capture completed frames for this probe.\n")
                capturing = True
            if capturing and not should_capture:
                capture_flag.unlink()
                capturing = False
            if args.stop_file and args.stop_file.exists():
                stopped = True
                proc.kill()
                break
            if time.monotonic() >= deadline:
                raise subprocess.TimeoutExpired(proc.args, args.seconds + 10)
            time.sleep(0.2)
        code = proc.wait()
    except subprocess.TimeoutExpired:
        timed_out = True
        proc.kill()
        code = proc.wait()
    finally:
        if capturing:
            capture_flag.unlink(missing_ok=True)
result = {"exit_code": code, "timed_out": timed_out,
          "stopped": stopped,
          "elapsed_seconds": round(time.monotonic() - start, 2)}
(p / f"analysis/{args.name}.json").write_text(json.dumps(result, indent=2))
if metrics_proc:
    try:
        metrics_proc.wait(timeout=15)
    except subprocess.TimeoutExpired:
        metrics_proc.kill()
        metrics_proc.wait()
    if metrics_proc.returncode:
        print(f"Performance monitor exited {metrics_proc.returncode}; inspect its logs.")
print(json.dumps(result))
