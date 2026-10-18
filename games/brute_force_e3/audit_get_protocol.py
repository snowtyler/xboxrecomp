"""Save reproducible original-XBE disassembly for the GET experiment.

The reference scan covers decoded function bodies, not arbitrary indirect
aliases. Candidate readers must still be classified by their control flow.
"""
import hashlib
import json
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_MEM, CS_OP_IMM
from tools.disasm.loader import load_image

def audit():
    root = Path(__file__).resolve().parent
    image = load_image(str(root / "input/default.xbe"), str(root / "analysis/xbe_analysis.json"))
    functions = json.loads((root / "analysis/disasm/functions.json").read_text())
    cs = Cs(CS_ARCH_X86, CS_MODE_32); cs.detail = True
    references, selected, emitted = [], {}, set()
    for f in functions:
        lo, hi = int(f["start"], 16), int(f["end"], 16)
        body = image.read_bytes_at_va(lo, hi - lo)
        if not body:
            continue
        instructions = list(cs.disasm(body, lo))
        offsets = {0x17C0,0x3244,0x324C,0x800044,0xFD800044,0xFD003244,0xFD00324C}
        absolute = {0xFD800044,0xFD003244,0xFD00324C}
        hits = [i for i in instructions if any(
            (o.type == CS_OP_MEM and (o.mem.disp&0xffffffff) in offsets) or
            (o.type == CS_OP_IMM and (o.imm&0xffffffff) in absolute) for o in i.operands)]
        nop_emitter = any(i.mnemonic == "mov" and len(i.operands) == 2 and
                          i.operands[1].type == CS_OP_IMM and
                          i.operands[1].imm == 0x40100 for i in instructions)
        if hits or nop_emitter or lo in (0x39EAF0, 0x39F140) or lo <= 0x39C333 < hi:
            selected[(lo, hi)] = instructions
        for i in hits:
            if i.address in emitted:
                continue
            emitted.add(i.address)
            references.append(dict(function=f["start"], address=f"{i.address:08X}",
                                   instruction=f"{i.mnemonic} {i.op_str}"))
    metadata = dict(xbe_sha256=hashlib.sha256((root / "input/default.xbe").read_bytes()).hexdigest(),
                    scope="decoded function bodies; indirect aliases require separate analysis",
                    references=references)
    (root / "analysis/get-protocol-audit.json").write_text(json.dumps(metadata, indent=2) + "\n")
    text = []
    for (lo, hi), instructions in sorted(selected.items()):
        text.append(f"RANGE {lo:08X}-{hi:08X}")
        text.extend(f"{i.address:08X}  {i.bytes.hex():20} {i.mnemonic} {i.op_str}" for i in instructions)
        text.append("")
    (root / "analysis/get-protocol-original-disassembly.txt").write_text("\n".join(text))
    print(f"Saved {len(references)} unique references and {len(selected)} function bodies")

if __name__ == "__main__":
    audit()
