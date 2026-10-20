# Brute Force E3 prototype recompilation

GET progress is now enabled by default on the integrated v0.14 build, after
normal-barrier gameplay/death/replay qualification. The named prototype uses
candidate157; see [GET default promotion](GET_DEFAULT_PROMOTION.md) for the
results and rollback. Coalescing and IRQ gating remain off. Current gameplay
performance and audio quality still need measurement. Assets and generated/build
outputs must be supplied/regenerated locally as described below.

The first combat profile keeps release synchronization as the main performance
lead; see [combat and audio observations](COMBAT_PROFILE_158.md) for the measured
wait split, replay timing check and remaining qualification limits.

The five coalesced GET pairs are complete. The follow-up
[sync accounting and rendered comparison](COALESCING_159_QUALIFICATION.md)
records the shared visual defects, strict input-route failures, readback bytes,
CPU-clock correction and lean audio/performance evidence. Coalescing remains
experimental.

This project targets the extracted prototype supplied at
`D:\Downloads\Brute Force E3 Prototype`. The retail build must use a separate
project: its function addresses and generated code will differ.

The executable identifies itself as Brute Force, title ID `0x4D53001E`, debug
type, dated `2003-04-10 00:02:08 UTC`. It uses XDK 5233 and D3D8LTCG. The
guest entry point is `0x00327BEF`. The folder name alone does not establish
which E3 presentation this build belongs to.

## Generate

From the toolkit root, with Python and Capstone installed:

```powershell
python games/brute_force_e3/recompile.py 'D:\Downloads\Brute Force E3 Prototype'
```

This checks the executable's SHA-256 before copying it to the ignored `input/`
directory. All pipeline stages use project-specific directories in `analysis/`.
Disassembly includes the separate executable library sections, followed by
function identification, ABI recovery, and C generation in chunks of 250
functions. Generated game code and input files are ignored by Git.

## Build

```powershell
.\games\brute_force_e3\build.ps1
```

Use Visual Studio's bundled CMake if the CMake on PATH does not support this
generator. MSVC 2022 Build Tools and the Windows SDK are installed on this machine.
The script configures explicitly and invokes MSBuild from that installation.
Automatic regeneration is disabled because it stalled during initial setup;
run this script again after regenerating code to refresh the source list.
Build the current prototype candidate with:

```powershell
.\games\brute_force_e3\build.ps1 -Optimize -OutputName brute_force_e3_prototype
```

Use `RelWithDebInfo`: some runtime libraries force `/O2`, which conflicts
with the runtime checks enabled by the toolkit's Debug configuration.

## Run

```powershell
.\games\brute_force_e3\play.ps1
```

The launcher enables hardware rendering, audio, keyboard input and a game
window. It reads the original extracted game folder directly and writes saves
under this project's `saves/`. Pass `-GameDirectory 'D:\Your\Game'` to select
another location containing this exact prototype. Pass `-Audio:$false` to
mute output while keeping the console's audio hardware active. Keep the PDB
beside the executable for crash diagnostics. GET progress defaults on; use
`-DisableGetProgress` or `RECOMP_NV2A_GET_PROGRESS=0` for rollback. The monitor
is off by default; qualification runners enable it explicitly.

Press Enter at the Start screen, choose Campaign, then Quick Campaign. Wait
for loading and the introductory sequence. When Basic Controls appears,
press S (Y) to skip tutorial menus. The three campaign intro movies total
about 3 minutes 49 seconds with audio enabled.

| Action | Keyboard |
| --- | --- |
| Start / pause | Enter |
| A / accept / clone squad | Z |
| B / back | X |
| X / replay mission | A |
| Y / skip tutorial menus | S |
| Menu navigation | Arrow keys |
| Movement | NumPad 8 / 2 / 4 / 6 |
| Camera | I / K / J / L |
| Fire | 3 |
| Left trigger | 1 |
| Black / medkit | E |
| White | Q |
| Mission objectives / Back | Backspace |

An XInput controller also works. Keyboard input applies while the game window
has focus. Close the window or press Ctrl+C in the launching terminal to exit.
Logs are written to `analysis/play-<timestamp>.*.log`.

Candidate156 passed a 25-minute audio-enabled normal-barrier run with GET on:
rendered gameplay, movement, aiming, firing, combat damage, Squad Killed,
mission restart, and movement/firing after replay. Candidate157 changes the GET
default and passed separate default-on and explicit-off probes. The named
prototype is byte-identical to candidate157 and passed owned-window launch/close
verification. See `GET_DEFAULT_PROMOTION.md` for the exact qualification chain.

