# Brute Force v0.14 integration checks

This Windows fixture links the integrated runtime once and runs the upstream
APU IRQ, automatic doorbell, lifecycle and XAudio2 staging regressions alongside
the retained Brute Force PCM/mixdown checks and a runtime guest-port check.
XAudio2 uses a fake device; no game assets are required.

```powershell
cmake -S tests/upstream_v014 -B build/v014-integration -G "Visual Studio 17 2022" -A x64
cmake --build build/v014-integration --config Release -- /m:1 /nr:false
ctest --test-dir build/v014-integration -C Release --output-on-failure
```

Run `tests/apu_monitor`, `tests/apu_wav`, `tests/nv2a_gpu`,
`tests/nv2a_pb_control` and `tests/nv2a_semaphore` separately for queue pacing,
WAV output, rendering, late-patch races and completion ordering. Game probes
use `games/brute_force_e3/verify_upstream_v014.ps1`; they are separate from
fixture success and do not promote an executable.
