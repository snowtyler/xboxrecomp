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
including the real file-APC path); logs in ignored nalysis/v015-kernel-*.
The final fresh-candidate gameplay/death/replay gate is still pending.

## 2. POP ESP

Pending.

## 3. Lifter applicability and regeneration

Pending. `audit_v015_instructions.py` records whole-disassembly counts separately
from existing generated-function ranges. Decoded data is not runtime evidence.

## 4. Vblank measurement

Pending: measure the retained implementation before considering paced vblank.

## 5. Rendering leads

Pending. Rendering changes remain opt-in diagnostic behavior.

## 6. APU

Pending.

## 7. Profiling collector

Pending.

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
