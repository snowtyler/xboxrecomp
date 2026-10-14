"""Regenerate the E3 prototype in its own analysis and code directories."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys

PROJECT = Path(__file__).resolve().parent
ROOT = PROJECT.parent.parent
EXPECTED_SHA256 = "bcf4fbfb1adc595a3d04fe1443f22fcc1ec61bee4909f2780783d6ba0a13c4de"


def run(module, *args):
    subprocess.run([sys.executable, "-m", module, *map(str, args)], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game_dir", type=Path, help="Extracted E3 prototype directory")
    args = parser.parse_args()
    source = args.game_dir / "default.xbe"
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    if digest != EXPECTED_SHA256:
        parser.error("default.xbe differs from the analyzed E3 prototype; use a separate project for other builds")
    xbe = PROJECT / "input/default.xbe"
    xbe.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, xbe)
    analysis = PROJECT / "analysis"
    analysis.mkdir(exist_ok=True)
    disasm, func_id, abi = (analysis / name for name in ("disasm", "func_id", "abi"))
    run("tools.xbe_parser", xbe, "--json", analysis / "xbe_analysis.json")
    seeds = ["--seed-functions", PROJECT / "seed_functions.json"]
    if (PROJECT / "table_functions.json").exists():
        seeds += ["--seed-functions", PROJECT / "table_functions.json"]
    run("tools.disasm", xbe, "--analysis-json", analysis / "xbe_analysis.json", "--output", disasm, *seeds)
    run("tools.func_id", xbe, "--functions", disasm / "functions.json", "--strings", disasm / "strings.json", "--xrefs", disasm / "xrefs.json", "--output", func_id)
    run("tools.abi_analysis", xbe, "--disasm-dir", disasm, "--func-id-dir", func_id, "--output-dir", abi)
    coalescence = []
    if (PROJECT / "coalesce_functions.json").exists():
        subprocess.run([sys.executable, str(PROJECT / "prepare_functions.py")], check=True)
        coalescence = ["--coalesce-functions", PROJECT / "coalesce_functions.json"]
        coalescence += ["--functions", analysis / "recomp_functions.json"]
    run("tools.recomp", xbe, "--game-name", "Brute Force E3 Prototype", "--all", "--split", 250,
        "--gen-dir", PROJECT / "src/recomp/gen", "--disasm-dir", disasm, "--func-id-dir", func_id,
        "--abi-dir", abi, "--output-dir", analysis / "recomp",
        "--manual-functions", PROJECT / "manual_functions.json", *coalescence)
    subprocess.run([sys.executable, str(PROJECT / "trace_stubs.py")], check=True)
    subprocess.run([sys.executable, str(PROJECT / "trace_loader.py")], check=True)
    subprocess.run([sys.executable, str(PROJECT / "trace_sdk_writer.py")], check=True)
    subprocess.run([sys.executable, str(PROJECT / "trace_audio_queue.py")], check=True)


if __name__ == "__main__":
    main()
