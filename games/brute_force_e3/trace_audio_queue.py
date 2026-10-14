"""Install an opt-in diagnostic at the original DirectSound SG queue update."""
from pathlib import Path
project = Path(__file__).resolve().parent
gen = project / "src/recomp/gen"
marker = "void sub_00343C61(void)\n{"
for path in gen.glob("recomp_*.c"):
    source = path.read_text()
    if marker not in source:
        continue
    include = '#include "../audio_queue_trace.inc"\n'
    source = source.replace(include, "")
    source = source.replace('#include "recomp_funcs.h"\n',
                            '#include "recomp_funcs.h"\n' + include, 1)
    hook = "    audio_queue_trace(ecx);\n"
    if marker + "\n" + hook not in source:
        source = source.replace(marker, marker + "\n" + hook, 1)
    path.write_text(source)
    print("Installed opt-in audio SG queue trace in", path.name)
    break
else:
    raise RuntimeError("DirectSound queue updater not found")
