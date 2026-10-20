# Executor quick wins and lighter profiling

October 20, 2026. Diagnostic work; candidate163 is not a promoted prototype.

The first changes cache executor launch configuration and avoid unused timing
calls. The existing bulk-readback experiment supplies the copy optimization;
its default remains off. GET progress, barriers, coalescing, IRQ gating,
texture epochs and the named candidate157 prototype retain their policies.

## Changes and scope

`nv2a_pb_exec.c` now caches the frequently checked verbose/movie/writer flags,
frame-vertex and report switches, and dump/peek configuration paths. External
trace-file existence checks still work during a run. Environment variables are
launch configuration; changing them inside an already running executor is not
supported by these caches. Presence semantics, including empty entries, remain
unchanged for these pre-existing diagnostic switches. GET's parsed rollback is
untouched.

`nv2a_pb_scan.c` caches `RECOMP_PB_CALL_TRACE`. In the old trace, however, the
largest `getenv` caller was `draw_primitive`, not CALL traversal. The remaining
lookups in these files are initialization, already cached, or cold failure/dump
paths. Hardware-raster configuration was already read during initialization.

Raw-vertex packing/submission clocks now require `RECOMP_GPU_TIMING` or
`RECOMP_HW_CPU_TIMING`; trap-wait clocks require `RECOMP_HW_COUNTERS`. Texture
lookup/decode/upload clocks and their timing report require the existing hardware
profiling flags. Cache replacement clocks and correctness counters are retained.
The QPC calls consumed by the existing periodic `[HW SYNC]` report also remain;
this change does not claim that every clock call disappears with profiling off.

The old stack export locates almost all `MoveSmall4` work in `synchronize`,
rather than attribute fetching or vertex uploads. The existing
`RECOMP_HW_BULK_READBACK=1` branch copies contiguous BGRA storage and the exact
CPU-edit snapshot with one `memcpy` each when both pitches match the full row.
Padded layouts and RGB565 retain their row/conversion fallback. This task tests
that branch without changing its default or any coherence/completion rule.

The readback polling loop is unchanged. Blocking Map/event-query experiments
are separate work; no readback, comparison or drain is skipped here.

## Build and checks

Built with `build.ps1 -OutputName brute_force_e3_candidate163 -Optimize`, using
the same optimized configuration as the retained baseline. Earlier candidate
basenames were not rebuilt.

| Artifact | SHA-256 |
| --- | --- |
| candidate163 EXE | `a2cc9c671dcc70a7e570d64cb8939cb4c5d8031840af0c2efb5608e15b4e83c5` |
| candidate163 PDB | `a69ac79bbf86b7d5010a1c6508746a47d1168ab02b51c19ca2fa6912623b941e` |
| retained candidate157 EXE | `27208192a7c5847f5b1a005fec1c2d4772c986a6e1b9050c35f86ccf64c13825` |
| retained candidate157 PDB | `894676031cec88006394266920fbfb0114ba62c6590b63c1d11f7c0828c1ed5e` |

All five newly built pushbuffer/control/GET fixture cases passed. The hardware
raster coherence fixture passed with ordinary rows, bulk readback, and bulk
readback plus timing enabled. It covers CPU edits, padded pitches, RGB565,
surface switches, masked writes and retained snapshots. A CTest entry now runs
the existing fixture with bulk readback enabled. These are regression checks,
not full gameplay/death/replay qualification.

## CPU-only comparison

The controls use `wpr -start CPU -filemode`, 30 seconds of rendered HUD combat,
and the same interior ETL interval `[1,29]` seconds for analysis. Both arms use
the reviewed flip-indexed route, normal barriers, GDI presentation and default-on
GET progress. Coalescing, IRQ gating, GET monitoring, bespoke sampling, WAV,
surface dumps and custom profiling flags are absent. Only the candidate arm
enables the existing bulk-readback option.

Completed HUD frames are reviewed and archived immediately before recording;
screenshots do not overlap tracing. Penumbra is present as concurrent GPU load.
Foregrounding the game and varying desktop work can affect scheduling. One
window per arm cannot establish a controlled FPS improvement or isolate the
contribution of each individual change.

