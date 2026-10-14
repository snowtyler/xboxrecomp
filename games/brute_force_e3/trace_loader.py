"""Instrument selected tutorial-loader calls without altering guest state."""
from pathlib import Path
import re

addresses = {0x26FD30, 0x26FDA0, 0x26FDC0, 0x26FF30, 0x26FF60,
             0x270000, 0x2707C0, 0x270800, 0x270A50, 0x26F5F0}
for p in (Path(__file__).resolve().parent / 'src/recomp/gen').glob('recomp_*.c'):
    s = p.read_text()
    changed = False
    for address in addresses:
        pattern = f'void sub_{address:08X}(void)\n{{\n'
        replacement = pattern + f'    recomp_loader_trace(0x{address:08X});\n'
        if pattern in s and replacement not in s:
            s = s.replace(pattern, replacement)
            changed = True
    if changed:
        if 'extern void recomp_loader_trace' not in s:
            s = 'extern void recomp_loader_trace(unsigned int address);\n' + s
        p.write_text(s)
