# Quiet-GPU combat comparison on candidate163

October 20, 2026. Diagnostic pair; no qualification, prototype promotion or
default change. Both arms use the retained candidate163 EXE/PDB.

**Result:** the traced barrier window averaged approximately **19.52 flips/s
(51.23 ms/flip)**; the coalesced window averaged **28.64 flips/s (34.92 ms/flip)**.
The largest identified reduction is readback-related CPU work. Texture comparison
remains approximately **5.4 ms/flip in both arms**, making texture epochs the next
bounded qualification experiment. These are observations from one instrumented,
imperfectly scene-matched pair, not a general speedup or shipping qualification.

## Collection and controls

The user closed GPU-heavy applications. The process inventory found no
Penumbra, prior game, Nsight collector or WPA exporter before collection.
A three-second physical-GPU preflight measured 8.46% graphics activity and
0.18% SM activity over its central two seconds. This is a quieter desktop,
not exclusive ownership of the GPU; light application/compositor work remains.

The existing bounded probe launched the game with sanitized launch settings.
GET progress remained at its qualified default; GET monitoring, IRQ gating,
bulk readback, texture epochs/hash, optional hardware profiling, stack sampler,
surface dumps and WAV tap were off. Audio and the default GDI presenter stayed
in their baseline configuration. Only the coalesced arm enabled the existing
`RECOMP_HW_RELEASE_COALESCE=1` policy. No new game build was made.

Each arm started from a separate copy of the same initial save/cache tree
(137 files, with SHA-256 manifest). The untouched second copy was checked
against that manifest before launch. Global boot-flip scheduling alone had
proved insufficient for menu navigation: the initial preparation trial was
stopped without recording. The definitive runs entered the same campaign
path; the second omitted a menu Down request that had arrived during the
first arm's transition. Both required reviewed rendered HUD and stable living
campaign state before the measurement setup.

After HUD verification, the existing virtual-controller parser accepted the
same sequence relative to a guest-flip anchor: Y for 24 flips at +0, Y/24 at
+100, left-stick up/12 at +200, right trigger/24 at +300, right-stick right/4 at
+400 and right trigger/24 at +500. Recording began after +700 and review of a
completed HUD frame. The live parser's required wall-time prefix used a
zero-duration step that pressed no button. Actual input receipts are archived;
requested scheduling is not a claim of exact input acquisition on every flip.
No scripted inputs remained during either recording window.

The coalesced HUD initially displayed a co-op hint rather than the barrier's
tutorial panel. A preparatory Y pulse advanced that hint; its completed frame
still showed the same 48/500 starting weapon. The shared sequence then ran.
Reviewed pre-trace frames show full health, 33/500 ammunition in barrier mode
and 34/500 in coalesced mode, in the same beach/camera area. The requested
four-flip right-stick input was acquired at +401 in barrier mode and +400 in
coalesced mode, so the views differ slightly. This is route matching with
documented input/scene drift, not pixel-identical state.

Completed frames came from read-only copies of the presenter's stable finished
buffer, checked twice with an unchanged buffer index. Guest-state snapshots
also used read-only process access. There were no frame dumps or stack samplers
during recording. A light host watcher timestamped the game's existing flip
report lines at 100 ms polling intervals; no runtime counter or hook was added.

The installed fixed-operation helper recorded CPU-only WPR plus non-injecting
Nsight WDDM events and NVIDIA GPU 0 metrics at 1 kHz for each arm. It ran without
further UAC prompts. CPU stop was requested after 35 seconds rather than after
Nsight conversion. Raw ETLs include WPR rundown; CPU analysis must use the same
central Nsight [1, 29] second interval by UTC alignment. **All combat exports and
WPA analysis were deferred until both recordings and both games had finished.**

## Aligned results

The barrier capture used game PID 32116, executor thread 1648 and main thread
37548. The coalesced capture used PID 24776, executor 42724 and main 28864.
Both pre/post snapshots have campaign type 362540135, flags 48, stable roots,
the same mission-state pointer, no reader errors and death flag false. Reviewed
post-trace frames retain their starting ammunition and HUD view.

Both CPU ETLs have **zero lost events and buffers**. Their envelopes include
approximately 56 seconds of recording/rundown. Analyze WPR [3.2344165,
31.2344165] seconds for barrier and [3.6047297, 31.6047297] for coalesced: each
matches Nsight [1, 29] seconds. The GPU metrics contain approximately 28,000
samples per arm at 1 ms spacing. The final idle bookend measured 0.004% graphics,
0.001% SM and 0% synchronous-copy activity over its central two seconds.

