# Selected v0.15.0 integration

Work in progress, October 20, 2026. Baseline `4b6079b`; branch
`codex/integrate-v015`. Upstream tag `v0.15.0` resolves to `6376eb8`.
Each numbered item has its own commit and validation below. Pending checks are
not qualification. Candidate157 and all earlier binaries are retained.

## 1. Kernel callback stack and registers

Confirmed the original `deliver_one_apc` builds a 16-byte NTAPI/stdcall frame
and then adds 12 after the callee has already consumed it. Ported the saved
stack/register approach from `a37d669` and `117a649` to file APC delivery,
NtUserIoApcDispatcher, DPC execution and KeSynchronizeExecution. EBP is also
preserved alongside EAX/ECX/EDX/EBX/ESI/EDI/ESP. Only synchronize retains the
callback's EAX result; dispatcher status results remain their own.

The other original `g_esp += 12` sites are `bridge_run_thread_inline`, the
first-call PsCreateSystemThreadEx path and the first-call PsCreateSystemThread
path (original lines 502, 622, 7954). Each invokes an Xbox system thread start
routine with two arguments, a stdcall `ret 8` contract, not a cdecl function.
All now restore their pre-frame ESP rather than assuming additional cleanup;
first-thread register inheritance is retained. These paths rarely return.

The kernel bridge fixture calls the actual thunk dispatcher for file-I/O APC,
NtUserIoApcDispatcher and synchronize callbacks. It clobbers registers and
exercises both stdcall and defensive cdecl return behavior, then executes a
caller POP ESI against a stack canary. The DPC shares the tested save/restore
helper; asynchronous dispatch policy is unchanged. MSVC RelWithDebInfo native fixture: zero failures (six callback cases,
including the real file-APC path); logs in ignored `analysis/v015-kernel-*`.
The final fresh-candidate gameplay/death/replay gate is still pending.

## 2. POP ESP

Confirmed the old macro assigns ESP before incrementing it. Ported `54b64ca`:
read into a temporary, increment the old stack pointer, then assign destination.
The current generated tree contains one `POP32(esp, esp)` in `recomp_0082.c`.
Regeneration matters for the copied runtime header; the emitted instruction
statement itself does not change. Refreshed that ignored header now and will
regenerate separately in item 3. Added upstream stack-operand fixture and a
lifter assertion. MSVC stack fixture passes push/pop ESP, the round-to-integer
idiom, ordinary POP, memory stack operands, LEAVE and PUSHAD/POPAD. The focused
lifter test passes.

## 3. Lifter applicability and regeneration

Ported `e1864d5` (lazy flags image, PUSHFD/POPFD and Pentium III CPUID),
`facb626` (BP/SP sign extension) and `b3c90cf` (66-bit-pi x87 trig helpers).
The flags image is thread-local, initialized to 0x202. Existing MXCSR, LAHF
carry handling and boundary/ABI analysis remain intact. AF is still not fully
modeled; unsupported PUSHFD flag reconstruction retains explicit diagnostics.

`audit_v015_instructions.py --decoded` checks the retained whole disassembly,
then decodes the existing dispatch functions using the same metadata/coalescence
as regeneration, excluding the manual override and deduplicating addresses.

| Pattern | Whole disassembly | Decoded dispatch functions |
|---|---:|---:|
| PUSHFD | 116 | 16 |
| POPFD | 89 | 8 |
| CPUID | 3 | 2 |
| MOVSX r32, BP/SP | 4 | 4 |
| FSIN | 25 | 25 |
| FCOS | 33 | 33 |
| FSINCOS | 16 | 16 |
| FPTAN | 6 | 6 |
| XADD, unprefixed | 6 | 5 |
| LOCK XADD | 0 | 0 |
| LAHF | 93 | 4 |
| POP ESP | 103 | 1 |

Counts describe static decode, not execution. Whole-section decoding includes
data; even a translated body can retain unreachable decoded bytes. Three LAHF
sites immediately follow a carry setter. Our existing unconditional LAHF carry
reader check already covers `e3f6373`; retained it without weakening the check.
All five applicable XADD sites are unprefixed (`0FC1`, no F0 prefix), unlike the
earlier review's wording. The v0.14 sum-flag port already covers them. The
LOCK-only declaration fix `6ac4bba` is therefore not imported for this title.

Regenerated separately into ignored `analysis/v015-generated/`, with zero
translation failures. The old and new dispatch entries are identical (26,463),
as are all 26,701 function definitions including aliases. Archived the previous
tree as `analysis/v015-pre-regeneration.zip` (its header already has item 2),
and recorded the archive/generated hashes and census in
`analysis/v015-generated-manifest.json`. Replaced the ignored generated tree,
restored GET, SDK, loader/audio and guest-state diagnostics, and refreshed nine
ABI fixture bodies. No new discovery or disassembly boundaries were imported.

