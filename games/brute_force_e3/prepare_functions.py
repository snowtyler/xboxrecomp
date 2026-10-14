"""Apply verified prototype entry corrections before CFG coalescence."""
import json
from pathlib import Path
import sys

p = Path(__file__).resolve().parent
sys.path.insert(0, str(p.parents[1]))
from tools.disasm.loader import load_image
image = load_image(str(p / "input/default.xbe"), str(p / "analysis/xbe_analysis.json"))
functions = json.loads((p / "analysis/disasm/functions.json").read_text())
repairs = json.loads((p / "coalesce_functions.json").read_text())
by_start = {f["start"]: f for f in functions}
for repair in repairs:
    # These entries are labels within recovered visitor methods. Their entries
    # save registers, and all paths share their restoration. Runtime ABI tracing
    # proves the truncated bodies cannot be independent callable functions.
    # Keep the raw disassembly untouched; correct the translation input only.
    for start in repair["coalesce_starts"]:
        f = by_start[start]
        if f.get("called_by") or f.get("has_prologue"):
            raise ValueError(f"Unexpected independent entry evidence at {start}")
        if f["detection_method"] not in {"tail_jump_alias", "imm_ref_target", "gap_prologue"}:
            raise ValueError(f"Unexpected entry classification at {start}")
        f["original_detection_method"] = f["detection_method"]
        f["detection_method"] = "verified_local_branch"
        end, old_end = int(repair["end"], 16), int(f["end"], 16)
        if old_end > end:
            padding = image.read_bytes_at_va(end, old_end - end)
            if not padding or any(byte not in (0x90, 0xCC) for byte in padding):
                raise ValueError(f"Non-padding overrun at {start}")
            f["end"] = repair["end"]
            f["size"] = end - int(start, 16)
(p / "analysis/recomp_functions.json").write_text(json.dumps(functions, indent=2))
print("Prepared prototype function metadata with verified local branch labels")
