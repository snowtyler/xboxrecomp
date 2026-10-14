"""Add opt-in diagnostics to the prototype SDK's command writers."""
from pathlib import Path

PROJECT = Path(__file__).resolve().parent
path = PROJECT / "src/recomp/gen/recomp_0104.c"
text = path.read_text()
include = '#include "../sdk_writer_trace.inc"'
if include not in text:
    text = text.replace('#include <math.h>', '#include <math.h>\n' + include, 1)
    start = text.index('void sub_003988D0(void)')
    end = text.index('void sub_00398BA0(void)', start)
    body = text[start:end]
    for pc, register in [('0039896B', 'ecx'), ('003989E0', 'eax'), ('00398B27', 'ecx')]:
        at = body.index('loc_' + pc + ': ;')
        store = '    MEM32(ebx) = ' + register + ';'
        where = body.index(store, at) + len(store)
        body = body[:where] + '\n    sdk_writer_trace(0x' + pc + ',ebx,' + register + ',MEM32(ebp+8));' + body[where:]
    body = body.replace('    { uint64_t _n = (uint64_t)ecx * 4u;',
                        '    sdk_writer_trace(0x003988D0,edi,ecx,esi);\n    { uint64_t _n = (uint64_t)ecx * 4u;')
    body = body.replace('    mm1 = MMX_MEM(esi + 4);',
                        '    sdk_writer_trace(0x00398A03,edi,MEM32(ebx),ebx);\n    mm1 = MMX_MEM(esi + 4);', 1)
    body = body.replace('loc_00398AA1: ;',
                        'loc_00398AA1: ;\n    sdk_writer_trace(0x00398AA1,ebx,MEM32(ebx),edi);', 1)
    text = text[:start] + body + text[end:]
    text = text.replace('loc_0039E84B: ;',
                        'loc_0039E84B: ;\n    sdk_writer_trace(0x0039E84B,eax,0xFC,MEM32(esp+16));', 1)
    text = text.replace('loc_0039C3F5: ;\n    ecx = MEM32(ecx + 4);',
                        'loc_0039C3F5: ;\n    ecx = MEM32(ecx + 4);\n    sdk_writer_trace(0x0039C3F5,eax,edi,ecx);', 1)
    text = text.replace('loc_0039C400: ;',
                        'loc_0039C400: ;\n    sdk_writer_trace(0x0039C400,eax,edi,MEM32(eax));', 1)
    text = text.replace('    ecx = MEM32(ebx + 4);\n    MEM32(esp + 0x28) = ecx;',
                        '    ecx = MEM32(ebx + 4);\n    if(!ecx) sdk_rt_zero_trace(ebx,esi,eax,MEM32(esp+0x4C));\n    MEM32(esp + 0x28) = ecx;', 1)
    path.write_text(text)
