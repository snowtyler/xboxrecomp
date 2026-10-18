# Brute Force v0.14 integration

October 18, 2026. Branch: `codex/integrate-v014`. Local baseline: `1788cb4`.
Upstream release source: `v0.14.0` / `eb0aa1d`.

This selectively ports the changes identified in `UPSTREAM_V014_REVIEW.md`.
It does not merge the release's replacement renderer, DMA walker, allocator,
thread/handle model, callback discovery or platform changes into our fork.

## Integrated changes

- Floating-point and flag lifting from the release, including MXCSR-controlled
  integer conversions, x87 remainder/status, SAHF flag snapshots, reciprocal
  estimates, XADD flags, FCMOV and unsigned NEG. Runtime helper templates and
  conformance fixtures accompany the lifter. Added guest MXCSR TLS at reset
  value `0x1F80`; preserved our LAHF carry and callback-analysis cache changes.
- Runtime IN/OUT helpers and field-pin behavior at `0x80C0`, backed by a count
  of the existing vblank ticks. The tick rate and interrupt-delivery policy
  are unchanged. IN writes the requested guest register width through the
  lifter's existing partial-register helpers.
- APU device-queue pacing and stalled-device fallback, safe XAudio2 period
  staging, pitch resampling, automatic DSP doorbell discovery/reset gating,
  one-shot front-end trap latching, sample-counter/SSL support, lifecycle
  cleanup, and WAV/starvation/voice/ring diagnostics.
- Positive-zero output W becomes 1 at the CPU and raw-GPU rendering
  boundaries. Negative zero stays clipped. The vertex interpreter's instruction
  output remains unchanged. GPU readback tests exercise both signs.

Retained our physical address resolver rather than importing the upstream
page allocator/map dependencies. Retained the non-consuming PCM inspection
helper, halted-pipeline stale-PCM guard, raw PCM diagnostics and master-volume
mute behavior. Hardware PCM and software voices are now combined with a
saturating add. Tests that inject hardware periods explicitly mark the
sequencer running, matching that guard's contract.

The GET walker is byte-for-byte unchanged. `xbox_memory_layout.c` changes only
by adding MXCSR state. Semaphore/completion ordering, SDK ring bounds,
full-barrier GET publication, failure freezes and experimental defaults remain
as before. The unsafe upstream VSH constant diagnostic was not imported.

The GPU backend, generic fixed-function/AA work, kernel multiple-object wait
changes, allocator/thread changes and wider callback discovery remain future
ports. Their omission is deliberate scope, not an assertion that they are
unnecessary for other titles.
The APU hook uses our retained x86 MMIO decoder coverage. The release's wider
ALU decoder is deferred with the platform work; its arm64 decoder header is
included as a source dependency of the APU hook, without importing the host
fault dispatcher. Validation here is on Windows x64.

## Regeneration and reproducibility

Regenerated into ignored `analysis/v014-fp-generated/` using the current
function, identification, ABI, manual-function and coalescence metadata.
There were zero translation failures. Both old and new output contain 26,463
dispatch entries and the same 26,701 function definitions (including generated
alias definitions). No dispatch address or body entry was added or removed.

The generated code now contains the eight rounding-aware conversions, the
status-aware remainder helper and both port reads identified in the review.
After checking those sets, copied the generated output into `src/recomp/gen/`
and restored the diagnostics. The pre-port generated tree is archived in
`analysis/pre-v014-generated.zip`; file hashes are recorded in
`analysis/v014-generated-manifest.json` before instrumentation.

The SDK writer installer now finds its routine and labels across generated
chunks rather than assuming every site remains in `recomp_0104.c`.
The journal implementation is compiled once in `src/recomp_sdk_trace.c`;
generated chunks include `src/sdk_writer_trace.h`, sharing one journal.
`recompile.py` also restores the GET diagnostics after regeneration.
`refresh_abi_fixtures.py` extracts the existing stack/ABI fixture bodies from
the current generated code, so those checks exercise the integrated lifter.

## Validation

- Lifter suite: 572 passed, 113 subtests passed, one skipped. MSVC does not
  provide the signed-integer-overflow sanitizer used by the skipped case.
  The suite includes the 979-snippet CPU differential check.
