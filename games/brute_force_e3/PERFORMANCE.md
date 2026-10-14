# Performance verification

## Manual launcher correction, October 14, 2026

Controller testing exposed a launcher environment bug after campaign movies:
PowerShell's string binding changed `$null` passed to
`[Environment]::SetEnvironmentVariable` into an empty environment value. Native
`getenv` presence checks therefore enabled supposedly disabled flags, including
release coalescing, the polling PFB flush, GPU profiling and audio mute. The live
black-screen snapshot showed the main and timer threads spinning in PFB flush
waits, a pending software notify and raised IRQL. This manual launch was not the
independent epoch-only configuration used by `performance.ps1`/`probe_runtime.ps1`,
which clear flags through the environment provider.

`play.ps1` now removes disabled flags through that provider and restores absent
caller variables as absent. `test_launcher_environment.ps1` checks default and
epoch-enabled child environments and caller restoration. Corrected native
window/child closure passed as `analysis/test-launcher149-environment-fixed.json`;
the earlier closure-only test did not detect incorrect option presence.
The restarted candidate logs confirm synchronous PFB flush and unmuted XAudio2.
Manual controller gameplay verification remains with the user; the launcher fix
does not establish broader controller coverage or eliminate the known movie
stall. The candidate149 executable itself has not changed.

## Review-driven experiments, October 14, 2026

The review's first implementation stage is available behind independent,
default-off controls. The verified executable and candidate132 are retained;
`play.ps1` still selects the verified executable and clears experiment flags
unless texture epochs are explicitly requested with `-TextureEpoch`.
GPU residency, asynchronous publication and WAIT_FOR_IDLE deferral are not
enabled by this work.

- `RECOMP_HW_COUNTERS=1`: query-free cumulative synchronization attribution,
  outcomes, executor CPU time, release/segment histogram, trap waits, comparison
  bytes, vertex upload bytes and target transition dimensions. Outcome bits are
  color=1, depth=2, event=4; successful outcome0 is a no-op. Failed calls are
  counted separately. JSON reports are locked against other stderr writers.
  Comparison-byte counters sum requested `memcmp` lengths; an early mismatch
  can read fewer bytes, so these are not exact memory-traffic measurements.
- `RECOMP_HW_CPU_TIMING=1`: CPU phase timing without GPU timestamp queries.
- `RECOMP_HW_RELEASE_COALESCE=1`: defer only barriers on releases inside an
  active batch. Publication still follows real completion at segment end and
  trap flush. Standalone releases, idle, flips, target transitions and CPU
  dependencies keep their barriers. GET_REPORT also completes before its
  observable done word in this mode. `RECOMP_HW_RELEASE_BARRIER=1` overrides
  coalescing and restores every release barrier.
  CPU reads of GPU-produced vertex/index data, textures, palette data and
  pushbuffer words also acquire overlapping backing before consumption.
  Contiguous/tiled aliases are normalized for these dependencies, and direct
  host inputs overlapping private depth request a depth download. Pending
  semaphore stores remain unpublished during such acquisitions.
- `RECOMP_HW_TEXTURE_HASH=1`: hashed, collision-checked texture identity with
  bounded LRU eviction. Identity includes mip interpretation, pitch, palette
  backing and decoding callbacks. Sampling/filter state remains separate.
- `RECOMP_HW_TEXTURE_EPOCH=1`: byte validation on first use in a submitted
  segment, invalidated after ISR return and renderer writes to guest memory.
  Unbracketed native calls still validate every use. Exact color snapshot
  checks are retained; readbacks invalidate the texture epoch.

Epoch reuse is an experimental ordering assumption: CPU writes required by a
submitted command precede its PUT store. It does not track arbitrary concurrent
CPU writes; unordered writes remain a residency/access-tracking concern.

The initial behavior-neutral `brute_force_e3_perf140.exe` completed its700s
query-free fixed-view probe (`perf140_counters_queryfree_01`). Its three windows
measured50.76/52.32/51.58ms per completed frame. This is a diagnostic run, not a
speedup comparison. Counter intervals show about65.5 color readbacks/frame,
approximately59 caused by releases,3.5 segments/frame,203.7MB/frame of texture
validation and74.3MB/frame of snapshot comparisons. That clears the20 avoidable
release-readbacks/frame gate for the coalescing experiment.

The first counter logger allowed two startup JSON packets to interleave with
audio diagnostics; those packets were excluded. Its counter-only summary uses
recorded QPC/UTC calibration and adjacent report IDs, stored separately as
`analysis/perf140_counters_queryfree_01-counters.json`. Later candidates emit
atomic reports parsed directly by the monitor.

