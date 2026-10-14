"""Report missing targets in dense prototype code-pointer tables for review."""
import json
import argparse
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.disasm.loader import load_image, DATA_SECTION_NAMES
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

p = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--write-seeds", action="store_true")
args = parser.parse_args()
image = load_image(str(p / "input/default.xbe"), str(p / "analysis/xbe_analysis.json"))
known = {int(f["start"], 16) for f in json.loads((p / "analysis/disasm/functions.json").read_text())}
code = [s for s in image.sections if s.executable and s.name not in DATA_SECTION_NAMES
        and not s.name.startswith("$$") and s.name != ".XTLID"]
def is_code(va):
    return any(s.virtual_addr <= va < s.virtual_addr + s.raw_size for s in code)

decoder = Cs(CS_ARCH_X86, CS_MODE_32)
found = {}
for section in image.sections:
    if section.name not in DATA_SECTION_NAMES:
        continue
    data = image.get_section_data(section)
    run = []
    def flush():
        if len(run) < 8:
            return
        # Switch tables point into function interiors. C++ vtables in this
        # prototype instead contain known entries or 16-byte aligned starts.
        if any(target not in known and target % 16 for _, target in run):
            return
        for slot, target in run:
            if target in known or target in found:
                continue
            if target % 16:
                continue
            insns = list(decoder.disasm(image.read_bytes_at_va(target, 2048) or b"", target))
            if not insns:
                continue
            if insns[0].mnemonic not in {"sub", "push", "mov"}:
                continue
            # Only report; these are evidence for review, not observed calls.
            found[target] = {"start": f"0x{target:08X}",
                "table_slot": f"0x{slot:08X}", "table_length": len(run),
                "first_instructions": [f"{i.mnemonic} {i.op_str}" for i in insns[:4]]}
    for off in range(0, len(data) - 3, 4):
        target = struct.unpack_from("<I", data, off)[0]
        if is_code(target):
            run.append((section.virtual_addr + off, target))
        else:
            flush()
            run = []
    flush()
report = sorted(found.values(), key=lambda x: x["start"])
(p / "analysis/table_candidates.json").write_text(json.dumps(report, indent=2))
print(f"{len(report)} missing targets in tables of at least eight code pointers")
print(json.dumps(report[:12], indent=2))
if args.write_seeds:
    seeds = [{"start": r["start"], "reason":
              f"Aligned prototype entry in code-pointer table at {r['table_slot']}; standard function entry pattern"}
             for r in report]
    (p / "table_functions.json").write_text(json.dumps(seeds, indent=2) + "\n")
