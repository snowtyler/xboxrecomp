"""Prove bounded internal epilogue repairs against the prototype binary."""
import copy
import json
from pathlib import Path
import sys

p = Path(__file__).resolve().parent
sys.path.insert(0, str(p.parents[1]))
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from tools.disasm.loader import load_image
from tools.recomp import config
from tools.recomp.translator import FunctionTranslator

image = load_image(str(p / 'input/default.xbe'), str(p / 'analysis/xbe_analysis.json'))
raw = json.loads((p / 'analysis/disasm/functions.json').read_text())
functions = sorted(raw, key=lambda f: int(f['start'], 16))
required = json.loads((p / 'coalesce_functions.json').read_text())
new_required = {'start': '0x00222310', 'end': '0x00222401',
                'coalesce_starts': ['0x002223F1']}
if not any(r['start'] == new_required['start'] for r in required):
    required.append(new_required)
required_starts = {r['start'] for r in required}
cs = Cs(CS_ARCH_X86, CS_MODE_32)
candidates = []
for k, gap in enumerate(functions):
    if (not k or gap['detection_method'] != 'gap_prologue'
            or gap.get('called_by') or gap.get('has_prologue') or gap['size'] > 64):
        continue
    owner = functions[k - 1]
    if owner['detection_method'] != 'tail_jump_alias' or owner['start'] in required_starts:
        continue
    start, end, gap_end = [int(s, 16) for s in (owner['start'], owner['end'], gap['end'])]
    if end != int(gap['start'], 16) or end - start > 1024:
        continue
    body = image.read_bytes_at_va(start, gap_end - start)
    if not body:
        continue
    ins = list(cs.disasm(body, start))
    if not ins or ins[-1].address + ins[-1].size != gap_end:
        continue
    edges = [x for x in ins if x.address < end and x.mnemonic.startswith('j')
             and x.op_str.startswith('0x') and end <= int(x.op_str, 16) < gap_end]
    returns = [x for x in ins if x.mnemonic == 'ret' and x.address >= end]
    if not edges or not returns:
        continue
    recovered_end = returns[-1].address + returns[-1].size
    if any(x.mnemonic not in ('nop', 'int3') for x in ins if x.address >= recovered_end):
        continue
    candidates.append({'start': owner['start'], 'end': f'0x{recovered_end:08X}',
                       'coalesce_starts': [gap['start']]})

db = {}
for original in raw:
    f = copy.deepcopy(original)
    address = int(f['start'], 16)
    f['_addr'] = address
    f['end'] = int(f['end'], 16)
    db[address] = f
for repair in required + candidates:
    for address in repair['coalesce_starts']:
        f = db[int(address, 16)]
        if f.get('called_by') or f.get('has_prologue'):
            raise ValueError(f'Independent entry at {address}')
        f['detection_method'] = 'verified_local_branch'
        end = int(repair['end'], 16)
        if f['end'] > end:
            padding = image.read_bytes_at_va(end, f['end'] - end)
            if not padding or any(b not in (0x90, 0xCC) for b in padding):
                raise ValueError(f'Non-padding overrun at {address}')
            f['end'] = end
            f['size'] = end - int(address, 16)

config.configure_from_xbe(str(p / 'input/default.xbe'))
t = FunctionTranslator(image.raw_data, db, seh_prolog=0, seh_epilog=0,
                       setjmp_fn=0, longjmp_fn=0)
t.discover_static_indirect_targets(coalescing=True)
accepted, rejected = [], []
for repair in required + candidates:
    start, end = int(repair['start'], 16), int(repair['end'], 16)
    interior = [int(a, 16) for a in repair['coalesce_starts']]
    before = {a: copy.deepcopy(t.func_db[a]) for a in [start, *interior]}
    old_cfg = {a: t._recovered_cfg.get(a) for a in before}
    old_sets = [set(s) for s in (t.owned_function_starts, t.recovered_function_starts,
                                t.coalesced_function_starts)]
    try:
        t.coalesce_function(start, end, interior)
        t.discover_static_indirect_targets(coalescing=True)
    except ValueError as error:
        t.func_db.update(before)
        for a, cfg in old_cfg.items():
            if cfg is None:
                t._recovered_cfg.pop(a, None)
            else:
                t._recovered_cfg[a] = cfg
        (t.owned_function_starts, t.recovered_function_starts,
         t.coalesced_function_starts) = old_sets
        if repair in required:
            raise
        rejected.append({'repair': repair, 'reason': str(error)})
    else:
        accepted.append(repair)

(p / 'analysis/epilogue_audit.json').write_text(json.dumps(
    {'accepted': accepted, 'rejected': rejected}, indent=2) + '\n')
(p / 'coalesce_functions.json').write_text(json.dumps(accepted, indent=2) + '\n')
print(f'Proved {len(accepted)} repairs; rejected {len(rejected)} candidates')
