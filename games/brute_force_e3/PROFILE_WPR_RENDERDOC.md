# Combat profile with WPR/WPA and RenderDoc

Captured October 19, 2026; analysis completed October 20. Diagnostic only.

October 20 follow-up: [candidate163 quick wins and Nsight combat windows](QUICK_WINS_163.md)
records the flag/timing changes and lighter CPU captures. Non-injecting Nsight
now measures physical GPU activity, but per-process NVIDIA 3D execution remains
unavailable. Changed concurrent load prevents a barrier/coalesced speed claim;
the earlier results below retain their original measurement scope.

**Recommendation: target residency and CPU-consumer observation next.** This
route spends substantial host CPU on readback/coherence and command preparation,
while a captured frame also performs real GPU work: **16.85 ms** summed GPU
replay duration, including approximately **5.15 ms of staging readback copies**.
The executor is predominantly running, rather than sleeping on GPU fences.
The guest's graphics-event wakeup passes through the timer/DPC dispatcher.

NVIDIA live utilization/contention remains unresolved: both installed WPA and
GPUView expose the Intel compositor and NVIDIA copy activity but omit NVIDIA
3D execution. Missing execution data is not evidence of an idle GPU. This limits
how confidently CPU serialization can be separated from concurrent GPU load.

## Identity, route and recording

The unchanged named **candidate157** was used for both runs. No new build,
runtime hook, counter, default or qualified-policy change was made.

| Artifact | SHA-256 |
| --- | --- |
| `brute_force_e3_prototype.exe` | `27208192a7c5847f5b1a005fec1c2d4772c986a6e1b9050c35f86ccf64c13825` |
| Matching `.pdb` | `894676031cec88006394266920fbfb0114ba62c6590b63c1d11f7c0828c1ed5e` |
| `analysis/wpr161-combat.etl` | `c1873b387e6733fb0ec18b6e8268f0664127d865d2dc5993c755937f5ae29a63` |
| `analysis/renderdoc162-combat_frame29500.rdc` | `98039d2f9fbee880a66a318fa3a5b630896a403046ac69c8808e64a11b4e1bf8` |

Both runs used normal barriers, default-on GET progress, coalescing off and IRQ
gating off. GET monitoring, ordered tracing, bespoke CPU sampling, custom
profiling flags, surface dumps and WAV output were absent. Normal runtime
logging and audio remained enabled. Inputs used the reviewed flip-indexed route:

```text
f4500:start:24,f5700:start:24,f6300:a:24,f6600:down:24,f6900:a:24,
f28000:y:24,f28500:y:24,f29000:lup:12,f29500:rt:24,
f30000:rright:4,f30500:rt:24
```

For WPR, the default GDI presenter was retained. The completed, composited game
window was captured immediately before tracing as
`analysis/wpr161-before-trace.png`: rendered beach/HUD, ammunition 40/500 and the
shared yellow foliage. Its provenance is in the adjacent JSON. No screenshots
or native surface captures occurred during recording. This is evidence of a
completed presentation, not a claim of an exact guest-flip ordinal.

An elevated helper ran the requested command, slept 30 seconds, then stopped:

```powershell
wpr -start GeneralProfile -start CPU -start GPU -filemode
wpr -stop C:\dev\xboxrecomp\games\brute_force_e3\analysis\wpr161-combat.etl
```

The ETL envelope spans 52.475 seconds including rundown. All CPU/wait and WPA
GPU-table numbers below use the **same interior 1–29 s interval**, 28 seconds.
Xperf reports **zero lost events and buffers**. WPR stop/merge took longer than
the recording; its completion timestamp is not the end of the combat interval.
The owned game was stopped after WPR reported that recording had ended.

Tools: WPR 10.0.26100, WPA 11.7.395.48728, GPUView build 26100.9750, RenderDoc
1.46. Symbols used the immutable PDB directory and
`srv*analysis/symbols*https://msdl.microsoft.com/download/symbols`.
Private NVIDIA symbols were unavailable.

## CPU Usage (Sampled): functions and stacks

Percentages here use **each thread's own sampled CPU weight**, not all 24 CPUs
and not wall time. Inclusive functions overlap and must not be added.
WPA exported fully expanded stacks; Xperf butterfly reports cross-checked the
same ETL. Stack exports contain both inclusive nodes and self rows with identical
paths. Analysis takes the maximum per path, then subtracts immediate children;
exclusive totals balance to thread totals. Recursion is counted once for
inclusive function attribution.

