# Current verification state

**2026-10-14 source handoff:** performance work is paused at the user's request.
Candidate132 has not replaced the old verified executable described below.
Instrumented fixed-view frame time improved49.22→45.24ms (8.1%); query-free
normal-mode benchmarks remain pending. The final normal-graphics gameplay test
was explicitly stopped at1325.32s, so it is not a completed replay/resumed-control
gate. See `review/opus55/SNAPSHOT.md` and `PERFORMANCE.md`. Source/evidence sharing
on main does not certify the candidate or change the local verified executable.

A playable native Windows x64 prototype build is verified on this machine:
`build/RelWithDebInfo/brute_force_e3_prototype.exe`. Launch with `play.ps1`.
The original extracted game folder remains the asset source; saves and logs
are written under this project. Retail remains a separate future project.

The exact named executable completed 1500.32 seconds with normal XAudio2
output. Its watchdog intentionally ended the probe (exit 3; no host timeout
or explicit stop). Completed-flip captures prove combat damage/firing,
Squad Killed, restart confirmation, repeated intro, resumed HUD, movement
and firing after replay. No AV, direct generated-target stub, PB parse error
or GPU readback failure was found. Source PCM reached 63,438,848 stereo
sample frames with 87,199,788 nonzero channel samples.

The refreshed final launcher test found a visible framebuffer and confirmed
that closing it ends only its owned game child. Native keyboard accept,
start, movement, firing, focus and closure checks passed. Native GPU
coherence/mip/pushbuffer and XAudio2 ownership/mute/failure-cleanup regressions
passed. Master mute retains console audio processing; the muted candidate
also passed ten minutes through gameplay.

Known limitations: variable gameplay performance (tested views about 8-28 FPS),
Bink decoding artifacts, and passthrough audio DSP. Full-campaign/multiplayer
coverage and physical-controller hardware testing remain unverified. These
are disclosed limits of this playable prototype, not a claim of complete
Xbox compatibility. No NVIDIA settings were modified. Original XBE SHA256
remains bcf4fbfb1adc595a3d04fe1443f22fcc1ec61bee4909f2780783d6ba0a13c4de.

The artifact manifest and exact runtime evidence are in
`analysis/build-verification.json`. All intentional probes have ended.

The sections below are a chronological development archive. References to
live processes or unfinished features describe their state at that checkpoint;
this current-state section takes precedence.

## GPU vertex execution (abi61Ã¢â‚¬â€œabi67)



src/kernel/nv2a_vsh_hlsl.c translates the verified kernel encoding, not the

older D3D8 decoder. A snapshot captures the current start-to-FINAL program and

192 constants. Source swizzles/negation, temp/position aliases, paired MAC/ILU

source-before-write behavior, masked destinations, implicit paired R1, relative

constant addressing, ARL, reciprocal clamps, EXP/LOG/LIT and default outputs

are implemented. Programs capable of writing shared constants are rejected

for GPU execution; CPU fallback preserves their ordered effects.



The GPU arithmetic test passes 116 configurations Ãƒâ€”64 inputs =7,424 inputs,

comparing every output component with the CPU interpreter (relative tolerance

0.0002; matching infinities/NaNs handled explicitly). Additional multi-step

cases cover the D3D viewport/RCC epilogue, ARL and relative bounds, temp/opos

aliasing, paired temp writes and default unwritten W. The raster fixture also

runs a real vertex shader directly from raw attributes and checks rejection

of stateful variants. This is reference equivalence, not full NV2A proof.



Raw batches deduplicate original indices, upload canonical 16 float4 attributes

per unique vertex, bind cached translated vertex shaders, and stream current

constants. Fixed-function fog is calculated in the vertex wrapper. Pixel/depth

raster state uses the existing hardware path. RECOMP_HW_VERTEX=1 enables this

path; it is not the default because sustained runtime verification has failed.



vertex61 initially performed worse (roughly 200+ ms raw submission in complex

views). Profiling separates input packing (about 5 ms/frame) from submission

(over 210 ms/frame in packing64). The large per-draw VS constant buffer now

uses DYNAMIC/WRITE_DISCARD mapping rather than UpdateSubresource, following

Microsoft's dynamic-resource API contract:

https://learn.microsoft.com/en-us/windows/win32/direct3d11/how-to--use-dynamic-resources

The rebuilt raw raster regression passes. discard65 measured a later interval

with packing 1.54 ms and submission 15.01 ms at 17.35 FPS; timings vary by view

and warm-up. This is not a stable/playable benchmark.



Semaphore completion can now be batched until the synchronous frontend has

finished reading submitted commands, so a release cannot immediately permit

command storage reuse mid-walk. Updates coalesce by resolved guest target;

completion is suppressed if parsing reports unknown words or GPU sync fails.

The native semaphore fixture verifies no early write and final executed serial.

This change alone did not eliminate the longer-run parser failures.



Original SDK 0x0039EA90 checks GET against device +0x24/+0x28. Main registers

these through xbox_Nv2aBindRing; bounds66 logs exact physical bounds

00002000..00182000. The executor now uses these when valid instead of estimating

the ring from observed PUTs. Longer runs still produce command-budget errors

followed by reads toward bogus addresses; guards prevent unmapped dereference

but the command stream is invalid and eventually stalls.



nop67's first failure is command-budget exhaustion, followed by false CALLs

(e.g. from 800CC5F8 to 821D01F8, last NOP=0), then unreadable targets. There

are no PB RESUME mismatches in that run. Investigate cycles/compiled return

jumps, command storage reuse and remaining metadata/interrupt semantics from

the FIRST budget failure; do not seed or normalize these bogus addresses.

A full SDK NOP interrupt-side implementation is still absent.



Latest probes are terminal: vertex61 (120.31s), sync62 (75.18s), fence63

(120.30s), packing64/discard65 (90.22s), bounds66 (120.29s), nop67 (90.23s),

all watchdog exits. No playable build or movement/firing verification exists.

Logs: gpu-vertex-extended.log, gpu-raw-raster.log, test-fence-batch.log,

test-pb-control-latest.log, build-abi67.log and the named probe logs/JSONs.

The old split60 75-second result remains a narrower successful run, not proof

of longer stability. All work toward the full objective remains active.



Without audio, campaign setup and Quick Campaign load the tutorial scene.

Before the semaphore repair, the intro camera advanced and a character

appeared, but terrain was missing. gameplay51 now shows terrain in the tutorial

intro, with very noisy textures; performance remains roughly 1Ã¢â‚¬â€œ3 FPS. Normal

player movement/firing and the end of the intro remain unverified.

Large index batches have capacity 65,536 and full-width draw-array indices.



USB HC0, four root ports, device port index 2 gives Controller 1 confirmation.

Menus respond to scripted Start/A. USB reports carry scripted sticks/triggers,

but their gameplay effect remains unverified. RECOMP_INPUT_TRACE=1 logs changed