The light control has zero lost events/buffers and 8,070,968 events, versus
approximately 159 million in the previous verbose trace. Identified tracing and
stackwalking account for 2.61% of executor samples, down from 19.78%. Its
executor samples still include 4.73% exclusive `common_getenv_nolock` and 11.14%
exclusive `MoveSmall4`. Kernel `KeQueryPerformanceCounter` is only 0.015% here;
the previous 3.5% figure is not a stable saving estimate. User-mode Rtl QPC is
another 0.277%. Sampling percentages from the two profiling configurations
should not be subtracted to predict untraced frame time.

Both CPU recordings completed with zero lost events and buffers. The expanded
WPA stack trees balance against their sampled thread totals. Executor IDs are
41224 in the control and 30064 in the candidate; both tables use 28 seconds.

| Executor measurement | candidate157 control | candidate163 + bulk |
| --- | ---: | ---: |
| Sampled executor weight | 27,061.96 ms | 27,335.24 ms |
| `common_getenv_nolock`, exclusive | 4.727% | 0.048% |
| `MoveSmall4`, exclusive | 11.142% | 0.406% |
| `memcpy_repmovs_Intel`, exclusive, all callers | 1.609% | 14.257% |
| Readback-copy category, exclusive | 10.779% | 12.673% |
| Map/poll/driver category, excluding identified tracing | 26.011% | 34.389% |
| Texture-comparison category | 11.271% | 10.485% |
| Rtl QPC, exclusive | 0.277% | 0.026% |
| Kernel QPC, exclusive | 0.015% | 0.041% |
| Identified tracing/stackwalking | 2.610% | 2.535% |

The flag hotspot is removed. Large copies replace the small-copy hotspot, but
the copy category does not become cheaper in this pair. There is no established
bulk-readback performance win and no basis to enable it by default. Additional
driver/polling work and variation in scene progression/concurrent GPU load
prevent attributing the overall difference to copying alone. CPU sample
percentages are compositional and these windows do not isolate each change.
Approximately 4–5% of executor samples lack usable stacks in these exports.

Control tracing began near reported flip 29,949; candidate tracing near 29,823.
Both pre-trace frames show 40/500 ammunition and the same beach/HUD view. The
scheduled later camera/fire inputs and wall-clock simulation can still produce
different work inside their 30-second windows. No untraced FPS claim follows.

The subsequent Nsight combat collections are reported below. Preflights are
excluded from combat conclusions.

## Nsight configuration

The user authorized installing Nsight Systems and collecting both arms.
Nsight Systems 2026.5.1.161 was downloaded from NVIDIA's official site; its
Authenticode signature verified as NVIDIA Corporation. Installation succeeded
without restarting Windows. HAGS and other system settings were not changed.

Two D3D11-injected launches failed before the first rendered frame: a direct
launch and a launch through the existing bounded probe with explicit working
directory and independent game logs. A separately labeled startup CPU trace
shows the guest repeatedly yielding through its file-I/O path and the executor
without submitted graphics work. This does not establish the underlying cause
of the launch failure. Neither launch qualifies as a combat trace.

A non-injecting, two-second system-wide WDDM/NVIDIA-metrics preflight succeeded
and exported physical AD10x GPU metrics at 1 kHz. A further system-wide CPU
backtrace preflight stalled during report processing and is excluded. Its
cleanup completed on the user's retry. Desktop control was reset after the
user questioned the persistent indicator; the retry and both combat collections
used command-line tools without further desktop input or focus changes.

The fallback recorded 30 seconds of system-wide WDDM additional events
and NVIDIA GPU metrics, alongside CPU-only WPR. WDDM backtraces and Nsight CPU
sampling/context-switch collection were off. Both arms used candidate163
with bulk readback off; their difference is the existing coalescing switch.
The session UTC epoch in each SQLite export aligns WPR to the same central
28 seconds, excluding the Nsight startup/stop edges. This observes GPU activity
without injecting the game, but supplies no exact D3D11 API-call timings.

## Nsight combat windows