The executor is thread **21864** (`nv2a_ack_thread`); guest main is **42620**.
Sampled weights were 27.255 s and 3.825 s respectively. Precise scheduling time,
reported separately below, is preferable for running/blocked wall fractions.

| Executor inclusive function | CPU weight |
| --- | ---: |
| `pb_walk` | 93.52% |
| `nv2a_hw_sync_reason` / `synchronize` | 46.95% / 46.95% |
| `hardware_batch` | 34.69% |
| `map_readback` | 33.83% |
| `draw` | 24.92% |
| `SwitchToThread` | 21.20% |
| `texture` | 11.89% |

| Executor exclusive function | CPU weight |
| --- | ---: |
| `memcmp` | 15.34% |
| `MoveSmall4` (copy routine) | 10.25% |
| NVIDIA user driver, unresolved private functions | 5.26% |
| `common_getenv_nolock<char>` | 4.32% |
| `EtwpLogKernelEvent` | 4.32% |
| `KiSystemServiceUser` | 4.27% |
| `KeQueryPerformanceCounter` | 3.52% |
| `nv2a_vertex_cache_lookup` | 3.05% |
| `fetch_attr` | 2.74% |

Approximately 3.52% of executor weight has no resolved stack. NVIDIA worker
thread **35536** adds 4.194 s sampled CPU; 80.42% of that thread's exclusive
weight is in unresolved NVIDIA user-driver functions. These are driver CPU
costs, not a demonstrated percentage of driver busy-waiting.

For guest main, inclusive `sub_0006F6E0` and `sub_000818B0` account for
76.65% and 76.10% of its CPU weight. `sub_0039EC10`, the SDK graphics wait,
accounts for only 0.40% CPU: its large off-CPU cost is in the precise table.
The PDB names guest functions by address; no unsupported semantic names are
assigned to them.

| Guest-main exclusive function | CPU weight |
| --- | ---: |
| `sub_000A8080` | 6.10% |
| `sub_002CD6A0` | 5.44% |
| `sub_000A5920` | 4.70% |
| `sub_00253C80` | 3.27% |
| `sub_000DD900` | 3.18% |

### Disjoint executor attribution

The following buckets use exclusive sample weights and classify tracing first,
so they do not double-count the inclusive functions above.

| Attributed CPU work | Executor CPU weight |
| --- | ---: |
| Vertex packing, uploads, draw/driver submission, excluding comparisons and tracing | 17.85% |
| Readback map/poll/driver path, excluding tracing | 16.28% |
| Texture `memcmp` | 10.32% |
| Readback CPU copies | 9.77% |
| Other `memcmp`, including inlined snapshot/state comparisons | 5.02% |
| Other synchronization CPU work | 2.63% |
| Other runtime/driver/application and remaining attribute work | 18.35% |
| Identified trace logging / stack walking | 19.78% |

Snapshot validation is inlined into `draw`; other small shader/blend/constant
comparisons share that caller. **Snapshot memcmp is an unresolved subset of the
5.02% bucket**, which does not bound all row-loop/state-validation overhead.
Texture comparison and readback-copy buckets are separated by their callers.
The vertex/submission bucket includes D3D/driver upload work, not just float
packing.

The native readback path performs `Map(READ, DO_NOT_WAIT)`, flushes once when
necessary, then retries with `SwitchToThread`. Thus the known polling/yield path
is visible; it is not equivalent to useful CPU computation. Conversely, opaque
NVIDIA samples cannot be labelled busy-waiting solely because they are in a
driver module.

**Tracing overhead is material here.** Identified trace logging/stack walking
consumes about 5.39 s, 19.78% of executor sampled weight. The exact requested
verbose profiles generated roughly 159 million events. Do not assume a small
fixed WPR overhead or subtract this bucket to predict untraced FPS. Existing
runtime logging also remained enabled.

## CPU Usage (Precise): running, blocked and ready

| Thread | Running CPU | Blocked | Ready, not running |
| --- | ---: | ---: | ---: |
| Executor 21864 | 26.557 s (94.85% of one core) | 1.051 s | 0.395 s |
| Guest main 42620 | 3.784 s (13.52% of one core) | 24.214 s | 0.022 s |