Candidate147 is recorded in `analysis/build-verification-perf147.json`, with
the exact source patch and protected build hashes retained separately. All
optimizations remain off by default. Three interleaved counter-overhead pairs
using the earlier behavior-neutral candidate143 completed:52.175ms/frame for
candidate132 versus52.046ms/frame with counters, pooled between-run SD0.127ms.
The0.129ms difference is within twice that SD; work matches within1%, and all
six completed capture sets show the same paused beach view, full health and
ammo48/500. No material counter overhead was detected in this comparison.
Candidate147 adds separately gated input-dependency guards after this run;
its optimization comparisons use candidate147 for both arms.

Candidate132's pending normal-graphics replay route completed as
`perf132_normal_replay_147gate`: 2000.31 seconds, watchdog exit3, no host timeout
or explicit stop. Captures show movement/firing (ammo22/500), squad death,
replay confirmation and intro, returned HUD with full health/ammo48/500, and
post-replay movement/firing/damage (ammo29/500). Source PCM reached83,623,936
sample frames and119,439,467 nonzero channel samples. The1640s inspection shows
no pending APU IRQ or raised IRQL. Evidence is in its `-verification.json`.
Candidate147's first release-on probe (`perf147_release_on_1`) failed the route:
completed flips stopped near103s at the title screen, leaving all timed windows
without completed frames. The harness rejected it and stopped subsequent runs.
The diagnostic snapshot/samples show the guest main thread waiting on the
graphics event and an idle executor; no D3D sync
failure was reported. This is not the baseline movie/DirectSound signature.
GET/PUT and the executor's pending TLS fence list were not captured before exit,
so the precise lost-progress cause is unresolved. Coalescing remains off; no
speedup is claimed from this failed experiment. Its off arm completed normally.
The later raw-data audit in `review/opus55/ANSWERS.md` identifies the event at
device+0x196C, signaled by software-trap action5, and executor polling samples.
The earlier attribution to `sub_0014C180` incorrectly treated a scanned stack
symbol candidate as an unwound caller; the concrete wait is in `sub_0039EC10`.
The first independent epoch-on run under `perf147_epoch` regressed to about
551–557ms/frame. Linear lookup selected the first basic-identity entry before
checking its decoder context, repeatedly evicting alternating stage contexts.
Candidate148 fixes selection to match full identity first. Its off arm passed,
but its on arm failed the movie route and left incomplete timed windows. The
558s capture remained in the intro instead of reaching the paused beach.
Snapshots show the known baseline DirectSound polling signature (`0x33ECAF`,
`0x386483`, APU IRQ pending), GET=PUT, notify clear and no pending executor
fences. This invalid run does not establish an epoch-caused stall; all148
timings are excluded from speedup claims. Candidate149 additionally includes
general linear source pitch in full identity, with a fixture for pitch changes
inside one epoch. Its manifest is `analysis/build-verification-perf149.json`;
fresh independent comparison/replay gates run under `perf149_epoch`, retaining
every original release/idle barrier.

Candidate149's first valid700s pair reached matching paused-beach captures,
with full health and ammo48/500. Mean frame time was53.000ms off versus41.726ms
on (21.27% reduction); executor CPU was50.280ms/frame versus39.005ms/frame.
Requested texture comparison lengths fell from about204MB/frame to25MB/frame.
Program work matched within1%. Evidence is `analysis/perf149_epoch-pair-1.json`
and `analysis/perf149-epoch-pair1-captures.png`. This is preliminary: repeat and
normal-audio replay gates are still pending, and the flag remains off by default.