Both runs used the existing flip-indexed combat route, with recording armed
after native flip 29,900. Read-only guest snapshots reported SDK swap serials
29,914 (barrier, PID 26432) and 29,993 (coalesced, PID 2824), stable campaign
roots, no death flag and no reader errors. The previously reviewed HUD frames
remain the visual route reference; no new completed-frame capture was taken
on this CLI-only retry. These are diagnostic route samples, not qualification.

GET progress stayed at its qualified default; monitor, IRQ gate, surface dumps,
sampler, WAV tap and bulk readback were off. Only the coalesced arm enabled
`RECOMP_HW_RELEASE_COALESCE=1`. Neither run changed saved defaults.

| Physical RTX 4070 Ti metric, mean of 1 kHz samples | Barrier | Coalesced |
|---|---:|---:|
| Graphics engine activity | 92.70% | 16.58% |
| SM activity | 54.73% | 6.79% |
| Synchronous copy engine activity | 6.24% | 10.91% |
| DRAM read bandwidth utilization | 7.80% | 5.41% |
| Samples reporting zero graphics activity | 0.029% | 30.50% |

Graphics activity is a busy-time measurement, not shader saturation or our
process's GPU utilization. The barrier window was heavily loaded. WDDM CPU
submission records include 204,121 NVIDIA 3D packets from Penumbra (PID 8172),
102,284 from an unidentified PID 28732 and 16,338 from our process. The
coalesced window has no corresponding Penumbra submission records; Claude,
ChatGPT and DWM also submitted work. Packet counts are not execution-time
shares. No application was closed to create a controlled quiet window.

The barrier's offline WPA exporter also overlapped the coalesced recording,
adding CPU contention. Thus these windows cannot measure a coalescing speedup
or explain the earlier candidate159 timing difference by subtraction. The
global activity difference is confounded by changing concurrent load.

Both reports contain NVIDIA copy-engine and Intel 3D execution records, but
neither contains NVIDIA 3D DMA execution intervals. Nsight emits unresolved
mapping and system-profiling warnings. The physical metrics have 28,001 and
28,000 samples respectively, at approximately 1 ms spacing, but the WDDM
timeline remains incomplete. It cannot establish per-process NVIDIA 3D time,
exact Map durations, or whether a particular drain waits for our draws versus
another process. HAGS remains a hypothesis for the coverage gap. Global busy
activity alone does not prove contention caused our drain delays.

CPU-only WPR was started before Nsight and stopped after its report processing;
its raw ETLs therefore include postprocessing tails. Both ETLs report zero
lost events and buffers. Analyze only WPR [3.5566273, 31.5566273] seconds for
barrier and [3.4191148, 31.4191148] seconds for coalesced: each corresponds to
Nsight [1, 29] seconds. The timestamp-by-stack export was stopped because of
its cost; its partial CSV is excluded. Aggregate stack exports use these same
aligned windows. No point-by-point CPU/GPU polling correlation is claimed.

### Aligned CPU stacks and interpretation

Executor threads 32704 and 36320 respectively have 27,316.65 and 27,405.50 ms
of sampled weight in their 28-second windows. This includes active polling and
kernel yield overhead, not just useful command work. The guest main threads
(10140 and 41392, identified through `WinMain`/guest entry stacks) have 1,684.51
and 7,334.06 ms of sampled weight. CPU sampling does not measure their blocked
time; this follow-up adds no precise-wait or wake-source claim.

Disjoint categories below are percentages of each executor's sampled weight;
identified tracing/stack walking is excluded from the Map category and shown
separately. The snapshot row includes other `memcmp` callers because inlining
prevents identifying every comparison's exact source.

| CPU category | Barrier | Coalesced |
|---|---:|---:|
| Readback Map/poll/driver | 69.74% | 14.71% |
| Vertex packing and draw submission, including driver descendants | 7.67% | 31.25% |
| Texture comparison | 3.97% | 19.31% |
| Readback copies | 4.19% | 3.01% |
| Other sync/copy/compare | 4.12% | 3.54% |
| Other comparisons, including inlined snapshot state | 2.46% | 1.19% |
| Other vertex attribute/cache work | 0.03% | 0.19% |
| Identified tracing/stack walking | 1.66% | 1.56% |
| Other/unattributed | 6.17% | 25.25% |