Minor boundary differences prevent these from being exact partitions of 28 s.
Sampled weights also differ from precise scheduling totals; they are different
estimators with interrupt/boundary accounting. The figures are not additive
across the two threads as frame time.

| Blocked cause | Total | Wait count | Longest identified wait |
| --- | ---: | ---: | ---: |
| Executor readback Map path | 873.555 ms | 33,682 | 0.718 ms |
| Executor other GPU sync / driver flush | 54.793 ms | 1,681 | 0.240 ms |
| Executor other object waits | 121.794 ms | 1,447 | 0.649 ms |
| Executor other scheduling/interrupt | 0.584 ms | 18 | 0.130 ms |
| Main SDK graphics event | 24,153.985 ms | 479 | 63.217 ms |
| Main other object/I/O waits | 59.135 ms | 277 | 1.520 ms |
| Main other scheduling/interrupt | 0.429 ms | 24 | 0.058 ms |

For the most frequent executor waits, **NVIDIA worker 35536** readied the
executor through this stack:

```text
NVIDIA user driver [private symbols unavailable]
  -> ReleaseSemaphore -> NtReleaseSemaphore -> KeReleaseSemaphoreEx
```

It accounts for 33,674 of the 33,682 Map-path waits and 873.239 ms blocked time.
The waiting side is `synchronize -> map_readback -> D3D/driver Map ->
WaitForSingleObjectEx`. The 1,681 other sync waits, 54.793 ms, have the same
readying worker; the waiting stack includes `wait_completion -> Flush ->
FlushAndEnqueueSetEvent -> NVIDIA driver -> WaitForSingleObjectEx`.

Other executor object waits were chiefly readied by **timer thread 19120**,
1,433 waits / 120.713 ms, through the runtime's event/notification machinery.
The machine-readable report preserves complete stacks for every cause.

All **479 SDK graphics-event wakeups** of guest main came from **19120**:

```text
kernel_timer_thread -> kernel_drain_dpcs -> kernel_run_dpc
  -> sub_003A4D20 -> sub_003A5120 -> sub_003A53F0
  -> bridge_KeSetEvent -> SetEvent -> NtSetEvent -> KeSetEvent
```

The waiting side is `sub_0039EC10 -> bridge_KeWaitForSingleObject ->
xbox_KeWaitForSingleObject -> WaitForSingleObjectEx`. The existing source review
in `review/opus55/ANSWERS.md` identifies this software-method action-5 signal as
the SDK graphics event, distinct from the vblank event. Mean blocked duration
was 50.43 ms. This is a dependency interval, not a measured DPC-dispatch delay.

**Conclusion:** the simple model “executor mostly sleeps on GPU fences; it
directly wakes the guest” is rejected. The executor mostly runs; its short
kernel blocks are predominantly driver-worker semaphore waits. GPU readiness
is also polled while running. The guest waits on the graphics dependency and
is actually readied by the timer/DPC handler. These stacks do not by themselves
locate when the GPU became ready relative to DPC delivery.

## GPU utilization and concurrent load

Over the same 1–29 s window, WPA's GPU frame-model table reports:

- Intel UHD Graphics 770: `dwm.exe`, **13.764 s GPU packet time**, approximately
  49.16% of this 28 s interval.
- NVIDIA RTX 4070 Ti: **8.723 ms unattributed memory-transfer activity**.
- No usable NVIDIA 3D execution attribution to Brute Force or competing apps.

GPUView was opened on this same ETL, not a different capture. It likewise shows
Intel 3D/composition and a sparse NVIDIA copy queue, but no NVIDIA 3D execution
track. The ETL does contain hardware-queue rundown and newer DMA-packet events;
no loss is reported. A decoder/coverage issue with hardware scheduling is a
possible explanation, not an established cause. The missing track contradicts
any interpretation of its tiny copy total as total NVIDIA workload.

Therefore **GPU busy/idle during drains, NVIDIA per-process GPU-time shares and
NVIDIA contention are unknown from the installed views**. The capture/replay
adapter is independently confirmed as NVIDIA RTX 4070 Ti. Intel compositor time
must not be counted as load on that NVIDIA engine.

Penumbra and WalkingDead do not appear in the exported ETL process-lifetime
roster, despite an earlier preflight desktop inventory showing WalkingDead.
Other desktop/browser activity was present. No user applications were closed,
and no controlled quiet-GPU second trace was obtained. This window must not
inherit the heavy-Penumbra contention assumption from candidate159.