All three candidate149 pairs subsequently completed. Mean frame time was
52.600ms off versus41.421ms on:21.25% reduction, with pooled between-run
SD0.338ms. The performance threshold, work equality and consistent executor
CPU reduction gates passed; all six fixed-view captures show the expected
paused beach/HUD. Evidence is `analysis/perf149_epoch-comparison.json` and the
three `perf149-epoch-pair*-captures.png` montages.
The original recorder children emitted no hardware files, so these pairs pass
the frame/CPU/work gate but do not complete the planned clocks/load check.
The launcher now uses its current PowerShell interpreter and rejects failed or
missing recorder output. NVIDIA collection works in the sandbox; GPU Engine
collection requires host access here. A host recorder test retained valid
owned-process samples (`perf149_host_gpu_check-gpu-load.jsonl`). A fresh
hardware-recorded comparison is required before promotion; unavailable counters
are not evidence that the GPU is idle.
The first replay control exited with code0 at1164.74s, before the2000s route
finished. Its captures show squad death and restart confirmation, but no
returned HUD/resumed firing. There is no logged device loss or firmware exit;
the exit cause is unresolved and this run is invalid for qualification.
Fresh replay gates completed under `perf149_epoch_retry`, using the completed
comparison without rerunning its benchmark pairs. All four normal-audio,
query-free routes (two per arm) completed normally at2000.21–2000.47s, watchdog3,
without host timeout or explicit stop. Reviewed completed-flip captures show
combat/firing, damage/death, mission restart confirmation, returned full HUD
with health/ammo48/500 and resumed movement/firing/damage in every run.
Each1640s inspection shows APU IRQ0, raised IRQL0 and notify0. Source PCM totals
range83.39–83.79 million sample frames, except the final candidate's83.36 million;
all retain nonzero audio. Per-run `-verification.json` files record exact values
and reviewed captures. The hardware retry under `perf149_epoch_host` was
stopped at 153.77s, before any timing window: Penumbra
(`C:\dev\Antumbra\build\bin\RelWithDebInfo\penumbra.exe`) was using about 53%
of the same GPU's 3D engine. Both recorders worked and identified the owned game;
this contended run supplies no speedup evidence. A fresh three-pair comparison
requires the competing workload to stop.
The flag remains default-off; `play.ps1 -Executable brute_force_e3_perf149.exe
-TextureEpoch` explicitly selects it, while the verified launcher default is
preserved. The exact candidate with epochs enabled passed visible-window and
owned-child closure (`analysis/test-launcher149-epoch-final.json`). Hardware
qualification remains pending, so no default promotion is made.

Native validation passed after explicit successful rebuilds: nine hardware
tests (including hash/epoch combinations, palette/mip freshness,1025 texture
identities, GPU-produced vertex/index/private-depth inputs and an injected
device-loss fatal-handler test), four semaphore
tests (including coalescing, tiled-alias acquisition and the barrier override),
and pushbuffer control with GPU-produced command words.
Eight Python report tests cover exclusions, missing CPU measurements, resets,
lost reports, outcome accounting, legacy logs and competing GPU processes
without double-counting their queues, and rejection of changed concurrent load
or clocks despite faster reported frame times. The fatal-handler fixture
does not simulate an actual driver removal.

Later stages remain gated: GPU clears, broader storage reuse, vertex compaction
and trap latency changes require measured attribution and separate correctness
comparisons. Guest-memory residency and asynchronous publication require proven
write observation before removing snapshot checks or moving publication off the
executor. The failed release route is not evidence to proceed past those gates.

Repeated comparisons use run means, pooled between-run standard deviation,
work equality within1%, and executor CPU time when available. The harness also
requires complete hardware observations and at most 5% peak competing 3D-engine
load summed across other processes in each timing window. This is a sampled
interference screen, not a fixed-clock or full-system-idle guarantee. Captures and
normal-audio2000s replay gates still require review before promotion.

When another project must stay running, `-ConcurrentWorkload` records and
compares the same three pairs without requiring low external load. It instead
screens each paired window for at most 5 percentage points difference in both
each external process's mean 3D activity and their total. Graphics/memory clock
ranges must overlap, their means must differ by at most 10%, and hardware query
errors are rejected. These are sampled comparability checks, not causal
isolation. The resulting evidence qualifies only the concurrent workload and
is recorded separately; it does not satisfy the idle-load promotion gate.
The ordinary mode keeps its low-load requirement. Penumbra may stay open and
is never controlled by this harness. If work changes too much between arms,
the result remains inconclusive rather than being accepted as a speedup.

The first two concurrent pairs completed with matching paused-beach captures.
Pair 1 measured49.175→41.390ms/frame (15.83%) and executor CPU46.302→38.795ms/frame;
pair 2 measured48.143→45.507ms/frame (5.47%) and CPU44.992→42.285ms/frame. Competing
3D activity reached about35–37% in pair 2. These are separate observations under
changing workload, not an isolated improvement claim. The third control stalled
in a movie before the paused view. Live executor and main inspections and all
thread samples were retained before stopping it: GET=PUT, no pending executor
fences or notify, APU IRQ pending, main stack through0x386483. This matches the
known baseline DirectSound signature with epochs off. Its data is excluded.
The third pair completed normally on retry under `perf149_epoch_concurrent_retry`;
the two earlier pairs and all original failure evidence are retained. All six
valid captures match the paused beach, full health and ammo48/500, and all six
binary start/end hashes match frozen candidate149. Final evidence is
`analysis/perf149_epoch_concurrent_retry-comparison.json`.