| Same-window measurement | Barrier | Coalesced |
|---|---:|---:|
| Host-timed existing flip receipts | 512 flips / 26.230 s | 748 flips / 26.121 s |
| Estimated flip rate | 19.52/s | 28.64/s |
| Estimated frame interval | 51.23 ms | 34.92 ms |
| Executor sampled CPU in 28 s | 27,365 ms | 27,574 ms |
| Main guest sampled CPU in 28 s | 4,128 ms | 5,924 ms |
| Estimated executor CPU per flip | 50.07 ms | 34.39 ms |
| Physical graphics-engine activity | 10.63% | 19.93% |
| Physical SM activity | 4.43% | 15.42% |
| Physical synchronous-copy activity | 27.89% | 16.16% |
| Physical DRAM read bandwidth utilization | 0.62% | 0.64% |
| Recorded game-process NVIDIA 3D CPU submission packets | 44,296 | 16,671 |

The executor occupies nearly one CPU core in both arms. This does not mean all
that time is useful computation: the largest category still includes readback
availability polling, yields and driver calls. Graphics/SM activity increases
with the higher flip rate while synchronous-copy activity decreases. Recorded
CPU submission packets are neither draws nor sync counts nor GPU-time shares.

Disjoint executor categories, with identifiable tracing excluded from the Map
bucket, are:

| CPU category | Barrier share | Coalesced share | Estimated CPU ms/flip, barrier → coalesced |
|---|---:|---:|---:|
| Readback Map/poll/driver | 38.40% | 31.57% | 19.23 → 10.86 |
| Vertex packing/draw submission, including driver descendants | 17.99% | 24.57% | 9.01 → 8.45 |
| Texture comparison | 10.85% | 15.77% | 5.43 → 5.42 |
| Readback copies | 9.55% | 1.85% | 4.78 → 0.64 |
| Other comparisons, including inlined snapshot state | 5.21% | 0.77% | 2.61 → 0.27 |
| Other synchronization/copy/compare | 3.17% | 4.05% | 1.59 → 1.39 |
| Identified tracing/stack walking | 2.59% | 1.54% | 1.30 → 0.53 |
| Other vertex attribute/cache work | 0.07% | 0.12% | 0.04 → 0.04 |
| Other/unattributed | 12.18% | 19.75% | 6.10 → 6.79 |

Inclusive `synchronize` accounts for 53.19% / 38.18% of executor samples;
`map_readback` 40.32% / 32.08%; `SwitchToThread` 24.98% / 21.95%; and
`hardware_batch` 35.68% / 44.62%. These inclusive values overlap and cannot be
added. Inclusive `common_getenv_nolock` stays small at 0.063% / 0.102%.

Leading exclusive functions are `memcmp` (16.05% / 16.54%),
`KiSystemServiceUser` (13.22% / 11.83%), unresolved NVIDIA user driver
(7.99% / 7.79%) and `NtDelayExecution` (7.17% / 6.18%). `MoveSmall4` is 9.82%
exclusive in barrier mode, consistent with its retained row-copy path;
`fetch_attr` is 3.07% / 4.48%, and D3D11 resource `Map` is 3.69% / 3.42%.
Other comparison callers cannot all be separated from optimized/inlined
snapshot checks, so that category is deliberately not labelled exact snapshot
cost. Unresolved driver samples are not automatically classified as busy-waiting.

Neither quiet window shows sustained graphics/SM saturation. However, the
fallback still lacks NVIDIA 3D DMA execution intervals and per-process GPU
timings; Nsight reports mapping/system-profiling warnings. There is no exact
CPU-Map/GPU-execution correlation here. The quieter GPU does not make every
remaining Map cost a proven latency-only wait.

## What comes next

For this quiet route the ranked identified executor costs are **readback
Map/poll/driver**, **command preparation**, then **texture comparison**. Barrier
mode additionally pays substantial copies and other comparisons. The per-flip
estimates explain why comparing CPU percentages alone would be misleading:
texture comparison rises to 15.77% while its approximately 5.4 ms/flip cost
barely changes.

This pair supports coalescing as a meaningful performance candidate even on a
quieter GPU, while leaving its correctness and full gameplay gates open. It
does not support treating candidate159's 2.8 ms observation as a fixed ceiling.
It also does not prove that Penumbra caused all the earlier barrier cost: large
readback-related costs remain without that heavy background load. The earlier
loaded window and this pair are not a controlled loaded-versus-quiet experiment.

