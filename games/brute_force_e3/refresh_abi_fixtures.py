"""Extract the existing stack/ABI regression bodies from current generated C."""
from pathlib import Path
import re

PROJECT = Path(__file__).resolve().parent
ENTRIES = {
    "repair62fd0.c": "00062FD0",
    "repair113570.c": "00113570",
    "repair418e0.c": "000418E0",
    "repair00041D90.c": "00041D90",
    "repair00100B60.c": "00100B60",
    "repair00105810.c": "00105810",
    "repair001530D0.c": "001530D0",
    "repair00186390.c": "00186390",
    "repair157d40.c": "00157D40",
}


def main():
    sources = [p.read_text(encoding="utf-8")
               for p in (PROJECT / "src/recomp/gen").glob("recomp_*.c")]
    prepared = {}
    for name, address in ENTRIES.items():
        pattern = rf"^void sub_{address}\(void\)\n\{{.*?^\}}\n"
        matches = [m.group() for source in sources
                   for m in re.finditer(pattern, source, re.M | re.S)]
        if len(matches) != 1:
            raise ValueError(f"Expected one generated body for {address}: {len(matches)}")
        prepared[name] = '#include "recomp_funcs.h"\n#include <math.h>\n' + matches[0]
    for name, source in prepared.items():
        (PROJECT / "analysis" / name).write_text(source, encoding="utf-8")
    print(f"Refreshed {len(prepared)} ABI fixture bodies from current generated C")


if __name__ == "__main__":
    main()