| Concurrent pair | Epoch off ms/frame | Epoch on ms/frame | Off CPU ms/frame | On CPU ms/frame |
| --- | ---: | ---: | ---: | ---: |
| 1 | 49.175 | 41.390 | 46.302 | 38.795 |
| 2 | 48.143 | 45.507 | 44.992 | 42.285 |
| 3, retried | 49.679 | 68.466 | 46.217 | 64.909 |

The run-mean aggregate is48.999ms off versus51.788ms on, with pooled between-run
SD10.331ms. Complete hardware observations and work equality within1% pass;
the performance threshold, consistent CPU reduction, low competing load and
paired-context comparability fail. Competing3D load in the last candidate reached
60%, versus about42% in its control. These measurements are **inconclusive for
an isolated optimization effect**, not proof of an epoch-caused regression.
Requested texture `memcmp` lengths consistently fell from about203.7MB/frame to
25.1–25.3MB/frame (about88%); these are requested lengths, not exact memory
traffic or elapsed CPU cost. Penumbra and other projects were left untouched.
The earlier three-pair frame/CPU result remains separate from this changing-load
experiment. No default promotion is made. Concurrent profiling can continue
without closing another project; a reliable wall-time claim still requires
comparable conditions, and arbitrary changing contention cannot be normalized
away from sampled GPU percentages.

```powershell
.\games\brute_force_e3\verify_performance.ps1 -Name fresh_concurrent_epoch -Executable brute_force_e3_perf149.exe -Experiment TextureEpoch -ConcurrentWorkload
```

```powershell
.\games\brute_force_e3\verify_performance.ps1 -Name fresh_overhead -Executable brute_force_e3_perf149.exe -Experiment Overhead
.\games\brute_force_e3\verify_performance.ps1 -Name fresh_release -Executable brute_force_e3_perf149.exe -Experiment Release -ReplayGates
.\games\brute_force_e3\verify_performance.ps1 -Name fresh_hash -Executable brute_force_e3_perf149.exe -Experiment TextureHash -ReplayGates
.\games\brute_force_e3\verify_performance.ps1 -Name fresh_epoch -Executable brute_force_e3_perf149.exe -Experiment TextureEpoch -ReplayGates
.\games\brute_force_e3\verify_performance.ps1 -Name fresh_cache -Executable brute_force_e3_perf149.exe -Experiment TextureCombined -ReplayGates
.\games\brute_force_e3\verify_performance.ps1 -Name fresh_combined -Executable brute_force_e3_perf149.exe -Experiment Combined -ReplayGates
```

Run these sequentially. Each pair's guest work is checked before the next pair;
an unmatched or incomplete route stops the workflow and requires diagnosis.
Each three-pair performance comparison takes about70
minutes; replay gates add four2000s runs. The runner stops on abnormal process
termination, preserves state/evidence, and never promotes binaries. Build and
probe entry points reject live executable rebuilds and reused probe names.
An exclusive `analysis/runtime-probe.lock` also prevents overlapping probes
across shell sessions; launch failures release it and terminate owned children.
The launcher and build script respect this lock. A workflow can be cancelled
between probes with its `<Name>-stop.flag`; use the active probe's stop flag to
end that probe as well.
To resume only replay gates, use a fresh name with `-ReplayGates` and
`-ResumeComparison <completed-comparison-name>`. The runner requires a passed
three-pair comparison, matching start/end binary hashes, normal exits and the
same experiment flags. It preserves the original failed workflow evidence.

## Prior candidate132 evidence

Status: candidate132 is separate; the instrumented comparison and one normal-
graphics replay route passed. Query-free improvement against the original
verified build remains unproven; the counters comparison above is a different
experiment.
The normal executable is still the previous verified
build. Original assets remain the read-only source at
`D:\Downloads\Brute Force E3 Prototype`; saves and diagnostics are under this
project. No NVIDIA settings were changed. The user subsequently authorized a
source/evidence handoff commit to main; that handoff is not a verified release.

## Reproduce the comparison

Run benchmarks sequentially with no other Brute Force test active. Start read-only
`monitor_gpu.ps1` and `monitor_nvidia.ps1` recorders for each fresh probe name,
then run:

