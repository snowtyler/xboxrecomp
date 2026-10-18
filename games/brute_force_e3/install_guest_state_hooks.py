"""Install exact, idempotent guest replay observations; never patch guest state."""
from pathlib import Path
import argparse
import re

HOOKS = {
    "000845F0": {"loc_0008468F: ;":
        "    {extern void brute_force_guest_route(uint32_t,uint32_t,uint32_t);"
        "brute_force_guest_route(0x0008468F,esp+0x14,esi);}"},
    "00111D00": {"loc_00111D43: ;":
        "    {extern void brute_force_guest_death(uint32_t);brute_force_guest_death(eax);}"},
    "0010B480": {"loc_0010B524: ;":
        "    {extern void brute_force_guest_route(uint32_t,uint32_t,uint32_t);"
        "brute_force_guest_route(0x0010B524,eax,ecx);}"},
    "0008A350": {
        "    ebp = MEM32(ecx + eax * 4);":
        "    {extern void brute_force_guest_route(uint32_t,uint32_t,uint32_t);"
        "brute_force_guest_route(0x0008A38E,ebp,esi);}",
        "loc_0008A4CF: ;":
        "    {extern void brute_force_guest_route(uint32_t,uint32_t,uint32_t);"
        "brute_force_guest_route(0x0008A4CF,ebp,esi);}"},
}


def install(root):
    changes = {}
    # Validate every target before writing anything.
    for address, hooks in HOOKS.items():
        pattern = re.compile(rf"void sub_{address}\(void\)\s*\{{.*?\n\}}", re.S)
        found = [(path, match) for path in root.glob("recomp_*.c")
                 for match in pattern.finditer(path.read_text(encoding="utf-8"))]
        if len(found) != 1:
            raise ValueError(f"Expected exactly one guest procedure {address}")
        path, match = found[0]
        text = changes.get(path, path.read_text(encoding="utf-8"))
        match = pattern.search(text)
        body = match.group()
        for anchor, hook in hooks.items():
            if body.count(anchor) != 1:
                raise ValueError(f"Expected exactly one {anchor} in {address}")
            if anchor + "\n" + hook not in body:
                body = body.replace(anchor, anchor + "\n" + hook)
        changes[path] = text[:match.start()] + body + text[match.end():]
    for path, text in changes.items():
        if path.read_text(encoding="utf-8") != text:
            path.write_text(text, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, nargs="?", default=Path(__file__).parent / "src/recomp/gen")
    install(parser.parse_args().root)