Focused flags/CPUID/trig/LAHF tests passed (54 tests, 24 subtests), with the
MOVSX fixture added afterwards. The first full run passed 607 tests and 136
subtests, skipped the existing unsupported MSVC sanitizer case, and exposed one
FPTAN differential mismatch. Its live tangent value used an exact comparison,
unlike standalone FPTAN: native tan(1) and the pi66 sin/cos quotient differ by
one ULP. Applied the same 1e-9 transcendental tolerance to that sequence only;
status/branch registers remain exact. The 1,249-vector sequence and full CPU
differential recheck pass. Final suite: **608 passed, 136 subtests passed,
one existing sanitizer skip**. All **84 native tests pass**, including the
91,344-schedule GET/control combinations, semaphore, raster, regenerated ABI,
kernel callback, x87 positive/negative-control, APU and XAudio2 fixtures.
POSIX-only projects are inapplicable on this Windows host; the old v0.14
aggregate duplicates component fixtures. Logs: `analysis/v015-lifter-suite-final.log`
and `analysis/v015-native-tests.log`. Final fresh-game gate remains pending.

## 4. Vblank measurement

Measured the **unchanged candidate163** counter `g_vblank_total` by read-only
PDB symbol access, with host QPC timestamps once per second. Each process used
a fresh copy of the same saves and the reviewed normal-barrier route, GET on,
monitor/IRQ/coalescing/render diagnostics/epochs off. `RECOMP_VBLANK=1` remained
set. The second process omitted `RECOMP_NV2A_NOTIFY` entirely, rather than setting
it to `0`. No runtime instrumentation or timer code changed for these runs.

| Setting and verified state | Counter delta | QPC seconds | Hz |
|---|---:|---:|---:|
| Notify on, title screen | 2,933 | 74.5459 | **39.3449** |
| Notify on, living HUD gameplay | 2,885 | 74.5652 | **38.6910** |
| Notify off, title screen | 2,963 | 74.5431 | **39.7488** |
| Notify off, gameplay | — | — | **Not reached** |

Reviewed completed title/HUD frames at both ends; gameplay guest state has a
living campaign and stable roots. The notify-off process remained on the title
path, SDK flip serial 8,055 at both 620 and 695 seconds. Its later 73.9015-second
counter interval measured 39.9315 Hz, but this is **stalled title**, not gameplay.
No notify-off gameplay rate is claimed. The runs lasted 710 seconds each, including
the title hold, menu navigation and intro playback. Source editing, regeneration
and fixture checks overlapped parts of collection; these are tick-rate diagnostics,
not clean-host performance comparisons.

The rate is materially below 60 Hz; the exact 32 Hz inference is not supported
on this host. The legacy tick schedules `now + 16` on a coarse-clock/10 ms timer
loop; notify wakes and ISR/DPC work also affect its cadence. The count is ticks,
not proof of ISR acceptance or guest completion at the same rate.

**Paced-vblank assessment: not ported in this integration.** `5bb9e04` contains
both pacing and substantial interrupt/acknowledgement changes. An isolated opt-in
port should use an absolute QPC-nanosecond 60 Hz schedule, a high-resolution
waitable timer, PCRTC/PMC enable checks and a separate acknowledgement watcher,
with no DISPATCH gate. Our notify mode already implements W1C in
`irq_write_veh`, with child-status clearing deasserting PMC, and excludes these
registers from the legacy ack thread. Copying the upstream plain-RAM ack watcher
alongside it could acknowledge a freshly raised source prematurely. Adaptation
needs register-level tests for both notify modes, masked/unmasked delivery,
delayed DPC acknowledgement and shutdown before a live paced comparison. Retain
the qualified vblank behavior here; this measured prerequisite belongs before
the later 60 FPS pacing/flip-hold work, not as an unqualified default change.

Guest dependencies: the NV2A ISR/DPC drives SDK vertical-blank events used by
presentation-interval/frame-limiter code; the field pin at port 0x80C0 derives
bit 5 from this counter. Brute Force reads that pin at 003A4C7C/003A4F3D and
stores the result in device+0x1D0. These are vblank timing dependencies, distinct
from release/semaphore waits. No presentation-interval setting, exact FPS cap,
audio consequence or fixed speedup is inferred from this rate alone.

Raw series, frames, launch receipts, guest states and hashes are in ignored
`analysis/v015-vblank-summary.json` and `v015_vblank_original_{notify,poll}*`.
The normal-barrier full gate runner `verify_upstream_v015.ps1` uses a fresh save
copy and records EXE/PDB hashes; it clears rendering diagnostics and keeps the
qualified GET monitor requirements. Final fresh-candidate gate: pending.

## 5. Rendering leads

Confirmed unclamped oD0/oD1 in the CPU vertex interpreter and the D3D11 vertex
wrapper. Pixel-stage saturation occurs after interpolation and cannot substitute
for vertex-output saturation. `RECOMP_NV2A_COLOR_CLAMP=1` now independently
clamps both colours at the CPU output and both transformed/raw hardware vertex
boundaries, before interpolation. It is off when unset, empty or `0`; the HLSL
compile-time constant folds out the disabled branch. This is a diagnostic, not
a promoted rendering fix. Gameplay foliage comparison: pending fresh candidate.

