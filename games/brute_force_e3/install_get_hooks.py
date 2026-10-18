"""Install idempotent read-only wait hooks in this title's generated C.

The exact generated labels must exist once. Regeneration is supported; an
unrecognised generator output fails rather than silently losing observations.
"""
from pathlib import Path
import argparse

HOOKS = {
    "    MEM32(esi + 0x1970) = eax;": (
        "    {extern void xbox_DebugEventTrace(uint32_t,uint32_t);"
        "xbox_DebugEventTrace(esi+0x196C,0);}"),
    "loc_0039EC10: ;": (
        "    {extern void brute_force_get_wait_begin(uint32_t,uint32_t,uint32_t);"
        "brute_force_get_wait_begin(MEM32(esp+4),MEM32(esp+8),esp);}"),
    "loc_0039ECBC: ;": (
        "    {extern void brute_force_get_wait_check(uint32_t,uint32_t,uint32_t,uint32_t);"
        "brute_force_get_wait_check(edi,esi,eax,edx);}"),
    "loc_0039ECDD: ;": (
        "    {extern void brute_force_get_wait_poll(void);brute_force_get_wait_poll();}"),
    "loc_0039ED20: ;": (
        "    {extern void brute_force_get_wait_event(void);brute_force_get_wait_event();}"),
    "loc_0039ED0E: ;": (
        "    {extern void brute_force_get_wait_end(void);brute_force_get_wait_end();}"),
    "loc_0039ED2F: ;": (
        "    {extern void brute_force_get_wait_end(void);brute_force_get_wait_end();}"),
}

def install(path: Path):
    text = path.read_text(encoding="utf-8")
    for label, hook in HOOKS.items():
        if text.count(label) != 1:
            raise ValueError(f"Expected exactly one {label} in {path}")
        if label + "\n" + hook not in text:
            text = text.replace(label, label + "\n" + hook)
        if label + "\n" + hook not in text:
            raise ValueError(f"Unexpected hook placement for {label}")
    if text != path.read_text(encoding="utf-8"):
        path.write_text(text, encoding="utf-8", newline="\n")

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("path", nargs="?", type=Path,
                   default=Path(__file__).parent / "src/recomp/gen/recomp_0104.c")
    install(p.parse_args().path)