SDK reports after callback 0x003ADAFC. gameplay51 now confirms left Y=32767,

RT=255, and right X=32767 in the SDK, plus release back to idle.



Audio uses RECOMP_AC97_READY=1. The APU already resolves physical DMA buffers

through the separately backed contiguous window, so initialize it with the

normal guest memory base. RECOMP_APU_DSP_ACK=0x83390810 uses the observed guest

virtual command doorbell. Physical 0x03390810 is different storage and does not

release the wait. Acknowledging commands 3 and 2 passes initialization but

computes no DSP effects. Audio output is unverified.



Recovered DirectSound adapter 0x0033F99B ends in ret 4; its placeholder left an

argument on the caller stack. Bink block callbacks 0x00388760 and 0x00388480,

and worker I/O callback 0x00385F60, are now compiled. Skipped calls had left

uninitialized decoder output and caused corruption in 0x00389440.



Bink's MMX probe at 0x00330100 used unlifted pushfd/popfd/cpuid instructions,

leaving conversion callbacks unset. manual_functions.json now excludes that

body; src/recomp_cpu.c implements the guest capability, cache, and explicit

disable flag. Its native test checks result, preserved registers, and cdecl

return/argument cleanup. Other reached cpuid sites 0x002C7DB4 and 0x002C8264

remain unsupported; this replacement is specific to the Bink probe.



Pixel converters 0x003911E0 and 0x00391020 are observed seeds. Alternatives

0x00391430, 0x00398560, 0x00397FC0, 0x00398240, and 0x00397E00 are static entries

from the initialized descriptor table at 0x004F1D20.



## Latest startup repairs and remaining failures



The handle table is now locked. Single and multiple waits acquire lifetime

leases; closing a tagged guest token defers native CloseHandle until all active

waits release it. Missing/closed tagged tokens return STATUS_INVALID_HANDLE.

The native regression checks two active leases across close, rejection of new

waits, slot preservation, usable native event, and exactly one final close.

Other handle operations still resolve borrowed handles, and token generations

are unchanged. Untagged legacy handles still pass through (with wait logging).



lease39 still crashed at 108.64s. Tracing in decode43 identified the actual

wait token as untagged 0x00490052, read by Bink worker 0x00386BA0 from its

context. This was not proved to be a tagged handle close race. Bink creates

its worker suspended (creation flags 4), then initializes its events.

PsCreateSystemThreadEx ignored CreateSuspended and started the worker early.

The bridge now supplies CREATE_SUSPENDED, and normal guest NtResumeThread

starts it. fence45 logs suspended=1 for Bink workers, reaches menuBack.bik

and the title screen, and exits at the 140-second watchdog without the former

invalid handle exception. suspend44 independently stalled during earlier

DirectSound work before movies; startup scheduling still varies.



order48 proves the movie write was erased: CPU first/last row conversion

finishes, then the GPU clears that same buffer, then flips it. Moving the

blind fence mirror after scanning was insufficient because it acknowledged

serials that had not executed. Original SDK 0x0039E9E0 emits method 0x1D70

with the current serial; 0x003A2B60 binds semaphore DMA context 8 and offset 0.

The context buffer is stored through device global 0x003AB378 at device +0x30.



With RECOMP_PB_EXEC, main now registers xbox_Nv2aBindSemaphore(8,...,4)

instead of blindly mirroring device +0x2C. The executor handles 0x1A4 context,

0x1D6C offset, and 0x1D70 release; the release writes only the executed serial

through the registered, bounded buffer. This is a title-supplied DMA binding,

not a general GPU DMA-object resolver. Other titles still use existing mirror

APIs. The native test runs the real memory layout and command executor, checks

clear-before-release, no early completion, unknown context, offset/alignment

bounds, pointer overflow, and null buffer rejection. It passes.



semaphore49 shows the corrected order: clear, CPU conversion, flip. movie50

normal flip captures are nonblack and show actual video (frame 00079), with

visible decoding artifacts. Movie playback fidelity still needs work.

RECOMP_MOVIE_PIXELS=1 logs conversion and tiled/contiguous aliases;

RECOMP_MOVIE_ORDER=1 logs bounded first/last rows, clears, and flips.

RECOMP_FB_DUMP_OTHER_OFFSETS=0x184000,0x2B0000 produces alternate diagnostic

BMPs; use RECOMP_FB_DUMP_FLIPS with a short capture flag for movies, because

periodic report captures often run after the next buffer has been cleared.



Within each programmable draw batch, repeated full-width indices now reuse

vertex outputs using an epoch-tagged hash table. nv2a_vsh_cacheable disables

reuse when execution can write shared constants; uploads/new batches cannot

reuse prior results. Regression checks collisions, 32-bit indices, table-full

fallback, epoch rollover, and actual context-writing shader behavior.

cache52 records about 9.34M hits and 5.77M executions at its final report.

Vertex time is sometimes 29 ms/frame, but complex batches still take roughly

130Ã¢â‚¬â€œ160 ms/frame; rasterization dominates at about 360Ã¢â‚¬â€œ430 ms/frame.

The 120-second run ends at the watchdog without a native crash.



tests/nv2a_gpu device_probe.c confirms a real NVIDIA GeForce RTX 4070 Ti

D3D11 hardware device (feature level 0xB000), render-target clear, and verified

readback of every pixel. CIM enumeration was unavailable; this D3D11 check

succeeded. Hardware rendering is the next useful performance path. The existing

src/d3d/d3d8_vsh.c decoder uses different field positions (Word 0) from the

verified kernel interpreter (Word 1); do not assume it can translate this title

unchanged. Existing combiner HLSL generation needs a semantic check too.

A new src/kernel/nv2a_combiner_hlsl.c emits the kernel evaluator's register

arithmetic as HLSL, with uniform factor colors in a b0 constant buffer. It

preserves register 14=sum and 15=E*F, input mappings, dot products, output

scale/bias/clamp, mux selection, simultaneous RGB/alpha reads, ordered output

writes, final complements, and initial r0 alpha. Small/null output buffers

return failure. The source was compiled into abi53 unused, and is now called by hardware

rasterization when enabled. Existing src/d3d combiners use the opposite 14/15 labels, so

do not substitute that generator without checking its semantics.



The native GPU combiner test compiles generated HLSL as a compute shader on

the actual hardware device, dispatches 64 varied inputs for each of 40

configurations (0Ã¢â‚¬â€œ8 stages, shared/per-stage factors, mux modes, final combiner

and reserved output fields), reads float outputs back, and compares every

component with nv2a_rc_eval at tolerance 0.0002. All 2,560 inputs pass. This

proves equivalence to the existing evaluator, not complete NV2A fidelity or

performance/playability in the game. Texture fetch is outside this arithmetic

function. Shader compilation and buffer creation/readback are exercised.



## Hardware rasterization bring-up (abi60)