```powershell
.\games\brute_force_e3\performance.ps1 -Name perf_baseline137_idle -Executable brute_force_e3_prototype.exe -PausedView -Seconds 700
.\games\brute_force_e3\performance.ps1 -Name perf_candidate138_idle -Executable brute_force_e3_perf132.exe -PausedView -Seconds 700
python games/brute_force_e3/compare_performance.py --baseline perf_baseline137_idle --candidate perf_candidate138_idle --name perf137-vs138-final
```

These are the recorded commands before promotion; existing names must not be
reused. After promotion, copy the original executable/PDB from
`analysis/verified122-backup` to a distinct basename under
`build/RelWithDebInfo` (for example `brute_force_e3_baseline122.exe/.pdb`), verify
the original43d7f8b3… executable hash, and select that copy for the before run.
Use fresh probe names and matching recorder names. Never rebuild an executable
while its test process is running.

The script opens the same mission, skips the same prompts, and pauses the
starting beach camera. Timings cover560–590,600–630 and650–680 seconds. CPU
sampling and completed-flip captures occur outside those windows. Confirm the
captured scene, full health/ammo48/500, and approximately127,300 vertex rows/frame.
This measures one fixed rendered gameplay view with normal audio; it does not
establish an active-combat or campaign-wide speedup. Mean frame times use
completed flips over complete host-observed report intervals. There are no
per-frame percentiles; polling/log buffering and natural clock variation remain
measurement limits. GPU Engine observations are sampled3D activity, supplemented
by NVIDIA utilization, clocks, memory activity, temperature and power queries.

Baseline137 completed700.43s (intentional watchdog3), matching completed00331 at
558s. Windows50.56/49.59/47.50ms aggregate49.22095ms (20.32FPS). Competing observed
3D engine activity was low, with external individual peaks<=1.3%; no counter
or NVIDIA query errors occurred during those windows. Hardware clocks varied
naturally. Candidate138 completed700.48s (intentional watchdog3), matching
completed00320 at558s and the same vertex workload. The paired result is
`analysis/perf137-vs138-final.json`:

| Requested seconds | Original ms/frame | Candidate ms/frame |
| --- | ---: | ---: |
| 560–590 | 50.56 | 44.10 |
| 600–630 | 49.59 | 45.82 |
| 650–680 | 47.50 | 45.83 |
| Aggregate complete intervals | 49.22 | 45.24 |

That is an8.1% frame-time reduction (20.32→22.11FPS) in this one instrumented paired fixed
view. All three windows improve. Vertex packing is approximately5.7→4.1ms/frame
with essentially unchanged vertex rows; submission remains about14.7–15.1ms.
Both runs have low competing observed3D activity, no telemetry/query errors
during the windows, and overlapping naturally varying clock ranges. This is
not a fixed-clock experiment or a campaign-wide performance guarantee. Both
runs enabled GPU profiling queries. Because those queries can affect scheduling
and clocks, a second pair with `-ProductionBenchmark` will measure the normal
graphics mode (GPU trace/timing disabled, external report timestamps only).
Production performance is not claimed proven by the instrumented pair alone.
The query-free observer also records always-printed cumulative vertex-program
work counters, pairing only adjacent report packets, to check matching work
without enabling graphics timing queries.

## Changes and preserved behavior

- Queue requested color/depth staging copies together before mapping.
- Reuse compatible same-sized native targets, including pitch in color identity.
  New backing still uploads coherent content; allocation reuse is not content
  residency. Exact CPU snapshots distinguish allocation, loaded content and
  valid coherence evidence, including failed setup/recovery.
- Keep the renderer's private managed depth native until a real CPU or target
  dependency requires synchronization. Depth-only releases wait on an actual
  D3D11 event result; errors propagate. Semaphore releases, software traps,
  interrupts and frame boundaries retain real completion and prefix ordering.
- Upload eligible linear BGRA textures directly with bounds/pitch checks and
  byte-exact mip/palette freshness. Unsupported cases retain the existing path.
- Gather all vertex inputs referenced by the translated shader's A/B/C operands.
  Invalid or missing-END programs gather all inputs. The vertex layout, draw
  submissions, HLSL arithmetic and CPU fallback inputs remain unchanged.
- Preserve failed-batch suffix suppression and completed prefixes; verify the
  GPU consumer actually exits before releasing its resources/backing.

Direct swap-chain presentation, bounded target allocation pooling, contiguous
readback copies and event-query readback preflight were implemented and tested
as options. Earlier measurements did not establish a benefit for enabling them
by default, and several lacked matching GPU telemetry. `play.ps1` clears these
experiment flags. `RECOMP_HW_VSH_GATHER_ALL=1` provides an input-gather A/B control.
No draws, audio hardware or real completion were removed.

