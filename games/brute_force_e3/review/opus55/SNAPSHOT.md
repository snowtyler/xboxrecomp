# Review snapshot — 2026-10-14

The performance goal is paused at the user's explicit request. All game tests
were stopped. This export prepares an independent review; it does not resume
the performance goal or certify completion.

## Artifact and verification status

- Normal verified executable remains `build/RelWithDebInfo/brute_force_e3_prototype.exe`.
  SHA256 `43d7f8b3953a7c733eaa9acfa4826ed71ac50ef328e085d0ab0834e7db4bc36e`.
  It passed the original25-minute audio-enabled combat/death/replay/control test.
- Separate candidate is `brute_force_e3_perf132.exe`, SHA256
  `67eaa4298b7b5d048d878eda096bc0fd23f0cbcb32025da69ee6a42ff7f58ad5`.
  PDB SHA256 `67030bcdcecaedd2506eec7a70ae121159d8654ef7f107228533018b386f420e`.
  It has not replaced the verified executable.
- Candidate132 completed2000.23s with normal audio and GPU profiling enabled.
  Captures prove combat/firing, death, replay, returned HUD and resumed controls.
  Its native regressions and the normal launcher owned-window/child closure passed.
- `final_candidate139_normal` disabled GPU trace/timing and experimental flags,
  retained audio, and disabled CPU sampling. It was explicitly stopped by the
  user at1325.32s, exit1/stopped=true/timed_out=false. It is NOT a completed replay/
  resumed-controls gate.00419 proves initial paused beach;00567 movement/fire;
 00681 Squad Killed.00905 is a partial report dump, not replay evidence. Do not
  count this interrupted run as either a passed full gate or a spontaneous crash.
- Two query-free production benchmarks remain, followed by completion of normal
  graphics gameplay validation and final promotion/launcher/documentation audit.

## Measured instrumented pair

RTX4070Ti; same starting beach, paused Options, full health/ammo48/500, normal
audio and roughly127,300 vertex rows/frame. Runs were sequential. Baseline137
completed700.43s and candidate138700.48s (intentional watchdog3, no host timeout).

| Window seconds | Original ms/frame | Candidate ms/frame |
| --- | ---: | ---: |
| 560–590 | 50.56 | 44.10 |
| 600–630 | 49.59 | 45.82 |
| 650–680 | 47.50 | 45.83 |
| Aggregate complete host-observed intervals | 49.22095 | 45.23633 |

8.09537% lower frame time,20.32→22.11FPS, for this one instrumented fixed view.
All three windows improve. Packing5.7→4.1ms/frame, submission approximately14.7ms.
Timed-window competing3D activity was low (individual external peaks<=1.3%),
with no GPU-counter/NVIDIA query errors. Clocks naturally varied in both runs.
Both enabled GPU profiling queries. Normal-mode improvement remains unproven;
`performance.ps1 -ProductionBenchmark` is prepared to measure without them.

Candidate138's complete sync-report intervals cover29.48–29.88s each and spend
12.65–13.23s synchronizing. There are43,020–43,910 color maps; color-map waits
consume7.73–8.15s, copying3.65–3.72s, and event fences0.34–0.41s. Submission/sync
metrics may overlap; they must not be added as independent frame costs.

## Implemented and investigated

Native D3D11 rasterization and translated GPU vertex shaders already existed.
Current changes queue color/depth copies before maps, reuse compatible storage,
retain private managed depth, maintain exact color snapshots, wait on real
depth-only completion events, support bounds/pitch-checked direct linear BGRA
uploads, prune only unreferenced vertex inputs, and strengthen failed-prefix/
suffix and shutdown handling. Every semaphore release still waits for real
completion; an earlier attempt to defer that barrier failed and was reverted.

Direct GPU presentation, a bounded dimension-based target pool, contiguous
readback copying and readiness preflight are implemented/tested options, off
by default. Earlier option comparisons lack complete matching telemetry or
did not establish benefit, so causal performance claims are limited.

Color still comes back to emulated memory frequently. Selective readback and
true asynchronous guest publication need wider coherence/frontier mechanisms.
`gpu-residency-investigation.md` describes actual ownership/access hazards and
requirements; it is not proof that those mechanisms have been implemented.

## Provenance and limits

README/STATUS and build-verification retain the previous verified build. Some
chronological investigation passages describe former live processes; this
snapshot and the paused work-state override those descriptions. The original
build-verification launcher hash predates experiment-flag clearing in play.ps1.
It must be refreshed with final promotion, not read as a current launcher hash.

The repository had existing uncommitted runtime/APU/translator changes before
this performance work. They are included for a complete handoff, not attributed
to the performance optimization. No APU behavior was changed for the speculative
movie-status race. The before-source archive covers only10 specific files; its
diff is not a complete repository history or a binary-to-source equivalence proof.

Raw game assets, EXEs/PDBs, saves and generated game-function chunks are omitted.
Source and evidence are for review. Static tests do not establish full campaign,
multiplayer or physical-controller correctness. Original assets stay local and
read-only. The user has now authorized committing/pushing the handoff to main;
this supersedes the earlier no-commit/no-publish instruction for this handoff.