RECOMP_HW_RASTER=1 enables src/kernel/nv2a_hw_raster.c. Programmable batches

now submit transformed vertex outputs and triangulated 32-bit indices to a

D3D11 offscreen color/depth target. The PS uses the verified combiner generator,

projective 2D sampling, cube-face addressing, pass-through/clip-plane stages,

alpha test, blending, color masks, scissor, and depth state. Texture resources

are decoded through the existing sampler and cached against exact raw/palette

bytes; CPU rewrites cause re-upload. Only mip level zero is uploaded, matching

the prior software path; noisy/minified textures remain a fidelity issue.

Unknown stage modes currently return zero, also matching the software model.

This is not full NV2A feature coverage.



GPU-written color/native depth are synchronized before semaphore release,

CPU clears/fallback rasterization, BMP capture, and flips. After a sync, a new

hardware draw uploads CPU color again and initializes depth from the host

buffer, preserving movie pixels under later overlays. Surface changes sync

the prior surface. Feedback sampling, active z-pass counting, nonzero depth

clip minimum, incompatible blend-factor combinations, cache exhaustion, and

unsupported target sizes fall back to software. Vertex shaders still execute

in the CPU interpreter/cache. Hardware is opt-in; default remains software.



The native raster regression verifies actual GPU color/depth, farther-depth

rejection, scissor/blend, CPU movie writes before overlay, projective texture

quadrants, raw texture rewrite detection, and surface switching. It passes.

The native combiner 2,560-input equivalence and updated semaphore fixture pass.

GPU submissions are not by themselves proof of playable gameplay.



hardware55 reached roughly 11 FPS in parts of the tutorial, with raster cost

about 18Ã¢â‚¬â€œ24 ms/frame, then crashed at 47.52s in pb_walk. Initial parser guards

in abi56 prevented unmapped reads but did not fix command desynchronization.

call57-first.bin captures 4MB at the first falsely interpreted CALL. The

compiled geometry is actually entered by a JUMP and ends with a JUMP back to

the ring. Following that return without restoring the submitted bounds made

the walker read future/unsubmitted words. The supposed CALL after the NOP was

inline CPU record data, not a DMA instruction.



Original SDK encoder 0x0039C333 emits NOP parameter (record pointer <<5)|flags,

a linked-list word, then four record words; record +12 contains the next GPU

command. Main registers this prototype's action 3/shift 5/offset 12 layout.

The walker validates record placement/alignment/continuation, skips metadata,

and supports compiled JUMPs plus genuine one-level CALL/RETURN. This handles

the verified PC continuation; it does not emulate all SDK NOP interrupt-side

callbacks. At abi60 ring bounds were learned from observed PUTs. The latest source

uses a verified SDK binding when available, described above.



control59 still logged parser failures later. abi60 additionally retains

method count/subchannel/address/non-increasing state when a submitted segment

ends in packet parameters. split60 logs many such splits and resumes them.

Its 75.18s run ends at the watchdog, with no PB ERROR or native CRASH lines.

Native fixtures check compiled jump-out/jump-back, genuine CALL/RETURN, SDK

metadata skip and invalid continuation rejection, ring wrap, and increasing/

non-increasing split packets whose payload looks like control words. They pass.



split60 shows tutorial geometry/terrain in frame 00036 and about 4Ã¢â‚¬â€œ11 FPS

across measured views. Complex intervals: vertex ~95Ã¢â‚¬â€œ111 ms/frame and raster

~28Ã¢â‚¬â€œ31 ms/frame. Simpler interval: vertex 38 ms and raster 10 ms at 10.93 FPS.

CPU vertex execution, incomplete features/texturing, sustained stability, and

actual player movement/firing remain unfinished. Next useful performance step

is a verified GPU vertex-program translation using the kernel interpreter's

encoding; the existing D3D8 decoder has incompatible field positions.



All abi54Ã¢â‚¬â€œabi60 builds and hardware55/call56/call57/ring58/control59/split60

probes are terminal. Key logs: build-abi60.log, gpu-raster.log,

test-pb-control.log, test-semaphore-hw.log, split60.stderr.log and split60.json.



fence45-frame-00204.bmp shows the Brute Force title screen with audio enabled.

No scripted input was sent in that run; interactive gameplay remains unverified.



RECOMP_GPU_TIMING=1 reports vertex and raster time per flip. Movie rendering

bypasses those paths (zero time in pixels38). Use it in the tutorial to locate

its performance bottleneck. Untranslated-instruction diagnostics now report

each new reached VA per thread, avoiding a global print limit exhausted by IRQ

instructions.



## Reproduction



Terminal runs: audio33 (decoder crash), bink34 (missing I/O callback),

binkio35/moviehang36 (unset conversion callback), mmx37 (missing converters),

pixels38 (black presentation/handle error), lease39/buffers41/decode43

(handle diagnosis), suspend44 (early stall), fence45 (title screen), and

flip46/flip47 (movie buffer diagnostics), order48 (clear race proof),

semaphore49/movie50 (correct release and visible movies), gameplay51 (terrain

and SDK sticks/triggers), and cache52 (vertex reuse/timing). Logs are under analysis/;

build-abi60.log confirms the current build; recompile-bink-pixels.log records

the latest generation. All these probes are terminal.



Example: set RECOMP_INPUT_TRACE=1 and RECOMP_GPU_TIMING=1, then run

probe_runtime.ps1 -Name next68 -Executable brute_force_e3_abi67.exe -Audio

-DspDoorbell 0x83390810 -Seconds 120 -PadScript ''.

Use fresh names. JSON is written on completion. Creating the initially absent

analysis/<name>-stop.flag kills only that probe's child process. Optimized

builds take much longer and have not shown a clear frame-rate improvement;

build.ps1 -AbiCheck is useful for diagnostic builds.



Native verification: build/nv2a-semaphore/RelWithDebInfo/nv2a-semaphore-test.exe

tools/conformance/test.xbe passes (analysis/test-semaphore.log).

build/nv2a-vsh/RelWithDebInfo/nv2a-vsh-test.exe passes updated cache checks.

build/nv2a-gpu/RelWithDebInfo/nv2a-gpu-device-probe.exe passes

(analysis/gpu-device.log).



Hardware combiner equivalence: build/nv2a-gpu/RelWithDebInfo/

nv2a-gpu-combiner-test.exe passes; analysis/gpu-combiner.log records the result

and gpu-combiner-build.log the native build. build-abi60.log confirms the game

build with the new unused HLSL module. All commands and runtime probes are

terminal.



For the hardware path, set RECOMP_HW_RASTER=1 before probe_runtime.ps1.

GPU raster fixture: build/nv2a-gpu/RelWithDebInfo/nv2a-gpu-raster-test.exe.

Control fixture: build/nv2a-pb-control/RelWithDebInfo/nv2a-pb-control-test.exe.

The rebuilt real-memory-layout semaphore fixture passes with the hardware