**Qualify texture epochs next**, independently of hash lookup and coalescing.
It targets a measured comparison cost with an existing bounded implementation.
Then continue **target residency/CPU-consumer observation** as the structural
track: the coalesced arm still spends an estimated 10.86 ms/flip in Map/poll/
driver work. Asynchronous completion may free polling CPU, but a throughput gain
requires dependency overlap and safe publication; it is not established by this
profile. The shared yellow-foliage rendering defect remains visible in both
arms and stays a separate investigation. No rendering fix or ablation was run.

## Interpretation limits

This is one route and one window per arm, in fixed barrier-then-coalesced order.
The windows cover the settled beach/HUD view after brief movement and firing,
not every combat workload or a sustained firefight.
Driver caches, clocks, desktop activity and scene progression were not locked.
The same flip-relative inputs do not guarantee identical simulation state,
input-poll receipts or ammunition. Before/after frames and guest-state checks
provide route evidence; they do not constitute coalescing qualification.

Flip-rate estimates use first/last existing reports entirely inside the
28-second GPU window and QPC elapsed time. They cover a slightly shorter
subwindow and have approximately 100 ms observation granularity at endpoints.
Any CPU milliseconds per flip derived from that rate are estimates, not exact
per-frame instrumentation. Native printed FPS values are not used for the
host-timed calculation.

CPU sampling measures on-CPU work, including active polling/yield overhead;
it does not supply blocked time or readying stacks. Inclusive functions overlap.
Private NVIDIA driver symbols and exact D3D11 API call timings remain unavailable.
Physical GPU activity is global busy time, not shader saturation or per-process
GPU time. WDDM submission counts cannot be converted into GPU-time shares or
proof that a staging-resource Map drains every application's queue.

The named prototype remains candidate157. Texture epochs qualification and the
normal gameplay gate before promoting launch-flag caching remain separate work.
Bulk readback is not carried forward as a performance candidate. The
[residency design](TARGET_RESIDENCY_DESIGN.md) remains design-only, with eager
coherence initially and explicit CPU-consumer/alias coverage before removing
readbacks.

## Reproduction and artifacts

Software: Nsight Systems 2026.5.1.161, WPR 10.0.26100, Windows build 26340,
24 logical processors and the NVIDIA RTX 4070 Ti. The installed collector and
ordinary client are documented in `tools/windows-profiler/README.md`.

Candidate163 retains EXE SHA-256
`a2cc9c671dcc70a7e570d64cb8939cb4c5d8031840af0c2efb5608e15b4e83c5`
and PDB SHA-256
`a69ac79bbf86b7d5010a1c6508746a47d1168ab02b51c19ca2fa6912623b941e`.
The final verification confirms both these files and the named prototype's
EXE/PDB are unchanged from the pre-pair identities.

Raw files remain in ignored `analysis/quiet170/`; derived SQLite, WPA profiles,
CPU exports and summaries use the `analysis/quiet170_*` prefix. The capture
receipts identify protected recorder jobs `7dfaa72a3ca94ef8bb575dcc8b576576`
(barrier) and `b60fb7104d14421c8418de1951c39177` (coalesced), game launch settings,
guest snapshots and UTC times. `comparison.json` records the exact CPU/GPU and
flip-report calculations. `verification.json` hashes 58 raw/derived artifacts,
snapshots, scripts and manifests; the verifier checks ETL losses, alive campaign
state and unchanged binaries. The initial shortened-route preparation trial
and the older interrupted/menu capture are excluded from the findings.

| Raw file under `analysis/quiet170/` | SHA-256 |
|---|---|
| `barrier-recording/cpu.etl` | `9792659f1660eff396b67295b66c64a3aa232dcdc4d150c338446038d0929936` |
| `barrier-recording/gpu.nsys-rep` | `dec7a37193a98303bd5934686662e7a49ea558e8549a2ed2fe53bf6609dce7bb` |
| `coalesced-recording/cpu.etl` | `15964c44ae57247c98708e765e131711ac56ec86349ff34cc27bfb38c8b938fc` |
| `coalesced-recording/gpu.nsys-rep` | `b02b4f7479b4041ff482a3894dbb71e6cc2752e2cf82eaf4b1f61198ac39e73d` |

Both games and recorder sessions were stopped after collection. No runtime code,
defaults or qualified policy changed; no ablation, HAGS change or reboot occurred.