Coverage evidence: `analysis/wpr162-gpuview-coverage.png`; raw WPA GPU table:
`analysis/wpr162-export/GPU_Utilization_Table_GPU_by_Process.csv`.

## RenderDoc: one rendered frame

Approach **(a)** worked without a hook. `nv2a_hw_present_source` returns the
renderer's exact `g.device` and `g.ctx`; the executor passes them to
`nv2a_gpu_present`, whose DXGI swap chain calls `Present` on that device.
Only the separate RenderDoc run used `RECOMP_HW_PRESENT=1`. WPR retained GDI.

RenderDoc was injected at launch and queued **host capture frame 29,500** while
the same guest-flip input script ran. Its thumbnail was reviewed: rendered
beach/HUD, ammunition 48/500 and shared yellow foliage. Host frame ordinals begin
at device creation and have not been proven equal to absolute guest flip
ordinals. This is one guest-presented gameplay frame from the requested route,
not an asserted exact scene match to the WPR screenshot.

Adapter description and replay properties both identify NVIDIA/D3D11.
Three `EventGPUDuration` replay-counter passes gave:

| Replay | Sum GPU action time | Draws | All CopyResource actions |
| --- | ---: | ---: | ---: |
| 1 | 16.445 ms | 11.139 ms | 5.306 ms |
| 2 | 16.999 ms | 11.798 ms | 5.201 ms |
| 3 | 16.851 ms | 11.660 ms | 5.191 ms |

The **median total is 16.851 ms**. These are sums of per-action GPU timestamps,
not live elapsed frame duration. Per-action replay instrumentation, injection,
resource restoration, cache state, clocks and scheduling can distort them.

There are **1,161 draws**, **74 CopyResource calls**, **72 Flush calls**, and
**69 copies into CPU-readable staging textures**: 66 color and three depth.
Those staging copies carry **82,533,376 logical bytes**, excluding row padding
and unknown physical traffic. Their median summed GPU duration is **5.151 ms**.
Four other copies target ordinary textures and one targets the presenter.

The recorded Map stream contains **3,588 upload maps**: 1,354 WRITE_DISCARD and
2,234 WRITE_NO_OVERWRITE. **Read-only maps and unsuccessful readback polls are
absent from this captured stream; their actual call count is unknown.** Do not
report zero actual readback maps or equate upload maps with readback waits.
Native source and ETW establish that staging copies are subsequently read.

Readback copies occur throughout the frame, EIDs **2354–29469**, rather than
only after the final draw. Between successive readbacks there are a median
11 draws, range 0–101. For example, staging copies appear at EIDs 2354, 2409,
2612 and 3303, with further copies continuing up to just before the final
presentation. Exact resources, formats and EIDs are in the JSON summary.

| Most expensive draws | Median per-draw GPU time |
| --- | ---: |
| EID 18676, DrawIndexed | 0.05325 ms |
| EID 18430, DrawIndexed | 0.04710 ms |
| EID 759, DrawIndexed | 0.03277 ms |
| EID 1522, DrawIndexed | 0.03174 ms |
| EID 18761, DrawIndexed | 0.03072 ms |

The most expensive individual actions are staging copies: EID 7910 is about
0.0952 ms, EID 11966 0.0917 ms and EID 16149 0.0863 ms. No single draw explains
the frame cost; many small draws and repeated copies accumulate. No application
pass markers were available, so this report uses EIDs rather than invented
pass names. Per-event medians do not necessarily sum to the median frame total.

Readbacks make the **CPU** wait for prior GPU results before more work is
submitted; they can leave the GPU waiting for the CPU to resume. The interleaved
copies plus native Map retry loop establish those serialization points.
RenderDoc's replay omits the original read-only polling behavior, so it cannot
measure the corresponding live GPU-idle gaps or prove contention at each gap.

Compared with the historical approximately **66 ms/flip**, the 16.85 ms replay
sum is about one quarter of the interval. GPU work alone therefore does not
explain that whole historical interval. However, GPU work is real and of the
same order as the previously reported **12–22 ms drain wall time**; the roughly
5.15 ms of copy work is itself nontrivial. It would be wrong to call all drains
pure latency. The two measurements use different runs/presentation and overlap
CPU/GPU execution, so subtracting them is not a decomposition of live frame time.
The NVIDIA live-attribution gap prevents deciding how much additional delay is
contention.

