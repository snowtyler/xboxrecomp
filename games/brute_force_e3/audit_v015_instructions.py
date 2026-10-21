"""Count v0.15 instruction leads in the retained disassembly and generated C.

Disassembly counts include data decoded as instructions. Function-range counts
filter to the existing recompilation metadata; neither proves runtime execution.
"""
import argparse
from bisect import bisect_right
from collections import Counter, defaultdict
import json
from pathlib import Path
import re

PROJECT = Path(__file__).resolve().parent
PATTERNS = {
    "pushfd": r"^pushfd$", "popfd": r"^popfd$", "cpuid": r"^cpuid$",
    "movsx_bp_sp": r"^movsx .*, (?:bp|sp)$",
    "fsin": r"^fsin$", "fcos": r"^fcos$", "fsincos": r"^fsincos$",
    "fptan": r"^fptan$", "xadd": r"^xadd .*", "lock_xadd": r"^lock xadd .*", "lahf": r"^lahf$",
    "pop_esp": r"^pop esp$",
}


def audit(decoded=False):
    functions = json.loads((PROJECT / "analysis/recomp_functions.json").read_text())
    ranges = sorted((int(f["start"], 16), int(f["end"], 16)) for f in functions)
    merged = []
    for start, end in ranges:
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    starts = [r[0] for r in merged]
    raw, covered, addresses = Counter(), Counter(), defaultdict(list)
    for file in (PROJECT / "analysis/disasm/asm").glob("*.asm"):
        for line in file.read_text().splitlines():
            match = re.match(r"\s*0x([0-9A-Fa-f]+)\s+[0-9A-Fa-f]+\s+(.+?)\s*$", line)
            if not match:
                continue
            address = int(match[1], 16)
            instruction = " ".join(match[2].split()).split(" ;")[0]
            index = bisect_right(starts, address) - 1
            inside = index >= 0 and address < merged[index][1]
            for name, pattern in PATTERNS.items():
                if re.match(pattern, instruction):
                    raw[name] += 1
                    if inside:
                        covered[name] += 1
                        addresses[name].append(f"0x{address:08X}")
    generated = Counter()
    for file in (PROJECT / "src/recomp/gen").glob("*.c"):
        text = file.read_text()
        generated["pop_esp_macro"] += len(re.findall(r"POP32\(esp, esp\)", text))
        for name in PATTERNS:
            generated[name + "_unimplemented"] += len(re.findall(
                r'RECOMP_UNIMPL\("' + re.escape(name) + r'"', text))
    result = dict(raw_disassembly={n: raw[n] for n in PATTERNS},
                  existing_function_ranges={n: covered[n] for n in PATTERNS},
                  addresses=dict(addresses), generated=dict(generated))
    if decoded:
        from tools.recomp import config
        from tools.recomp.translator import BatchTranslator
        xbe = PROJECT / "input/default.xbe"
        config.configure_from_xbe(str(xbe))
        manual = json.loads((PROJECT / "manual_functions.json").read_text())
        protected = {int(a, 16) for a in manual}
        batch = BatchTranslator(str(xbe), str(PROJECT / "analysis/recomp_functions.json"),
            labels_json_path=str(PROJECT / "analysis/disasm/labels.json"),
            identified_json_path=str(PROJECT / "analysis/func_id/identified_functions.json"),
            abi_json_path=str(PROJECT / "analysis/abi/abi_functions.json"),
            coalesce_json_paths=[str(PROJECT / "coalesce_functions.json")],
            protected_function_starts=protected)
        dispatch = (PROJECT / "src/recomp/gen/recomp_dispatch.c").read_text()
        entries = {int(a, 16) for a in re.findall(r"\{\s*0x([0-9A-Fa-f]+)u?,", dispatch)}
        found, last_cf_lahf = {}, []
        for start in sorted(entries - protected):
            info = batch.func_db.get(start)
            if not info:
                continue
            recovered = batch.translator._recovered_cfg.get(start)
            end = recovered["end"] if recovered else info["end"]
            instructions, blocks = batch.translator.decode_function(start, end)
            for insn in instructions:
                found[insn.address] = insn
            for block in blocks.values() if isinstance(blocks, dict) else blocks:
                previous = None
                for insn in block.instructions:
                    if insn.mnemonic == "lahf" and previous and previous.mnemonic in (
                            "add", "sub", "adc", "sbb", "cmp", "stc", "clc", "cmc",
                            "inc", "dec", "shl", "shr", "sar", "neg", "and", "or", "xor"):
                        last_cf_lahf.append(f"0x{insn.address:08X}")
                    previous = insn
        counts, sites = Counter(), defaultdict(list)
        for address, insn in sorted(found.items()):
            text = " ".join((insn.mnemonic + " " + insn.op_str).split())
            for name, pattern in PATTERNS.items():
                if re.match(pattern, text):
                    counts[name] += 1; sites[name].append(f"0x{address:08X}")
        result["decoded_dispatch_functions"] = {n: counts[n] for n in PATTERNS}
        result["decoded_sites"] = dict(sites)
        result["lahf_immediately_after_carry_setter"] = sorted(set(last_cf_lahf))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--decoded", action="store_true")
    args = parser.parse_args()
    result = audit(args.decoded)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2))
    print(json.dumps({k: v for k, v in result.items() if k not in ("addresses", "decoded_sites")}, indent=2))
