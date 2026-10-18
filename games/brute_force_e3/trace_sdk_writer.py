"""Add opt-in diagnostics to the prototype SDK's command writers."""
from pathlib import Path

PROJECT = Path(__file__).resolve().parent
sources = {p: p.read_text(encoding="utf-8") for p in (PROJECT / "src/recomp/gen").glob("recomp_*.c")}
include = '#include "../../sdk_writer_trace.h"'

def owner(anchor):
    matches = [p for p, text in sources.items() if anchor in text]
    if len(matches) != 1:
        raise ValueError(f"Expected one generated owner for {anchor}: {matches}")
    return matches[0]

def insert(anchor, hook):
    path = owner(anchor)
    if hook not in sources[path]:
        sources[path] = sources[path].replace(anchor, anchor + "\n" + hook, 1)

path = owner('void sub_003988D0(void)')
text = sources[path]
start = text.index('void sub_003988D0(void)')
end = text.index('void sub_00398BA0(void)', start)
body = text[start:end]
for pc, register in [('0039896B', 'ecx'), ('003989E0', 'eax'), ('00398B27', 'ecx')]:
    hook = '    sdk_writer_trace(0x' + pc + ',ebx,' + register + ',MEM32(ebp+8));'
    if hook not in body:
        at = body.index('loc_' + pc + ': ;')
        store = '    MEM32(ebx) = ' + register + ';'
        where = body.index(store, at) + len(store)
        body = body[:where] + '\n' + hook + body[where:]
for anchor, hook in [
    ('    { uint64_t _n = (uint64_t)ecx * 4u;', '    sdk_writer_trace(0x003988D0,edi,ecx,esi);'),
    ('    mm1 = MMX_MEM(esi + 4);', '    sdk_writer_trace(0x00398A03,edi,MEM32(ebx),ebx);'),
]:
    if hook not in body:
        if anchor not in body: raise ValueError(f"Missing SDK writer anchor: {anchor}")
        body = body.replace(anchor, hook + '\n' + anchor, 1)
hook = '    sdk_writer_trace(0x00398AA1,ebx,MEM32(ebx),edi);'
if hook not in body: body = body.replace('loc_00398AA1: ;', 'loc_00398AA1: ;\n' + hook, 1)
sources[path] = text[:start] + body + text[end:]
insert('loc_0039E84B: ;', '    sdk_writer_trace(0x0039E84B,eax,0xFC,MEM32(esp+16));')
insert('loc_0039C3F5: ;\n    ecx = MEM32(ecx + 4);', '    sdk_writer_trace(0x0039C3F5,eax,edi,ecx);')
insert('loc_0039C400: ;', '    sdk_writer_trace(0x0039C400,eax,edi,MEM32(eax));')
insert('    ecx = MEM32(ebx + 4);\n    MEM32(esp + 0x28) = ecx;', '    if(!ecx) sdk_rt_zero_trace(ebx,esi,eax,MEM32(esp+0x4C));')
for path, text in sources.items():
    text = text.replace('#include "../sdk_writer_trace.inc"', include)
    if text != path.read_text(encoding="utf-8"):
        if include not in text: text = text.replace('#include <math.h>', '#include <math.h>\n' + include, 1)
        path.write_text(text, encoding="utf-8")