Inclusive functions overlap; they cannot be added to these categories:

| Inclusive function | Barrier | Coalesced |
|---|---:|---:|
| `synchronize` | 79.19% | 22.11% |
| `map_readback` | 70.76% | 15.48% |
| `SwitchToThread` | 49.21% | 9.61% |
| `hardware_batch` | 16.81% | 54.64% |

Top exclusive functions in barrier mode are `KiSystemServiceUser` (25.74%),
`NtDelayExecution` (14.21%), unresolved NVIDIA user driver (11.82%), D3D11
resource `Map` (7.41%), `memcmp` (6.42%) and `MoveSmall4` (4.30%). In the
coalesced window they are `memcmp` (20.47%), unresolved NVIDIA user driver
(6.31%), `KiSystemServiceUser` (5.22%), `fetch_attr` (5.07%), vertex-cache lookup
(4.89%) and `MoveSmall4` (3.86%). Private driver symbols are unavailable.

For the loaded barrier window, the ranked cost is readback availability
polling/yielding and driver work, then command preparation, then copies and
comparisons. For the less GPU-loaded coalesced window, command preparation and
texture comparison become the largest identified categories, followed by
readback polling. An executor occupying a core is therefore not sufficient
evidence that useful CPU computation alone limits frame rate. The barrier
window's high global GPU activity supports investigating GPU latency and
external load, but does not attribute either to a particular drain. Conversely,
the coalesced window is not a controlled quiet baseline because of the exporter
overlap and remaining applications.

These samples support **texture epochs qualification next**, as the bounded,
already implemented experiment for a measured comparison cost. **Target
residency with CPU-consumer observation** remains the structural next design:
it can remove readback copies and repeated CPU/GPU serialization. Asynchronous
completion could free polling CPU, but a frame-rate gain and safe publication
ordering still need proof. The shared rendering defect stays independent.
No blocking-Map, skip-comparison, skip-readback or skip-drain ablation was run.

NVIDIA documents D3D11 tracing and HAGS context-switch support in the
[Nsight Systems User Guide](https://docs.nvidia.com/nsight-systems/UserGuide/index.html).
HAGS is a possible explanation for the older WPA/GPUView coverage gap, not an
established diagnosis of that ETL.

Raw scripts, ETLs, exports, build/test logs and identities remain in ignored
`analysis/`. The previous [WPR/RenderDoc report](PROFILE_WPR_RENDERDOC.md) remains
the historical record of the verbose trace and frame capture.

`analysis/quick163-verification.json` hashes 15 selected raw/derived artifacts;
`quick163-final-binary-identities.json` confirms the retained EXE/PDB hashes.
The CPU captures are `light163_control-combat.etl` and
`light163_candidate-combat.etl`; their expanded WPA exports and analysis scripts
are preserved. No push or promotion follows from this work.

`analysis/nsight166-verification.json` hashes 34 Nsight/WPR raw files, derived
exports, helper scripts and retained binary identities. Both candidate163 and
the named prototype still match the EXE/PDB hashes recorded above. The new raw
capture identities are:

| Raw artifact in ignored `analysis/` | SHA-256 |
|---|---|
| `nsight165_retrybarrier.nsys-rep` | `35b8d73341e6889dd1c8f32a68ea1f853d1cbf96c2a1ad3b7e31338d8cfc2223` |
| `nsight165_retrybarrier-cpu.etl` | `2f9611c0c2c217768f88373441c2bd3bb4b55c1c38ad73476d7ce9c17071531d` |
| `nsight165_coalesced.nsys-rep` | `296605adbcf78b612ec294e7b07146b03603d2fe6ccd10a53fa5c335909dfb07` |
| `nsight165_coalesced-cpu.etl` | `24c1b1816e6d9a93622e03caa0ea0384fed9d1534c571453fbccf38b6c8ed4a9` |

Next: qualify the existing texture-epoch experiment. Target residency with conservative
CPU-consumer observation remains the structural performance track. The shared
rendering defect and asynchronous completion remain separate work.
