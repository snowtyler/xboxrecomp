"""Add opt-in first-use diagnostics to generated unresolved direct calls."""
from pathlib import Path
import re

p = Path(__file__).resolve().parent / 'src/recomp/gen/recomp_stubs_unresolved.c'
s = p.read_text()
if 'trace_missing_direct' not in s:
    helper = '''
#include <stdio.h>
#include <stdlib.h>
static void trace_missing_direct(unsigned int address, int *seen)
{
    if (*seen) return;
    *seen = 1;
    if (!getenv("RECOMP_STUB_TRACE")) return;
    fprintf(stderr, "[DIRECT STUB] %08X caller %08X eax %08X ecx %08X esp %08X\\n",
            address, MEM32(g_esp), g_eax, g_ecx, g_esp);
}
'''
    s = s.replace('#include "recomp_funcs.h"', '#include "recomp_funcs.h"\n' + helper)
    s = re.sub(r'(void sub_([0-9A-F]{8})\(void\) \{)',
               lambda m: m[1] + f' static int seen; trace_missing_direct(0x{m[2]}, &seen);', s)
    p.write_text(s)
    print('Enabled optional unresolved direct-call tracing')