This is an experimental playable build. Current gameplay performance has not
been qualified; older builds measured roughly 8–28 FPS in selected scenes.
Bink movies have decoding artifacts, and the
audio DSP uses passthrough mixing. The full campaign, multiplayer and a
physical controller have not been exhaustively tested. The test used scripted
controller input and native keyboard fixtures.

At Squad Killed, press A (Xbox X) for Replay Mission, then Z (Xbox A) to
accept the restart confirmation. Intro movies repeat. Tutorial prompts may
reappear after movement; press S (Xbox Y) to skip them.

## Development history

The following checkpoints describe earlier builds. Current launch instructions
and verification status are above.

### Initial bring-up result

The native `RelWithDebInfo` executable builds successfully. Generation lifted
26,342 functions into 2,821,285 lines of C in 106 chunks. It also emitted 298
unresolved-target stubs and reported 90 unimplemented instruction occurrences.
Those counts are diagnostics, not evidence that every function is correct.

Two toolkit fixes were required: carry tracking for arithmetic followed by
`LAHF`, and mapping direct `Partition1\\TDATA`/`UDATA` paths to the same save
directories as T:/U:. The carry regression and existing LAHF tests pass; the
native save-path regression checks roots, descendants, component boundaries,
and raw partition devices.

The first boot exited to the dashboard because opening TDATA failed. After
the path fix, TDATA/UDATA creation succeeds. Recovering the GPU ISR at
`0x003A4C00`, enabling title event objects and pushbuffer execution, and
mirroring the device completion fence gets through D3D startup and the first
framebuffer clear. Recovering the file worker at `0x0028F620` lets asset reads
complete: the next probe reaches common texture and material loading.

That probe exposed an unresolved indirect call to `0x000A5620`, previously
merged into a neighboring function. The measured entries are recorded in
`seed_functions.json` for regeneration. USB MMIO handling is connected to the
host behind `RECOMP_USB`. Reads are sampled by default; set
`RECOMP_FILE_READ_TRACE=1` for complete file-read diagnostics.

Bring-up probes currently use `BRUTE_FORCE_E3_FENCE_MIRROR=1`,
`RECOMP_PB_EXEC=1`, `RECOMP_VBLANK=1`, and `RECOMP_TITLE_KEVENTS=1`.
`RECOMP_FB_DUMP` selects a filename prefix for captured framebuffer BMPs.
The splash screen renders continuously, with vertex-program execution and
textured draws. After recovering the XPP callbacks, USB controller reports
reach the game, and Start/A advance to the menu. Menu panels and text render.
Campaign setup and Quick Campaign both reach the
tutorial's 3D scene. Terrain rendering, the transition into player control,
audio, and gameplay are still unverified. This is not yet a playable build.

The software NV2A executor now respects the game's clear rectangles. The
letterbox clears previously erased the entire color and depth surfaces,
preventing the world from appearing. Homogeneous clipping uses the window
bounds and configured depth range, preserving exact depth for unchanged
triangles. Position shaders that write only XYZ retain the default W of one.
Native regressions cover partial color/depth clears, clipping across view
planes, and the shader position defaults. Large index batches now have room
for 65,536 entries and preserve full-width draw-array indices.

For this prototype, `RECOMP_USB_HC=0`, `RECOMP_USB_NDP=4`, and
`RECOMP_USB_PORT=2` produce the game's "Controller 1 ready" confirmation.
`probe_runtime.ps1` supplies these settings for bounded diagnostic runs;
its default script selects Quick Campaign. A probe can be stopped by creating
its initially absent `analysis/<name>-stop.flag` file. Audio remains experimental.
The observed DirectSound command doorbell is guest VA `0x83390810`; supplying
`-Audio -DspDoorbell 0x83390810` acknowledges commands through the toolkit's
stub DSP. This gets through initialization. Recovered Bink callbacks and a
native replacement for the Xbox MMX probe let startup advance through the
movie files. Captures remain black, and movie teardown raises an invalid
host-handle exception during a worker wait. DSP effect processing, audio
output, and video presentation have not been verified.

`manual_functions.json` excludes the original CPU probe at `0x00330100`
from generated bodies. `src/recomp_cpu.c` preserves its cache and disable-flag
behavior while reporting the guest CPU's MMX capability. The native CPU
regression checks its result, register preservation, and cdecl stack cleanup.

`build.ps1 -Optimize` enables compiler optimization for the generated game
code. Diagnostic builds can combine this with `-AbiCheck`. The current
unoptimized tutorial probe runs at roughly three frames per second; its
letterboxed camera sequence has not yet reached verified player control.