module linked. No process is intentionally left running.



GPU vertex test: build/nv2a-gpu/RelWithDebInfo/nv2a-gpu-vertex-test.exe.

Set RECOMP_HW_VERTEX=1 only for experiments. RECOMP_GPU_TIMING adds HW INPUT

packing/submission rows and HW SYNC diagnostics. All probes/builds/tests from

this turn are terminal; no process is intentionally left running.





## Command corruption diagnosis (abi68Ã¢â‚¬â€œabi80)



The first malformed-command capture and SDK write journals now reproduce the

failure before the parser spends its budget chasing index values as CALL/JUMP

addresses. `joined76.stderr.log` records drawing through color offset zero

before its first malformed command. That resolves to 0x80000000 and overlaps

the contiguous pushbuffer ring at 0x80002000..0x80182000. SDK tracing identifies

an actual SetRenderTarget resource with Data=0: object 0x01B5B640, Common

0x810D0002, Format 0x00011E29, Size 0x1F1FF1FF (512x512), Parent=0.

Its creation/registration remains unresolved; refusing that invalid draw is

memory protection, not a completed implementation of that render target.



The draw path now rejects an unset color offset consistently with the existing

clear path, before expensive vertex/raster work. A native regression draws a

real immediate triangle to a valid target, then verifies the same draw with

an unset target leaves physical-zero memory intact. It passes in

`test-null-surface.log`. `guard77` finished its 180-second watchdog without

parser errors, and `play78` ran 339.62 seconds without parser errors or a crash,

then was deliberately stopped after its input sequence. Left stick, RT and

right stick all reached the original SDK; movement/firing gameplay effects

are not yet established. `play78-frame-00088.bmp` shows the character standing

in the level. Report screenshots can capture incomplete frames.



SDK instrumentation is opt-in via RECOMP_SDK_WRITER_TRACE: 1 prints events,

2 keeps an in-memory journal, 3 only traces zero-address render targets.

RECOMP_SDK_JOURNAL_DUMP selects its text dump; RECOMP_PB_FAILURE_DUMP captures

the first malformed submitted segment and recent parsed headers. The parser

can invoke a registered failure callback to save the matching SDK journal.

`trace_sdk_writer.py` preserves these hooks across regeneration. A fresh-code

fixture and a second idempotent application matched the instrumented source.



Hardware submission still measured roughly 240Ã¢â‚¬â€œ352 ms per frame in play78,

which ran about 1Ã¢â‚¬â€œ2 FPS in complex views. RC and auxiliary constant buffers

now use DYNAMIC/WRITE_DISCARD like the vertex-program constant buffer.

The actual-GPU raster fixture passes after this change

(`gpu-all-constants.log`), including mutable shader state and readback.

Builds abi68Ã¢â‚¬â€œabi80 succeeded. All probes through play78 are terminal.

`constants80`, session 76061, is currently active with a 300-second watchdog,

scripted gameplay inputs and opt-in completed-flip capture. No playable-build

claim has been made; actual controls, correct rendering and useful speed

still require verification.





## Depth-only root cause and presentation (abi81Ã¢â‚¬â€œabi87)



The zero-address color header is deliberate original game code, not a failed

allocation: sub_000F3AF0 constructs it at class+0x40 with Data=0, and

sub_000F5820 uses that header when rendering its shadow pass (SetRenderTarget

caller return 0x000F5887). depth86 confirms color_mask=0, depth_test=1,

depth_mask=1, zeta=0x00C5AB00, pitch=2048 and 512x512 clip.

The real corruption was unconditional hardware color readback: copying an

unchanged GPU color snapshot back to dummy physical-zero backing overwrote

CPU command/USB writes made after the snapshot. No low-memory reservation or

address remapping was implemented; page zero also contains USB structures.



The hardware backend now tracks color and depth writes separately. A

color-disabled pass performs its depth work without copying unused color back;

a depth-disabled pass likewise preserves CPU changes to unused depth. The

kernel again permits the original zero-color depth-only pass, rejects zero

when color writes are enabled, and does not select depth-only targets for

presentation. Actual-GPU regressions demonstrate depth changed while a CPU

color mutation survives, and the converse for color-only work

(`gpu-depth-only.log`). depth86 ran its full 240-second watchdog without a

parser error or crash while these original shadow passes executed.



Presentation and completed-flip capture now happen on FLIP_INCREMENT_WRITE,

not on an optional later FLIP_STALL. The native fixture verifies an increment

completes one frame, a stall does not count it again, and a second increment

completes the second frame (`test-presentation.log`). Scheduled flip capture

is available through probe_runtime.ps1 -CaptureFrom <seconds> -CaptureFor

<seconds>; it creates and removes a fresh probe-owned flag during probe.py's

normal polling loop. present83-frame-00208.bmp is a completed frame.



Texture profiling found repeated decoding (about 1.3 ms/upload) with the

128-entry FIFO cache. The texture cache is now separate from the shader cache,

512 entries, with least-recently-used replacement and the same exact raw and

palette byte freshness checks. Native raster/coherence fixtures pass

(`gpu-texture-lru.log`). Some views improved to 5Ã¢â‚¬â€œ13 FPS and submission

20Ã¢â‚¬â€œ42 ms/frame, but complex views remain 1Ã¢â‚¬â€œ2 FPS and 120Ã¢â‚¬â€œ160 ms submission.

Textures still use mip level zero and some character geometry/rendering is

incomplete. Movement/aim/fire reach the original SDK, but full gameplay

verification remains unfinished.



Probes through depth86 are terminal. The optimized production build

brute_force_e3_abi87_opt is currently compiling (no ABI checking, generated

code /O2), recorded in build-abi87-opt.log. The goal remains active and is not

achieved. Latest non-optimized build: brute_force_e3_abi86.exe.



## Current gameplay verification (abi90-abi92)

Completed control90 captures verify walking toward palms, camera turning, and
firing (ammo 48 to 41). Skip Basic Controls with Y after the modal appears.
Complex views remain slow (around 2-5 FPS). target91 moves farther and fires
(ammo 33), then enters unresolved 00062FE3 from 00062FD0 and corrupts its stack.
Original disassembly identifies 62FE3 as a shared epilogue and 62FED as a
false function split. Explicit coalescence number 50 joins 62FD0..630A1.
The merged generator output replaces both entries; the interior dispatcher
entry and unresolved epilogue stub are removed. Native return-path tests
check both immediate and predicate-rejection returns, ESP and saved registers.

Managed depth uploads persist across synchronization. Kernel depth revisions
change on creation, CPU clear and CPU fallback after synchronization.
Unmanaged callers still refresh after synchronization. The GPU raster test
passes 235 draws, including rejection across syncs and acceptance after a
CPU clear revision. The new optimized game executable still needs testing.