The shared yellow foliage is present in the RDC thumbnail. The first offending
draw was not isolated, and no rendering fix was attempted.

## Ranked interpretation and next work

1. **Executor throughput and repeated coherence/serialization.** It runs about
   95% of one core. Readback polling, driver interaction, copies and comparisons
   consume substantial CPU; the frame repeatedly copies GPU output to staging
   between draws. This is the strongest combined host/GPU opportunity.
2. **Vertex/input preparation and many small draw submissions.** The disjoint
   packing/submission bucket is 17.85% of executor samples, before the separately
   counted comparisons. There is no isolated expensive GPU draw to remove.
3. **Texture validation and snapshot/state comparison.** Texture memcmp alone
   is 10.32%; remaining memcmp is 5.02%, with snapshot-only attribution unresolved.
   Texture epochs remain worthwhile, but address a narrower measured slice.
4. **GPU draw/copy execution.** Replay totals 16.85 ms, including approximately
   5.15 ms staging copies. It is significant, but does not establish that the
   historical 66 ms interval is GPU-throughput-bound.
5. **Guest wakeup latency/serialization, not heavy guest computation.** Main
   spends 24.15 s in the SDK event over 28 s and is readied by the timer/DPC
   service. That interval overlaps executor/GPU work; it is not an extra cost
   to add to the rows above.

This ranking separates mechanisms; it is not a list of additive live ms/flip.
Identified trace overhead is excluded from optimization priorities, and NVIDIA
contention remains unranked because live execution attribution is missing.

Among the requested choices, **target residency with conservative CPU-consumer
observation is next**: it can attack staging copy work, host copies, snapshot
validation and repeated readiness boundaries together. Retain eager coherence
until observation and alias/lifetime coverage are proven. Texture epochs
qualification is a smaller, independently useful follow-up. Asynchronous
completion requires its own publication/ordering proof and would not by itself
remove copying or the DPC event path. Keep the shared rendering bug separate;
this capture does not show that it dominates performance.

Open questions: recover NVIDIA live execution attribution with a compatible
hardware-scheduling decoder; separate snapshot comparison from inlined state
checks; and locate GPU-ready-to-DPC-event latency using standard trace events.
A future diagnostic ablation skipping texture comparisons or readbacks could
bound a cost that these views cannot isolate, but **no such build was created
or run in this task**. A lighter standard trace would also help quantify the
large observed logging/stack-walking distortion before an absolute FPS claim.

## Evidence and checks

Raw ETL/RDC files and derived exports stay in ignored `analysis/`. Identities,
selected source/export hashes and validation results are in
`analysis/wpr-renderdoc162-verification.json`. Key derived files:

- `wpr162-sampled-summary.json` and `wpr162-waits-summary.json`, with full
  function and readying-stack detail; analysis scripts retain balancing checks.
- `wpr162-stacks-export/CPU_Usage_(Sampled)_Breakdown_by_Process,_Thread,_Activity,_Stack_(1).csv`.
- `wpr162-target-waits-export/CPU_Usage_(Precise)_By_Process.csv`.
- `renderdoc162-replay.json`, `renderdoc162-transfers.json`,
  `renderdoc162-device.json`, `renderdoc162-summary.json` and the RDC thumbnail.

Exclusive CPU and wait-tree totals balance to their thread roots. Replay
counter values are nonnegative and all three passes use the same 1,161-draw
capture. Interrupted/redundant export attempts are preserved but excluded from
findings. No runtime tests or qualification gates are implied by these
measurements. WPR and owned game/export processes were stopped afterward.

This is one route, one 28-second ETW analysis window and one captured frame,
with uncontrolled desktop activity, substantial tracing overhead and incomplete
NVIDIA live GPU attribution. Candidate157 and all qualified policies remain
unchanged. No performance, audio-quality or rendering-correctness improvement
is claimed.

Method references: [WPA CPU analysis](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/cpu-analysis),
[WPA exporter and expansion](https://devblogs.microsoft.com/oldnewthing/20201209-00/?p=104530),
[RenderDoc counter example](https://github.com/baldurk/renderdoc/blob/v1.x/docs/python_api/examples/renderdoc/fetch_counters.rst).
