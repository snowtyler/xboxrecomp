# Performance verification

Status: candidate132 is separate; the instrumented comparison passed. Final
normal-graphics gameplay and query-free performance verification are pending.
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

Color readbacks remain mandatory because lifted guest loads are direct volatile
memory accesses, not memory-bus callbacks. Selective readback requires actual
access tracking across aliases/shared pages, owner-thread synchronization,
write invalidation, failure handling and native render-target sampling hazards.
Skipping readbacks without that mechanism would lose coherence. See
`analysis/gpu-residency-investigation.md` for the source audit and requirements.

Asynchronous guest completion likewise needs ordered GPU frontiers and staged
snapshots, bounded backpressure, real readiness, CPU dependencies and preserved
interrupt/semaphore prefixes. An earlier deferred-release experiment stalled
and was reverted. Current releases still wait for real completion.

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
