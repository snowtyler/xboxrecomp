"""Package a completed GET short-route series and its matching source evidence."""
import argparse
import hashlib
import json
import re
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
from analyze_get_progress import summarize

SOURCES = [
    "src/kernel/nv2a_pb_scan.c", "src/kernel/nv2a_pb_scan.h",
    "src/kernel/nv2a_order_trace.c", "src/kernel/nv2a_order_trace.h",
    "src/kernel/xbox_memory_layout.c", "src/kernel/xbox_memory_layout.h",
    "src/kernel/nv2a_pb_exec.c", "src/kernel/kernel_bridge.c", "src/kernel/kernel_hal.c",
    "src/kernel/nv2a_hw_raster.c", "tests/nv2a_pb_control/get_progress_test.c",
    "tests/nv2a_pb_control/control_test.c", "tests/nv2a_pb_control/CMakeLists.txt",
    "games/brute_force_e3/src/recomp_get_trace.c",
    "games/brute_force_e3/src/recomp/gen/recomp_0104.c",
    "games/brute_force_e3/install_get_hooks.py", "games/brute_force_e3/audit_get_protocol.py",
    "games/brute_force_e3/analyze_get_progress.py", "games/brute_force_e3/capture_order_trace.py",
    "games/brute_force_e3/inspect_guest.py", "games/brute_force_e3/verify_get_progress.ps1",
    "games/brute_force_e3/performance.ps1", "games/brute_force_e3/probe.py",
    "games/brute_force_e3/test_get_progress.py", "games/brute_force_e3/package_get_review.py",
    "games/brute_force_e3/review/opus55/GET_PROGRESS.md",
    "games/brute_force_e3/review/opus55/IRQ_GET_RESPONSE.md",
    "games/brute_force_e3/PERFORMANCE_NEXT.md",
]

def package(name):
    if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
        raise ValueError("Use a simple workflow name")
    project = Path(__file__).resolve().parent
    root = project.parents[1]
    analysis = project / "analysis"
    state = json.loads((analysis / f"{name}-get-verification.json").read_text())
    if len(state["completed"]) != state["pairs"] * 2 or state["status"] == "running":
        raise ValueError("The short-route series is incomplete")
    build = json.loads((analysis / "build-verification-perf151.json").read_text())
    tool_updates = {}
    for path, expected in build["source"].items():
        actual = hashlib.sha256((root / path).read_bytes()).hexdigest()
        if actual != expected:
            if Path(path).suffix == ".py":
                # The generated C must still match its build hash below.
                tool_updates[path] = dict(at_build=expected, packaged=actual)
            else:
                raise ValueError(f"Runtime source changed since the binary was built: {path}")
    runs = [summarize(analysis / run) for run in state["completed"]]
    for run in runs:
        (analysis / f'{run["name"]}-get.json').write_text(json.dumps(run, indent=2) + "\n")
    result = dict(workflow=name, pairs=state["pairs"], build_tool_updates=tool_updates,
                  automated_checks_passed=all(r["automated_checks_passed"] for r in runs),
                  status="short_route_automated_checks_only_full_gates_pending", runs=runs)
    summary = analysis / f"{name}-summary.json"
    summary.write_text(json.dumps(result, indent=2) + "\n")
    archive = analysis / f"claude-{name}-evidence.zip"
    files = {root / f: "source/" + f for f in SOURCES}
    for run in state["completed"]:
        for path in analysis.glob(run + "*"):
            if path.suffix in (".json", ".csv", ".log"):
                files[path] = "analysis/" + path.name
        captures = sorted(analysis.glob(run + "-frame-*.bmp"))
        if not captures:
            raise ValueError(f"No captures for {run}")
        for index in (0,len(captures)//4,len(captures)//2,3*len(captures)//4,len(captures)-1):
            path=captures[index]
            files[path] = "analysis/" + path.name
    for path in [summary, analysis / f"{name}-get-verification.json",
                 analysis / "build-verification-perf151.json",
                 analysis / "get-protocol-audit.json",
                 analysis / "get-publish-native-disassembly.txt",
                 analysis / "get-protocol-original-disassembly.txt"]:
        files[path] = "analysis/" + path.name
    manifest = {arc: hashlib.sha256(path.read_bytes()).hexdigest() for path, arc in files.items()}
    with ZipFile(archive, "x", compression=ZIP_DEFLATED, compresslevel=6) as z:
        for path, arc in files.items():
            z.write(path, arc)
        z.writestr("manifest.json", json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(dict(path=str(archive), bytes=archive.stat().st_size,
                         sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),
                         files=len(files), automated_checks_passed=result["automated_checks_passed"]), indent=2))

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("name", nargs="?", default="perf151_get_title_qf")
    package(p.parse_args().name)
