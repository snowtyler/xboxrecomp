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

Combat Nsight results remain pending; the preflights below are excluded from
combat conclusions.

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
cleanup helper is waiting on Windows UAC consent. Desktop control was reset
after the user questioned the persistent indicator; no further desktop input
was issued. The waiting normal game was stopped without arming combat capture.

The prepared fallback records 30 seconds of system-wide WDDM additional events
and NVIDIA GPU metrics, alongside CPU-only WPR. WDDM backtraces and Nsight CPU
sampling/context-switch collection are off. Both future arms use candidate163
with bulk readback off; their difference is the existing coalescing switch.
Use the session UTC epoch in the Nsight SQLite export to align the WPR interval.
This can observe GPU activity without injecting the game, but does not supply
exact D3D11 API-call timings. Per-process NVIDIA 3D coverage still needs checking.
No claim about combat GPU idle gaps or contention follows from these preflights.

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

Next: complete the two non-injecting combat collections after cleanup, then
qualify the existing texture-epoch experiment. Target residency with conservative
CPU-consumer observation remains the structural performance track. The shared
rendering defect and asynchronous completion remain separate work.