`RECOMP_NV2A_V015_RENDER=1` independently opts into the other missing semantics:
CPU and D3D11 MIRROR/BORDER addressing (including border-source selection and
SET_TEXTURE_BORDER_COLOR), reading the last SET_VERTEX_DATA* value for size-zero
attributes in both hardware packing and CPU vertex input, and SET_FOG_PLANE /
SET_EYE_POSITION writes to shared constants c[57]/c[56]. The sampler cache keys
the full descriptor including border colour and retains per-draw references
across cache eviction. Ordinary launch clears both diagnostic flags.

Near-plane clipping is already present in our `nv2a_clip.h` and hardware D3D11
clip volume. The CPU path reconstructs from window bounds/depth range, not
shader c[58]/c[59] viewport constants; no `8f84d41` CPU-raster replacement is
needed. Existing clipping tests remain unchanged. Corrected the survey table:
0x1808 ARRAY_ELEMENT32, 0x1818 INLINE_ARRAY. Walker/publication logic is untouched.

New native GPU tests pass with flags `0` and `1`: interpolated colour differs
as expected before/after clamp on transformed and raw vertex paths, CPU oD0/oD1
negative/high lanes clamp only when opted in, mirror chooses the correct texel,
and changing border colour changes output (cache-key coverage). Two native
method tests verify c[56]/c[57] via the real method dispatcher. Two compiler-backed
tests use the actual private fetch/coordinate function bodies to check all 16
current attributes, zero-stride declared arrays, negative mirror and wrap.
All 87 native tests passed before adding the two constant-method checks, which
also pass. Launcher regression waits until the baseline probe releases its lock.

## 6. APU

Confirmed `voice_get_samples` used fixed two-block ADPCM buffers while accepting
the descriptor's wider samples-per-block value. Ported `b380f02`: buffers hold
up to 18 blocks and wider values return before copying/decoding. Ported
`70afb54`: each bin ramps from its previous frame's gain to the target across
32 samples; new-voice/reset clears gain history. HRTF remains unchanged.
Master mute, stale-PCM guard and non-consuming inspection are retained.

The new voice fixture tests mono/stereo wide ADPCM, refuses values above the
buffer limit and checks every sample of a gain transition. It passes with the
rest of the 87-test native suite. This is not an audio-rate measurement or a
claim to resolve the 88% production shortfall.

**Per-voice locks deferred.** The retained WPR precise export
`wpr162-target-waits-export/CPU_Usage_(Precise)_By_Process.csv` covers executor
and main guest waits, not the APU producer/DirectSound lock path. It cannot
establish or reject contention there. Source confirms `voice_lock` takes
`d->lock` and the producer holds it while mixing; that is a lead, not measured
blocked time. `8627159` also changes protection for FE list/voice mutation
relative to the mixer; it is not treated as a low-risk unconditional port.
The broader retained `wpr162-waits-export/CPU_Usage_(Precise)_Thread_Delays.csv`
is truncated at row 46,334 (17 fields versus 56); it is not a complete alternative.
No voice-lock/producer stack was found in its valid prefix, which cannot rule
out waits in the missing threads. The checked prefix and incompleteness are
recorded in `analysis/v015-apu-waits.json`. Obtain a targeted precise-wait export
before changing that synchronization.

## 7. Profiling collector

Documented that any process as the installing user can trigger elevated
system-wide traces and read results containing other processes' names, command
lines and stacks; recommend uninstalling after the campaign. Added a 20 GiB
free-space prerequisite both before broker acceptance and before worker capture.
Refusal creates no capture job at acceptance and never deletes retained files.
The worker rechecks before starting WPR/Nsight. This is a free-space guard, not
a lifetime/session byte cap. Self-tests cover negative/zero/one-byte-below,
exact and one-byte-above threshold. Separate .NET Framework x64 diagnostic
`ProfilerV015.exe`/PDB builds and all self-tests pass, including live pipe
round-trip, identity checks and injection rejection. SHA-256 EXE:
`71a3bfc236ea586f1f8526c3e7198a155daeb0b09f31b98229d69041432bdf88`;
PDB: `6c4a7c86bf54c10d4799959b0ae4621ad9004871f39466d3b865c20c0aceb74f`.
The previously installed protected helper remains untouched; deployment of the
changed privileged binary requires the existing administrator install procedure.

## Deliberate omissions

No DISPATCH gate/IRQ safe points (`5bb9e04`, `2fb0b44`), back-edge yield
(`c7a1817`), threaded CPU rasterizer (`1ea1871`), window/fullscreen/presenter,
USB/input/rumble, new function discovery, `RECOMP_CONTIG_AVOID_IMAGE`, HRTF default
or flip hold (`3d7c5f8`). IRQ exclusion remains reference-only, with no demonstrated
race. GET walker, completion/publication policy, coalescing default and candidate157
are unchanged. Texture epochs qualification remains the next performance step.

## Final validation and binary identities

Pending: lifter/differential, native fixtures, launcher regression, separate
regeneration/ABI/dispatch accounting, candidate hashes and monitored normal-barrier
gameplay/death/replay evidence (lag at most 0x3FC, zero far/read-past decisions).