## Correctness evidence

Exact candidate132 SHA256:
`67eaa4298b7b5d048d878eda096bc0fd23f0cbcb32025da69ee6a42ff7f58ad5`.
It completed2000.23s with normal audio and graphics profiling enabled, watchdog3
with no explicit stop or host timeout. Completed-flip evidence under `analysis/`:

| Probe prefix `perf_candidate132_default_replay-frame-` | Evidence |
| --- | --- |
| `00602.bmp` | Moved player/camera, firing, ammo24/500 |
| `00726.bmp` | Squad Killed / Replay Mission |
| `00929.bmp` | Saving and replay intro starts |
| `01125.bmp` | Returned HUD, full health and ammo48/500 |
| `01209.bmp` | Movement/firing after replay, ammo28/500 and visible damage |

Source PCM reached83,001,344 stereo sample frames and117,926,103 nonzero channel
samples. The1640s read-only inspection showed no pending APU interrupt and IRQL0.
Normal graphics verification with GPU trace/timing disabled is still required
before promotion, since profiling queries can alter waits and scheduling.

The updated normal launcher passed visible-window/owned-child closure against
the exact candidate (`analysis/test-launcher132-final.json`). Native evidence:

| Requirement | Evidence under `analysis/` |
| --- | --- |
| Raster, color/depth coherence, failed setup, pitch, mip freshness | `test-raster132-default.log` |
| Real GPU semaphore ordering | `test-fences132-default-gpu.log` |
| Optional event/pool/bulk paths and failures | `test-raster132-event-pool-bulk.log`, `test-raster132-event-reload.log`, `test-fences132-event-gpu.log` |
| Shader inputs, all16 indices, B/C-only operands, unreferenced NaNs | `test-vertex132-input-mask-expanded.log`:148 configurations /9,472 inputs |
| Pushbuffer control and invalid-prefix/suffix handling | `test-pushbuffer132-final.log` |
| Keyboard, focus and window closure | `test-keyboard132-final.log` |
| XAudio2 buffer ownership, mute and failure cleanup | `test-xaudio2132-final.log`:0 failures |
| Optional direct presentation, pixels, resize, focus and closure | `test-framebuffer-gpu131.log`:24,064 exact pixels,0 failures |

## Remaining bottlenecks and limits

Color coherence is required before guest-visible publication or a CPU dependency.
Per-release completion inside a batch is now a separately gated experiment:
the store is queued until a later completed publication point. More general
selective readback requires actual
access tracking across aliases/shared pages, owner-thread synchronization,
write invalidation, failure handling and native render-target sampling hazards.
Skipping readbacks without that mechanism would lose coherence. See
`analysis/gpu-residency-investigation.md` for the source audit and requirements.

Asynchronous guest completion needs proven color access tracking, bounded
backpressure, real readiness, CPU dependencies and preserved interrupt/semaphore
prefixes. Whether staged snapshots/frontiers are needed depends on the coherence
design; they are not an established requirement for fault-driven residency.
Earlier deferred-release stalls were confounded by an intermittent baseline
movie stall and are insufficient causal evidence. Default releases still wait
for real completion.

Frequent color maps/copies, command parsing, vertex packing/coherence work and
many small native submissions remain. Final candidate138 intervals spend
12.65–13.23s/29.48–29.88s synchronizing, with43,020–43,910 color maps. Color-map
waits consume7.73–8.15s and color copying3.65–3.72s. There are2,371–2,427 event
fences and3,257–3,325 color allocations per interval; same-size reuse remains
enabled while the experimental dimension pool is off. Submission and sync
counters can overlap and must not be added as independent frame costs.

Current baseline137/candidate138 CPU samples are saved separately. Driver
exports may resolve with large symbol offsets, so symbol-name tallies are not
treated as precise driver-function costs. Current interval counters replace
the earlier119/118 traces as the final bottleneck evidence.

Contended measurements are excluded from an idle-load speedup claim. Original
baseline133 also stalled in a movie/DirectSound status poll before gameplay;
baseline134 exited early with an unverified cause. These experiments remain
documented in `analysis/performance-investigation.md`. The intermittent movie
status issue has no confirmed root cause and no APU behavior was changed for
this performance work. Movie decoding artifacts, passthrough DSP, incomplete
campaign/multiplayer coverage and untested physical controllers remain.