`coalesce_functions.json` records menu and object visitor methods whose detected
bodies ended at internal branch labels. Those false boundaries skipped the
methods' register and stack restoration. `prepare_functions.py` corrects only
the named interior entry classifications; the generator then verifies complete
instruction coverage and CFG ownership before merging each body. Build with
`-AbiCheck` to include runtime register and stack diagnostics.

Set `RECOMP_STUB_TRACE=1` to log the first execution of each unresolved direct
call. This exposed three omitted startup constructors and the menu text-drawing
method at `0x001151C0`; their measured entries are in `seed_functions.json`.
`trace_stubs.py` adds these optional diagnostics after code generation.

Tutorial loading needs the devkit configuration, which is now the default.
`BRUTE_FORCE_E3_RAM_MB=64` selects retail RAM for comparison probes. Mapping
extra RAM alone was insufficient because
`MmQueryStatistics` still reported retail memory. The runtime now reports the
configured amount; the next tutorial probe no longer hits the observed arena
allocation failures. Loading then exposed a direct-call stub at `0x0016EC00`,
recorded in the seed list. Mission gameplay remains unverified.

With that routine restored, loading progressed to an unresolved callback at
`0x0023A790` and a split epilogue in the object visitor at `0x0017EB40`.
The former is recorded as an observed entry; the latter uses the same CFG
coalescence workflow as the menu repairs.

Later probes reached main-thread scene processing. Missing callbacks at
`0x002EDFE0`, `0x002EE170`, and `0x002F6EF0` led to checking the dynamic tables
initialized by `0x002EE190` and `0x002F7030`. Their other missing entries are
recorded as static evidence in `table_functions.json`. Observed update entries
and seven independent, padded tail-call targets are recorded separately by
their evidence in the seed files. These repairs still require gameplay checks.

`RECOMP_LOADER_TRACE=1` enables bounded entry diagnostics around tutorial object
copies and allocation-failure reports. It is intended for bring-up probes.

Local diagnostic files are `analysis/boot.stdout.log`,
`analysis/boot.stderr.log`, and `analysis/boot_result.json`. The executable and
PDB are in `build/RelWithDebInfo/`.

The latest startup repair honors PsCreateSystemThreadEx CreateSuspended. Bink
initializes its worker events after creating the suspended thread; running it
early exposed an invalid wait value. The corrected run reaches the title screen
with audio enabled and survives its 140-second watchdog. Tagged native handles
also have wait lifetime leases with a native regression test. Movie converters
produce RGB pixels, but normal flip captures remain black; presentation ordering
is still under investigation. Neither audio output nor playable gameplay has
been verified. See STATUS.md for current probes and exact remaining failures.

Movie ordering is now repaired through executed GPU semaphore releases rather
than the blind fence mirror. Native ordering/bounds checks pass, and normal
flip captures show video, with decoding artifacts still visible. Terrain now
appears in the tutorial intro. SDK traces confirm sticks and triggers, but
player control remains unverified. Indexed vertex outputs are reused within
each draw when the shader cannot write constants; native guard/collision tests
pass. Software rasterization still limits the scene to roughly 1–3 FPS. A
hardware D3D11 render/readback probe passes on the RTX 4070 Ti, but the game
has no integrated hardware raster backend yet.

The new kernel combiner HLSL generator matches the existing software arithmetic
on the actual GPU across 40 configurations and 2,560 varied inputs, including
0–8 stages and final combiners. It uses a uniform constant buffer for colors.
abi53 builds with this module, but the game's draw path still uses software
rasterization; target/depth state, textures, and CPU/GPU surface coherence need
integration before this can improve frame rate. The standalone shader test
is not evidence of playable gameplay.

abi60 connects opt-in hardware rasterization (RECOMP_HW_RASTER=1) to original
programmable batches, with CPU vertex interpretation retained. Actual GPU tests
cover depth/color/blend/scissor, textures and CPU/GPU coherence. Pushbuffer
compiled jumps, inline SDK NOP records, and split packet parameters are now
handled with native control fixtures. The latest 75-second tutorial run has
no parser errors/crash and measures about 4–11 FPS depending on view. This is
still below the playable goal; native player control and sustained gameplay
remain unverified. See STATUS.md for exact gaps and probe artifacts.

abi67 adds experimental direct GPU vertex execution with
RECOMP_HW_VERTEX=1 plus RECOMP_HW_RASTER=1. The translator matches the CPU
reference on 7,424 inputs, including multi-step epilogue and relative addressing
cases. Dynamic discarded constant uploads improve measured submission cost,
but longer game runs still lose pushbuffer control flow and stall. Exact SDK
ring bounds and deferred frontend fence completion are now used, without fully
resolving that issue. The CPU vertex path remains the default. No playable
build or actual player movement/firing has been verified.
