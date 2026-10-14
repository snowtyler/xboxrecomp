# Independent review: Brute Force E3 native recompilation performance

Please independently review this Windows x64 Xbox native recompilation and
recommend the highest-value path to substantially lower frame times while
preserving correctness. I want an architectural opinion grounded in the code
and evidence, not generic advice to use the GPU or a promise of particular FPS.

Read `SNAPSHOT.md` first. The accompanying review ZIP contains the relevant
current runtime source, tests, benchmark records, raw logs, telemetry, selected
completed-frame captures, and a limited before-change source snapshot/diff.
Paths inside the ZIP correspond to repository paths. The runtime/performance
goal is paused; this is a review, not permission to launch tests or promote a
candidate. No game assets, executables, PDBs or generated game-code chunks are
included, so this is not a self-contained playable/buildable game distribution.

## What to examine

1. Start with `games/brute_force_e3/{README.md,STATUS.md,PERFORMANCE.md}`,
   `analysis/build-verification.json`, `analysis/performance-work-state.json`,
   and `analysis/gpu-residency-investigation.md`. Some chronological notes
   describe formerly live tests; SNAPSHOT.md overrides those stale statuses.
2. Inspect `src/kernel/nv2a_hw_raster.c/.h`, `nv2a_pb_exec.c`,
   `nv2a_pb_scan.c`, `xbox_memory_layout.c/.h`, the vertex-shader translator,
   and `src/video/fb_present.c` / `fb_gpu_present.c/.h`. Trace the actual draw,
   target transition, CPU coherence, semaphore, software-trap/ISR and flip paths.
3. Inspect `templates/runtime/recomp_types.h`: lifted guest memory accesses
   are native volatile pointer accesses, not ordinary memory-bus callbacks.
   Do not assume that observing renderer reads captures every guest CPU access.
4. Inspect the original137 / candidate138 paired reports and raw event streams,
   GPU Engine/NVIDIA telemetry and CPU samples. Both timed runs enabled graphics
   profiling queries; their8.1% improvement is not yet a normal-mode FPS result.
   Driver symbols can be nearest exports with large offsets, not actual hot
   function names. GPU timestamp sampling is bounded; dropped profiling samples
   are not dropped draws. Do not extrapolate sampled GPU time to all frame work.
5. Review the relevant raster/coherence/mip, shader-input, pushbuffer/semaphore,
   keyboard/window and XAudio2 fixtures and logs. Distinguish their actual
   coverage from the broader guest-runtime guarantees they cannot establish.

## Constraints

- Preserve original rendering, CPU/GPU memory coherence across aliases, and
  real guest interrupt/semaphore completion ordering.
- Do not fake completion, skip draws, remove audio hardware, force stopped audio
  status, or trade correctness for FPS. Original assets must remain read-only.
- Do not propose changing NVIDIA settings as the solution. Natural clocks were
  observed and recorded; no settings were changed.
- Develop separate candidate executables; never rebuild a running executable.
  Do not replace the verified build until combat, damage/death, mission replay,
  returned HUD, resumed movement/firing and relevant native regressions pass.
- Movie decoding artifacts and passthrough DSP are existing limitations.
  Earlier movie/DirectSound status stalls have an unconfirmed root cause; one
  occurred on the original verified bytes. Do not label the APU hypothesis proven
  or infer that removing sound is an acceptable optimization.

## Questions and requested response

1. Explain where time is actually going, separating measured facts from source
   inferences and hypotheses. Identify misleading instrumentation or conclusions.
2. Review correctness risks in the implemented changes, especially managed depth,
   snapshots/resource reuse, conservative vertex-input pruning, failure handling,
   completion frontiers and GPU presentation. Cite concrete paths/functions.
3. Rank the next3–5 changes by likely payoff, effort, risk and the measurements
   needed to decide. Distinguish incremental CPU savings from structural gains.
4. Assess a safe GPU-resident color/render-target path with selective CPU readback:
   alias/shared-page access tracking, CPU reads/writes, address reuse, native
   sampling/mips/feedback, context-owner servicing, lock/IRQL/deadlock risks,
   eviction/shutdown, and unsupported-case fallback. Offer a concrete design
   if viable; explain what must be proven before enabling it.
5. Assess true asynchronous completion: ordered GPU frontiers, staging snapshots,
   real readiness, guest publication, prefix/suffix failure semantics, DMA_GET/
   command/vertex-buffer reuse and backpressure. Earlier deferral failed and was
   reverted; that is evidence against that implementation, not against every
   asynchronous design.
6. Determine whether direct GPU presentation should be retested without graphics
   timing queries before tackling wider residency changes, and what would make
   the experiment comparable. Current direct presentation/pooling/bulk-readback/
   event-preflight options are off by default because benefit was not established.
7. Give a minimal, reproducible experiment and regression plan for the preferred
   next step. State success/failure criteria and how to preserve a rollback build.

Lead with your assessment and recommended next step. Avoid introducing patches
until you have explained why they target a measured bottleneck. If data are
insufficient, request the exact additional evidence instead of inventing results.