abi92 exposed a startup error in the targeted dispatcher edit: removal of
the interior entry left a stale literal entry count. Both generator and
current dispatcher now derive count from sizeof the actual array. The native
dispatch test passes all five checks. abi93_opt builds successfully and is
running depth93 (360 seconds), with farther movement, fire, aim and pause.
The additional CPU depth fallback invalidation accounts for writes whenever
the existing software path enables the write mask. Presentation/semaphore
regression passes after this change. No playable claim yet.


## Later combat path (abi93-abi95)

The depth93 run passes the former 62FE3 freeze, enters an encounter scene,
returns to gameplay and verifies ammunition 48 to 27 to 4, health damage and
the Options pause menu. It crashes at 329.18 seconds when message handling
enters five unresolved interior labels in 113570. Original x86 confirms
113570..113977 is one 363-instruction hash dispatcher; 13 detected interior
entries have no independent callers or frame prologue. Coalescence 51 joins
them and removes five unresolved stubs. A native test uses mocked handler
bodies with original RET immediates to check all 29 hash cases plus two
unknown cases: all 31 preserve ESP, EBX, ESI, EDI and caller EBP. This checks
dispatch ABI, not handler semantics.

The parser now waits for GPU completion at segment end only when a semaphore
release is pending. Frontend command consumption still completes first; CPU
raster/clear, target transitions, semaphore method and presentation retain
their synchronization. Native parser tests verify no unfenced waits, fenced
completion, and no publication after malformed input. Semaphore tests pass.
abi95_opt builds and segment95 is running (420 seconds) to verify performance
and later combat behavior. There is no final playable claim yet.


## Stable death/replay, mipmaps and audio output (abi95-abi98)

segment95 completes 420.18 seconds by the intentional watchdog (exit 3),
with no crash, PB error/budget, or repaired interior stubs. Actual completed
captures reach Squad Killed; a live X input accepts Replay Mission and
level loading begins again. The death/message-dispatch fix is verified.

abi97 adds stored mip-level decoding, mip/min/mag filter selection and
managed-depth completion. The renderer completes GPU fences via color
readback or an event query while retaining managed depth; full CPU-access
sync still downloads depth. Native GPU tests pass 240 draws, including
depth-only completion, later CPU download, color coherence, colored mip
selection, lower-mip freshness and nonmip filtering. The mip97 run remains
active at this checkpoint, with sharper captures and 20-23 FPS in some
combat views; slow complex views remain near 8 FPS.

Audio review identifies hardware DSP PCM erased by monitor_frame and
1024/2048 samples submitted each 256-sample delivery interval. The monitor
now retains the eight original 32-sample hardware slices in a private
256-sample output copy, mixes software voices there, and submits 256 to
XAudio2 or waveOut. Three native mixdown/monitor tests and the XAudio2
short-buffer/ring regression pass. abi98 builds, but game audio is not
yet verified. Optional RECOMP_AUDIO_TRACE and RECOMP_AUDIO_DUMP record
nonzero PCM evidence; dumping uses raw 48 kHz signed 16-bit stereo.

A real native framebuffer test verifies keyboard accept/start/move/fire,
key release, focus-loss clearing and window close. The launcher now sets
RECOMP_KEYBOARD=1 (previously missing); its default binary remains old
until the new build is verified. No full playable claim yet.


## Audio-enabled final candidate checkpoint

Audio98 completes 420.14 seconds with no crash, nonzero PCM and native
XAudio2 initialization. Its early scripted inputs reached the wrong menu;
live navigation enters Campaign and its movies. ffprobe verifies the first
mission briefing MB01 is 132.732733 seconds. Campaign_Intro (48.35s),
Tex_Intro (47.91s) and MB01 total roughly 3m49s; this is real source content,
not a hang.

The final candidate brute_force_e3_prototype.exe is running candidate, a
900-second audio-enabled test using live navigation. At 452s it reaches
Basic Controls. Live Y, movement, camera, firing and Start are verified in
completed captures: ammo 48 to 36 and the Options pause menu. Recent PCM
has nonzero samples, RMS 581.6 over a 5-second window with no clipping in
that window. Continue movement/combat, stability and launcher validation
before marking the goal complete.

The launcher defaults to the final candidate and audio on; it enables
keyboard input and quiet mode. Quiet mode suppresses per-transfer messages
and draw-counter chatter without changing command execution. The separate
brute_force_e3_prototype_quiet.exe builds with this logging option; launcher
exit still needs end-to-end verification. README has current controls and
run instructions, with honest 8-23 FPS and movie/DSP limitations.


## Later audio combat crash and coalescence 52

The candidate run crashes at 638.71 seconds after verified ammo 48 to 36 to
35, pause/resume, camera turning and movement with nonzero audio. A direct
stub 000419CC precedes two DirectSound SG updater faults (343C61: null
queue state and an out-of-range write). The wave-bank open status 0 is
success, not failure; the cached tutorial-en.xwb SHA-256 exactly matches
the source TGZ member. Do not treat this as a missing asset problem.

Raw 418E0..419D5 is one function with shared epilogues, saving EBP/ESI/EDI
and eight local bytes. 4195C and 419BB have no callers/prologues; 419BB
starts at an interior CALL. Coalescence 52 joins both and removes the
419CC stub. The flag and float early-return native tests pass, preserving
ESP, saved registers and x87 stack depth. abi99 builds. abi100 adds an
opt-in DirectSound SG queue diagnostic and builds. trace_audio_queue.py
is included in recompile.py and is idempotent. Keep its include after
recomp_funcs.h so RECOMP_TLS and register macros are defined.

combat100 is running a 900-second audio-enabled, quiet-mode regression
with spaced menu inputs and later movement/combat/pause. Optional
RECOMP_AUDIO_QUEUE_TRACE logs 343C61 object/root/head/tail/SG-base state.
Confirm whether audio faults persist after the known stack repair before
changing RAM aliases or adding guards. The 128 MB normal RAM and 64 MB
contiguous aperture are existing separate mappings; do not unify low
normal RAM with contiguous storage (would overwrite the XBE). Launcher
exit validation remains pending. No playable-complete claim is justified.


## Launcher window visibility repair (abi101)

The end-to-end launcher fixture found the owned framebuffer hidden by
STARTUPINFO from Start-Process -WindowStyle Hidden. MainWindowHandle was
zero, so window closure also failed to end the game child. fb_present now
explicitly shows a requested window with SetWindowPos(SWP_SHOWWINDOW).
abi101 builds. The real launcher test verifies visibility=True, a nonzero
MainWindowHandle, closure and owned-child exit; the launcher returns success.
The native framebuffer input fixture also asserts visibility and passes.
This change affects only the requested window, not Xbox execution.
combat100 remains active and is testing coalescence 52 with audio diagnostics.


## Fifteen-minute audio pass and final seed repair

