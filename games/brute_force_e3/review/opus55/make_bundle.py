"""Export a bounded review snapshot; never include game assets/build outputs."""
from pathlib import Path
import difflib
import hashlib
import json
import zipfile

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
PROJECT = ROOT / 'games/brute_force_e3'
ANALYSIS = PROJECT / 'analysis'
files = set()

def add(path):
    path = Path(path)
    if not path.is_file():
        raise FileNotFoundError(path)
    files.add(path)

for folder in ('src/kernel', 'src/video', 'src/apu'):
    for path in (ROOT / folder).rglob('*'):
        if path.suffix in ('.c', '.h') or path.name == 'CMakeLists.txt':
            add(path)
for name in ('nv2a_core.c', 'nv2a_state.h', 'nv2a_regs.h', 'qemu_shim.h', 'nv2a_mmio_hook.c', 'nv2a_mmio_hook.h'):
    add(ROOT / 'src/nv2a' / name)
for path in (ROOT / 'templates/runtime').glob('*.h'):
    add(path)
if (ROOT / 'include').exists():
    for path in (ROOT / 'include').rglob('*.h'):
        add(path)
add(ROOT / 'CMakeLists.txt')
for folder in ('nv2a_gpu', 'nv2a_vsh', 'nv2a_pb_control', 'nv2a_semaphore', 'nv2a_clear',
               'nv2a_combiner', 'framebuffer_input', 'framebuffer_gpu', 'xaudio2',
               'apu_mixdown', 'kernel_handle_lifetime', 'kernel_save_paths', 'brute_force_cpu'):
    for path in (ROOT / 'tests' / folder).rglob('*'):
        if path.suffix in ('.c', '.h', '.py') or path.name == 'CMakeLists.txt':
            add(path)
for path in PROJECT.iterdir():
    if path.suffix in ('.md', '.ps1', '.py', '.json') or path.name == 'CMakeLists.txt':
        add(path)
for path in (PROJECT / 'src').glob('*.c'):
    add(path)
for name in ('translator.py', 'test_lahf_carry.py', 'test_static_callback_cache.py'):
    add(ROOT / 'tools/recomp' / name)
for name in ('build-verification.json', 'performance-work-state.json', 'performance-investigation.md',
             'gpu-residency-investigation.md', 'perf137-vs138-final.json', 'perf132-gpu-contention.json',
             'perf123-source-before.zip', 'readback119-cpu-samples.json', 'gpu_wait118.stderr.log',
             'test-launcher132-final.json'):
    add(ANALYSIS / name)
for prefix in ('perf_baseline137_idle', 'perf_candidate138_idle', 'perf_candidate132_default_replay',
               'final_candidate139_normal', 'perf_baseline133_matched'):
    for path in ANALYSIS.glob(prefix + '*'):
        if path.suffix in ('.json', '.jsonl', '.md', '.log'):
            add(path)
for name in ('test-raster132-default.log', 'test-raster132-event-pool-bulk.log',
             'test-raster132-event-reload.log', 'test-fences132-default-gpu.log',
             'test-fences132-event-gpu.log', 'test-vertex132-input-mask-expanded.log',
             'test-pushbuffer132-final.log', 'test-keyboard132-final.log',
             'test-xaudio2132-final.log', 'test-framebuffer-gpu131.log'):
    add(ANALYSIS / name)
shots = {'perf_baseline137_idle': ['00331'], 'perf_candidate138_idle': ['00320'],
         'perf_candidate132_default_replay': ['00602', '00726', '00929', '01125', '01209'],
         'final_candidate139_normal': ['00419', '00567', '00681', '00905']}
for prefix, indices in shots.items():
    for index in indices:
        add(ANALYSIS / f'{prefix}-frame-{index}.bmp')

before_map = {'fb_present.c': 'src/video/fb_present.c',
              'raster_test.c': 'tests/nv2a_gpu/raster_test.c',
              'semaphore_test.c': 'tests/nv2a_semaphore/semaphore_test.c',
              'control_test.c': 'tests/nv2a_pb_control/control_test.c'}
delta = ['# Limited initial-snapshot comparison\n',
         'Only the10 archived files are compared. This is not the full Git diff,\n'
         'nor proof that the old executable corresponds exactly to this source.\n\n']
with zipfile.ZipFile(ANALYSIS / 'perf123-source-before.zip') as before:
    for name in before.namelist():
        relative = before_map.get(name, 'src/kernel/' + name)
        old = before.read(name).decode('utf-8-sig').replace('\r\n', '\n').splitlines(keepends=True)
        new = (ROOT / relative).read_text(encoding='utf-8-sig').splitlines(keepends=True)
        delta.append(''.join(difflib.unified_diff(old, new, fromfile='before/' + relative,
                                               tofile='current/' + relative, n=0)))
(HERE / 'SOURCE_DELTA.patch').write_text(''.join(delta), encoding='utf-8')
for name in ('PROMPT.md', 'SNAPSHOT.md', 'SOURCE_DELTA.patch', 'make_bundle.py'):
    add(HERE / name)
manifest = [{'path': path.relative_to(ROOT).as_posix(), 'bytes': path.stat().st_size,
             'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
            for path in sorted(files)]
(HERE / 'MANIFEST.json').write_text(json.dumps({'files': manifest}, indent=2) + '\n', encoding='utf-8')
add(HERE / 'MANIFEST.json')
output = HERE.parent / 'brute-force-opus55-review.zip'
with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    for path in sorted(files):
        archive.write(path, path.relative_to(ROOT).as_posix())
with zipfile.ZipFile(output) as archive:
    assert archive.testzip() is None
    for item in manifest:
        assert hashlib.sha256(archive.read(item['path'])).hexdigest() == item['sha256']
print(json.dumps({'zip': str(output), 'bytes': output.stat().st_size, 'files': len(files),
                  'sha256': hashlib.sha256(output.read_bytes()).hexdigest()}, indent=2))
