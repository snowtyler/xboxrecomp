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

Pending: measure the retained implementation before considering paced vblank.

## 5. Rendering leads

Pending. Rendering changes remain opt-in diagnostic behavior.

## 6. APU

Pending.

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