combat100 completes 900.35 seconds by intentional watchdog (exit 3), with
zero crashes, parser errors, parser-budget errors and direct stubs. It
opens tutorial-en.xwb and tutorial.xwb, verifies movement, aim, fire,
pause/resume, medkit input and ammo reserve increase to 600. The earlier
DirectSound queue faults do not recur after coalescence 52. Last audio
trace has 38,207,488 stereo samples delivered and nonzero PCM.

Static audit of forward branch stubs identifies five additional real shared
epilogues. Coalescences 53-57 join 41D90..41F18, 100B60..100BBB,
105810..105C63, 1530D0..153137 and 186390..186448. All interior starts have
no independent callers/prologues; their exits restore the owners' saved
registers. A native seven-path test passes ESP and register preservation.
The decoder rejected an initial 105C64 bound; exact RET end is 105C63.

The 240.18-second release smoke has no crash but reaches a missing type
getter 157D40 through a tail jump from 158440. Raw code proves it is an
independent aligned lazy initializer with its own RET, rather than an
epilogue. Added an observed seed, reran disassembly/identification/ABI and
prepare_functions, generated the function and inserted its sorted dispatcher
row. Native test verifies real type object return, CRC E3307376, original
flag bits, one atexit registration, ESP and saved registers. The final named
prototype executable rebuilds; release-final is now running a 300-second
startup/gameplay smoke with this exact artifact. Keep the goal active until
that check and final packaging/launcher audit finish.


## Current outstanding gates: exact-build speed and intermittent startup

The final named artifact contains all 57 coalescences and the real 157D40
class getter. The getter seed reruns disassembly, function identification,
ABI analysis and preparation successfully. Its native initialization/atexit
return test passes. release-final completes 300.31 seconds without crashes
or direct stubs, with gameplay captures, but sustained views now measure
around 3-6 FPS. The broad playability objective remains unproven because
this speed is too low; do not reuse earlier 8-23 FPS as a claim for this
exact artifact.

performance completes by watchdog at 300.32 seconds with one initial flip
and no game draws. It is an intermittent startup stall, not a parser crash:
main thread waits in an asset stream read through caller 2DA517, repeatedly
calling synthetic FE0001DC (thunk slot 119, ordinal 99 KeDelayExecutionThread).
Other runs still reach gameplay; investigate the file worker/stream reader.

Added sample_cpu.py, using scoped Win32 thread snapshots, CPU-time selection,
Suspend/GetThreadContext/Resume and local PDB symbols. Separate exec sandboxes
cannot see the running probe's threads; sampling is now launched inside
probe.py via --sample-from/--sample-for (PowerShell SampleFrom/SampleFor).
Each probe writes its owned PID/executable to analysis/<name>-process.json.
The first embedded sample had 2004 zero addresses because RIP offset was
wrong. AMD64 CONTEXT RIP is offset 248, not 256; corrected. Do not use that
invalid sample as performance evidence.

cpu-profile-valid is running the exact prototype exe for 240 seconds with
sampling from 145 for 12 seconds, using the corrected layout. Tool exec
handle is recorded in the conversation. CPU-profile earlier completes
260.34 seconds with slow gameplay, no crash. No goal-complete claim is
justified until speed and reliable startup are resolved.


## Corrected CPU profile and immediate PFB flush (abi104)

The corrected cpu-profile-valid sample has 1605 valid addresses, 1510
(94 percent of the busiest thread) in D3D sub_0039EED0. Raw x86 identifies
the PFB flush kick: set bit 10000 at GPU base+100410 then spin until clear.
The old NV2A_ACK polling thread creates a scheduler delay on every resource
lock. xemu's current pfb.c returns no flush pending for NV_PFB_WBC; host
normal RAM is already coherent.

Added a PFB 4KB PAGE_READONLY write trap in xbox_memory_layout.c, using the
existing AC97 single-step approach. It executes the original write, applies
MemoryBarrier, clears only bit 10000, and restores page protection before
the following guest read. Other PFB values remain intact. The poll loop
skips the PFB ack when the trap is active, while all GPU parsing, execution,
GET and semaphore completion ordering stays unchanged. Opt-out is
RECOMP_NV2A_PFB_POLL. Shutdown removes the handler. Native semaphore test
now checks immediate MOV/OR flush acknowledgement and neighboring PFB state;
it passes along with semaphore/presentation tests. abi104_opt builds.

flush104 is running a 300-second regression (exec handle 19339), but it has
stalled during startup at three flips, before any game draws. Its sampled
busiest thread is nv2a_ack_thread (mostly ZwDelayExecution sleep), so that
sample is not rendering performance evidence. The intermittent asset-read
stall remains the next necessary investigation. Do not claim a frame-rate
improvement before a run actually reaches gameplay. Previous no-progress
classification: this turn makes concrete progress via valid profiling, an
implemented register handshake, passing native checks and a live regression.




## Continuation: NV2A notifications, candidates 105-108



All-thread profiling of stalled 104 found the main thread blocked on a graphics event. Original D3D BlockOnTime emits NV097 NO_OPERATION parameter 5 and waits on its ISR-owned event. Added opt-in RECOMP_NV2A_NOTIFY dispatch through the existing timer guest stack, ordered after GPU completion. Nonzero methods populate PGRAPH trapped method/data/source and raise the hardware interrupt; original ISR and DPC perform event delivery. Read-only MMIO write traps implement W1C for PMC/PGRAPH/PCRTC and deassert PMC summary when child pending bits clear. Native semaphore fixture now verifies raise, selective acknowledge, zero-write preservation, neighboring registers, summary deassertion, and existing ordering/presentation. Native tests pass.



105 exposed optimized-away exception-handler state; fixed with volatile TLS in 106. 106 exposed persistent PMC summary causing DPC looping; fixed in 107. 107 loaded tutorial but later stalled; did not promote. 108 replaces polling delays with wake/completion events and waits until the original ISR/DPC acknowledges the trapped method. It is undergoing gameplay profiling. Named prototype executable and default launcher have not been updated; goal remains incomplete. Original assets are unchanged.





## Ordered notification checkpoint: 110-112



108 and 109 both stalled in D3D BlockOnTime with PUT=GET, notification event SignalState=0, PGRAPH/PMC pending=0. 109 opt-in bounded event history confirmed repeated correct reset/signal/wait cycles followed by a final reset and wait with no subsequent signal. Added explicit completed-prefix semaphore publication before a software interrupt (110), preserving future batch state. This is required GPU FIFO ordering: an original ISR must observe prior completed releases. Native tests verify prefix publication and that a later rejected suffix does not undo completed writes.



110 completed a 240-second probe with no crash, unresolved direct stub, or parser error; tutorial progressed through HUD gameplay with movement, aiming, and firing (ammo 48 to 39). It stayed around 5-7 FPS in complex views. Valid all-thread profile showed main mostly blocked waiting on GPU and 27 percent of GPU-thread samples in linear diagnostic method-table lookup. 112 replaces that lookup with a direct 8x2048 hardware-domain index while preserving report order/counts; parser control tests pass. 111/112 also mirror hardware COLORCLEARVALUE and ZSTENCILCLEARVALUE because original software interrupt methods read them. Native register fixture passes. 112 exact candidate is under gameplay/performance validation. Named launcher artifact has not been promoted. Goal is not complete.