- Disassembler plus title qualification/accounting tests: 65 passed.
- Native tests: 48 passed across GPU, GET/control, semaphore, IRQ,
  regenerated stack/ABI bodies, CPU probe, handles, save paths, VSH, APU
  pacing/period/mixdown/lifecycle/doorbell, XAudio2 and WAV output.
- Launcher environment regression passed. Experimental GET, IRQ gating and
  coalescing remain off in the default launcher.
- Test dependencies live only in ignored `analysis/v014-testdeps/`; no
  global Python packages were changed.

Candidate152 built successfully with MSVC RelWithDebInfo and generated-code
optimization enabled. EXE SHA256:
`b20521ff073a3ea024b67daf9d3512d1dc6e8d04ae5d66eed0ab709e65f5b13a`.
The retained candidate151 still hashes to its recorded
`f19bea59ab04259eb2c6b2e87eecada091a9d0bf9d03e8edc29c1195d42fc341`.

The first probe, `perf152_v014_audio`, is invalid and excluded. It was stopped
at 55.25 seconds when metadata showed that the new runner had created empty
environment entries for disabled switches. Presence-based flags treated them
as enabled, including master mute. The runner now explicitly removes disabled
variables and fails if removal does not stick. The corrected probe's actual
child environment was checked before interpreting results.

The corrected normal-audio run, `perf152_v014_audio_clean`, completed 600.48
seconds with the intentional watchdog exit 3, no external stop and no timeout.
Its actual child configuration had auto doorbell discovery, unmuted audio,
normal graphics barriers, and GET progress/coalescing/IRQ gating/profiling
switches absent. Auto discovery mapped GP scratch page `0x03390000` through
table `0x0339C000`, acknowledging physical `0x03390810`, matching the old
fixed contiguous-window address `0x83390810`.

Completed-frame captures at approximately 550 and 591 seconds show HUD
gameplay, movement/camera change and ammo 48/500 to 27/500. These are
`perf152_v014_audio_clean-frame-00355.bmp` and `...-00385.bmp`. Intermediate
GPU surface dumps show a landscape without the HUD; they must not be used to
conclude that the presented frame was frozen or missing its overlays.

The last PCM line reported 25,985,024 stereo sample frames, 49,791,592 nonzero
channel samples and peak 32767. No direct-stub, PB-parser, fault or access
violation marker was found. The new XAudio2 counter reached 8,533 underruns.
This is not proof of continuous/correct audio, and no comparison establishes
whether those dry spells predate the port. Movie pixel artifacts remain
visible. No performance improvement, replay pass or production promotion is
claimed.

The corrected mute run, `perf152_v014_mute_clean`, completed 180.20 seconds
with intentional exit 3, no external stop and no timeout. Child metadata
confirmed master mute with the same auto doorbell and disabled experimental
flags. The backend reported output muted, while the source delivered
7,831,552 stereo sample frames and 14,194,174 nonzero channel samples. A
completed-frame capture at about 160 seconds shows an advancing mission
briefing (`...-frame-00146.bmp`). No direct-stub, PB-parser, fault or access
violation marker was found. Its underrun counter reached 2,369; this short
movie test does not qualify muted gameplay or replay.

No probe remains active. Runtime summaries and source/binary hashes are in
ignored `analysis/v014-integration-verification.json` and the per-run logs.
The named prototype and retained performance candidates are not overwritten.

## Commands

```powershell
python -m tools.recomp games/brute_force_e3/input/default.xbe --game-name "Brute Force E3 Prototype" --all --split 250 --gen-dir games/brute_force_e3/analysis/v014-fp-generated --disasm-dir games/brute_force_e3/analysis/disasm --func-id-dir games/brute_force_e3/analysis/func_id --abi-dir games/brute_force_e3/analysis/abi --output-dir games/brute_force_e3/analysis/v014-fp-recomp --functions games/brute_force_e3/analysis/recomp_functions.json --manual-functions games/brute_force_e3/manual_functions.json --coalesce-functions games/brute_force_e3/coalesce_functions.json
cmake -S tests/upstream_v014 -B build/v014-integration -G "Visual Studio 17 2022" -A x64
cmake --build build/v014-integration --config Release -- /m:1 /nr:false
ctest --test-dir build/v014-integration -C Release --output-on-failure
```