## Concurrent PFB checkpoint: 113-114



112 advanced to gameplay but stalled. Its valid 190-second all-thread sample found main in sub_0039EED0 (PFB flush), timer/DPC in sub_0039F140 (same PFB flush), and GPU consumer waiting for software notification completion. The writable-window single-step PFB trap can miss another thread's write; the polling fallback cannot run while the consumer awaits its ISR. Replaced PFB single-step trap with PAGE_NOACCESS plus existing platform/mmio_decode.h decoder and an independent atomic register file. Flush bit is always cleared at each write; no writable aperture window exists. Existing neighboring-register checks and 30,000 flush loops across three simultaneous native threads passed.



113 retains a 32-bit color GPU copy after readback when a byte-exact CPU snapshot still matches before the next draw, avoiding redundant upload. CPU mutations invalidate retention; 16-bit targets retain previous quantization path. Native raster coherence tests (242 draws) passed, including two new CPU-modified preserved-channel checks. 114 combines that optimization with the concurrent PFB fix. Its exact gameplay probe is underway. No promotion or completion claim yet.





## 114 verification and remaining GPU load gate



coherent114 completed its intentional 300-second watchdog run without an AV, direct stub, or parser error. It reached HUD gameplay and initially ran complex views around 13-18 FPS. After movement into another view, FPS intermittently collapsed to 0.2-2 while nvidia-smi reported GPU utilization 100 percent (RTX 4070 Ti, 3066/12282 MiB VRAM, 66 C). This is not a completion or promotion. Its 190-second profile saw the GPU consumer mostly blocked in a native wait, NVIDIA worker mostly NtGdiDdDDIGetDeviceState, and game thread waiting on the original graphics event; the earlier concurrent PFB spin is gone.



A controlled comparison vertex_cpu114 is running the same 114 executable with RECOMP_HW_VERTEX unset, GPU raster still active, same scripted menu/movement/fire/camera/pause flow, 300-second limit, sampling at 190, captures 100-290 seconds. Its exec session is 18448; do not restart it on an observation timeout. Inspect completed frames by capture time; reports after capture flag ends can be partial frames. Next gate: distinguish raw hardware vertex output from color retention in the view-dependent GPU-load collapse; then verify exact audio-enabled gameplay and promote named launcher artifact only after success. Launcher still uses the previous named prototype and has not been changed to enable RECOMP_NV2A_NOTIFY. No probe apart from vertex_cpu114 is intentionally active.





## Controlled comparison prepared: 115



vertex_cpu114 remains active (session 18448) and also shows severe slow views, so raw hardware vertices alone do not explain the collapse. Added RECOMP_HW_COLOR_RELOAD to disable color snapshot retention in a fresh process, restoring the earlier reload behavior while preserving concurrency, interrupt, prefix-release and method-index fixes. Compiled brute_force_e3_abi115_opt.exe; it has not been run yet. Next: harvest vertex_cpu114 at its 300-second intentional watchdog, then run 115 with RECOMP_HW_COLOR_RELOAD=1 and RECOMP_HW_VERTEX=1 using the same scripted route. Investigate actual GPU workload if reload comparison also collapses. No other probe is intentionally active. Do not promote or claim goal achieved.





## GPU latency investigation: 115-118



vertex_cpu114 completed 300 seconds, also severe slow views. reload115 restored the earlier color reload path and reproduced the slowdown; intentionally stopped at 279.73 seconds (not crash). Neither hardware vertex execution nor color retention alone explains it. Added opt-in RECOMP_HW_GPU_TRACE: D3D11 timestamps around up to 64 individual draws per completed batch with disjoint clock validity, draw/texture/depth state, and synchronization CPU duration. Native raster tests with tracing enabled passed. 116 completed 240 seconds; 117 completed 300 seconds; 118 completed 240 seconds, no AV. All still had variable severe FPS, no promotion.



118 trace recorded typical slow UI synchronization waits 52-56 ms while valid sampled GPU drawing totaled only 0.01 ms. This is latency at completion/readback rather than expensive sampled drawing. A read-only NVIDIA NVAPI DRS query (616.92) found no configured base/global public foreground, idle or battery FPS settings (status -160, setting not found; not proof of effective defaults). No driver settings changed. These prior probes had no RECOMP_FB_WINDOW. Next comparison window118 enables the real visible game window and keyboard, same exact artifact and route, 300-second watchdog, exec session 6324 (confirmed live by probe launch tool). Do not restart on an observation timeout. Named launcher remains previous artifact and no NV2A_NOTIFY configuration. Goal incomplete.





## Nonblocking readback candidate: 119



window118 completed its intentional 300-second probe, no AV. A visible window improved early views but severe slowdown still reproduced after movement; it is not a fix by itself. It is terminal/harvested. No NVIDIA driver settings were modified.



Added native readback helper using D3D11_MAP_READ + DO_NOT_WAIT. If the resource is still in use, submit queued copy once with Flush and retry until real GPU availability; publish pixels/fences only after successful map. This replaces the blocking Map path for color/depth staging readbacks without removing GPU work. Unexpected failures now report HRESULT and device removal reason. Native 242-draw raster/coherence/mip suite with GPU timestamps enabled passed. Built abi119_opt. readback119 is running a visible-window 300-second scripted gameplay comparison, sampling at 190, capture 100-290, tool session recorded in current conversation. No other probe is intentionally active. Named launcher not promoted and goal remains incomplete.





## 119 gameplay checkpoint and exact audio test



readback119 completed 300.41 seconds with intentional watchdog exit 3, no AV, no direct stub, no PB parse error or readback failure. Visible HUD gameplay stayed responsive after scripted movement, rotation, firing and pause/resume. Captured completed frame 02542 shows ammo 17/500 (started 48), visible combat health damage, reticle/radar/player and terrain. Complex views around 8-17 FPS, ending around 8.3 FPS, no earlier multi-second collapse. Native readback still waits for real GPU availability and preserves colors/depth. This is concrete progress but not promotion: exact audio test remains.



audio119 is now live (exec session 37139, confirmed live at launch), same abi119_opt.exe, visible framebuffer/keyboard, audio on with DSP doorbell 0x83390810, quiet, GPU timing/trace disabled, PCM dump analysis/audio119.pcm plus AUDIO_TRACE. 900-second intentional watchdog, completed-flip captures 420-870 seconds. Script: Start75/95, A105, Down110, A115, Y470/490, movement510/700, fire535/590/735, turn560/780, pause620/A625, black medkit665. Confirm menu flow from frames or live input, do not assume timed navigation succeeds. Do not restart on observation timeout. No other probe intentionally running. Named prototype executable and launcher still previous configuration. Need exact audio-enabled gameplay stability, completed-frame evidence, native keyboard/window close regression, then promote actual artifact and docs. Goal active and incomplete.





## Live audio observation



Audio119 session 37139 confirmed still running by write_stdin. Around 194 seconds its native PCM monitor reported 8,290,304 stereo sample frames and 8,843,813 nonzero channel samples, peak 25,610. Brief manual completed-flip capture was enabled then removed before the probe's scheduled 420-second capture; final brief frame audio119-frame-01933.bmp shows the starfield cinematic, so the timed menu input did enter the intro sequence. Bink visual decoding artifacts remain visible and previously documented. Do not infer combat success from movie playback. Later Y470/Y490 and scripted movement/fire still need verification; use live input if navigation or cutscene timing differs. GPU timing and per-draw timestamps are disabled in this run.



## Final candidate audit and mute correction, 120-122

Audio119 completed 900.52 seconds (intentional watchdog exit 3), XAudio2 backend active, 37,715,968 stereo frames with 57,400,013 nonzero channel samples, final view about 27-28 FPS. No AV, direct stub, PB parse error, or readback failure. Completed frame audio119-frame-12153.bmp at elapsed869.97 shows Squad Killed with cash award for one Red Hand killed. Named prototype rebuilt; launcher audio-on real-window close test passed and owned child exited. Native keyboard accept/start/movement/fire/focus/close fixture passed. Named executable sections differ from119 due rebuilding, so do not claim bit-identical artifact.

replay_final (session89241) sound-hardware-disabled comparison reproduced severe slowdown, stopped416.59seconds. 120 explicit multimedia timer lease did not solve it; timer120 stopped347.43seconds and the lease/power-policy changes were removed. User-facing -Audio:false now preserves emulated audio hardware and selects RECOMP_AUDIO_MUTE. 121 muted the final PCM copy; mute121 completed300.18seconds but froze during Bink movie playback, main inside DirectSound critical-section/position-query path, so not a passing playback validation. Critical-section diagnostics show ownership moving; do not infer a permanent lock deadlock without all-thread evidence.

122 moves mute to XAudio2 mastering-voice SetVolume0 while keeping source buffers, console mixer, DSP and position progress intact; waveOut fallback mutes only its submitted copy. stdlib included for getenv prototype. Default unmuted rendering/processing unchanged. mute122 currently live in exec session69695, 600-second limit, hardware/audio enabled and output muted, scripts Start75/95,A105,Down110,A115,Y470/490,move510,fire535; capture420-590. Initial PCM source is nonzero and backend reports output muted. Confirm full gameplay before promotion. No other probe intentionally active. Goal remains incomplete.


## Master-volume mute candidate: 122

Timer120 ended347.43seconds after explicit stop; timer/power policy requests did not solve the hardware-disabled comparison and were reverted completely. Mute121 ended300.18seconds with stalled Bink movie/DirectSound position-query path. 122 mutes XAudio2 mastering-voice volume (SetVolume0), retaining original nonzero source buffers; waveOut fallback silences only its submitted copy. Native XAudio2 regressions now cover failed volume-setting cleanup and unchanged queued PCM while muted: zero failures. Source buffers and console processing stay active. Launcher -Audio:false retains AC97/APU hardware and DSP doorbell and requests master mute; normal audio unchanged.

The named prototype was rebuilt from122 source (build-prototype-122.log). mute122 remains live, exec session69695, 600-second test, visible window, emulated hardware and source audio active with master volume zero. Initial intro advances with nonzero internal PCM. At around335seconds it was running about23-30FPS in the next scene. Capture420-590 and inputs Y470/490, movement510,fire535 still need confirmation. Do not restart on observation timeout. No other probe active.

Next completion gates: harvest mute122 after real gameplay; run the exact named build with unmuted audio long enough to reach combat/death, accept X Replay Mission, and confirm resumed HUD gameplay; rerun named launcher window-close/keyboard checks; update current README/STATUS and concrete artifact manifest. Audio119 already proves15min combat/death on source119, but do not substitute it for the final named-artifact audit. All original assets remain read-only; no NVIDIA driver settings modified.


## 122 mute verification and final named replay run

Mute122 completed600.38seconds, intentional watchdog exit 3, no AV, direct stub, PB parse or readback error. Master-volume mute retained nonzero PCM source and advancing cutscenes; completed gameplay frame mute122-frame-02938.bmp shows movement, damage and ammo30/500 (started48). Native XAudio2 muted buffer-integrity and failed-volume cleanup regressions passed0failures.

The exact named brute_force_e3_prototype.exe was rebuilt from122 source. final_audio_replay is now live (exec session47425, confirmed by launch tool), normal audio, visible window/keyboard, quiet, no GPU timing/query diagnostics, AC97/APU enabled with doorbell83390810. 1500-second intentional watchdog allows replay to finish even if intro movies repeat. Initial completed-flip capture460-465; use brief manual capture flags for later gameplay/death/replay evidence. Script reproduces audio119 combat path through780 seconds, X Replay at930, Y1040/1240, movement1270/fire1300. Confirm death beforeX; steer via live input when needed, do not assume script establishes success. No other probe active. Remaining gates: named audio/death/replay/resumed-HUD evidence, launcher close check againstcurrent namedbuild, refreshed README/STATUS/artifact manifest. Goal not complete.

## Exact named combat/death observation

Final_audio_replay remains live in exec session47425. Completed-flip capture
00642 (02:02:00 local) shows Tex, HUD/radar/terrain, ammo17/500 and substantial
health damage. Completed capture00800 (02:04:13 local) shows Squad Killed with
X Replay Mission available. This is the exact named122 executable with normal
XAudio2 output, not an earlier artifact or a partial diagnostic screenshot.
At02:04:08 its audio monitor had31,850,496 stereo source sample frames and
44,783,700 nonzero channel samples. Replay input remains scheduled at930sec;
resumed gameplay is still unverified. Original XBE and final executable hashes
rechecked and match the manifest.

## Replay confirmation accepted

The scheduled X930 input opened the original restart confirmation dialog
(completed capture01177). Appended live A600 was accepted at controller
time1011547ms (log lines23173-23174); completed capture01488 shows the
intro movie running again. The movie retains documented Bink decoding
artifacts. This proves restart acceptance/loading, not resumed gameplay.
Session47425 is still confirmed live; keep waiting for the HUD and test
movement/firing after it returns.

## Exact named replay gameplay verified

Completed frame02285 shows resumed gameplay after replay with full health
and ammo48/500. Live movement at1345312ms triggers the next original tutorial
scene; another Y skips it. Completed frame02741 then shows the player in a
different position with ammo31/500 after actual firing, HUD/radar/terrain
intact. Replay, movement and firing are verified on the final named artifact.
The1500-second stability run remains live in exec session47425; terminal
log audit and refreshed launcher closure check are still pending.
