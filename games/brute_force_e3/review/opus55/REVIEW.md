# Independent review: Brute Force E3 native recompilation performance

Scope: source at `712798a` (identical, modulo CRLF, to the review ZIP), the
candidate137/138 paired evidence, raw event streams, CPU samples, GPU Engine and
NVIDIA telemetry, and the native fixtures/logs. Nothing was built or run; no
game test was launched. Labels used below:

- **[M]** measured in the supplied artifacts (with the caveats stated),
- **[S]** read directly from source,
- **[I]** inference from [M]+[S],
- **[H]** hypothesis that needs the evidence requested in section 8.

## 1. Assessment and recommended next step

**Frame time is set by one host thread, the pushbuffer executor, and almost half
of its wall time is spent on CPU⇄GPU round trips the guest cannot observe.**

- [M] In the candidate138 windows the executor performs about 1,310 draws,
  74 syncs and 66 full 640×480 color readbacks per frame, which is one GPU drain
  every ~18 draws. Sync time is 18.9–20.4 ms of a 44–46 ms frame. The GPU is
  mostly idle: 17–46 % utilization by nvidia-smi, 13–34 % own-process 3D engine,
  and clocks swinging 945–2280 MHz.
- [M] The guest's main thread is in `NtWaitForSingleObject` (via
  `xbox_KeWaitForSingleObject`) in 1044/1196 samples (87 %). The executor thread
  is busy except for ~15 % of samples in `ZwDelayExecution`. The guest is
  starved; it is not the limiter.
- [S] Semaphore values are *not* written to guest memory at
  `BACK_END_WRITE_SEMAPHORE_RELEASE`. `xbox_Nv2aReleaseSemaphore` queues them
  (`xbox_memory_layout.c:804–829`), and they become visible only at
  `xbox_Nv2aFenceBatchEnd` (end of the scanned segment, `nv2a_pb_scan.c:313–320`,
  which already calls `nv2a_hw_complete()` first) or at `xbox_Nv2aFenceBatchFlush`
  (software-method trap, preceded by `nv2a_hw_complete()`). The per-release
  `nv2a_hw_complete()` at `nv2a_pb_exec.c:3100–3106` therefore produces a
  readback whose result the guest can't observe through the fence. The fence is
  read only after a later completion that is already mandatory.
- [S] Every input the host GPU consumes (vertex rows, indices, constants,
  textures, color/depth uploads) is copied into D3D11-owned memory while the
  method is parsed. Pending host GPU work never reads guest memory. Its only
  guest-visible output is color written back to guest RAM; depth is a private
  host buffer (`s_zbufs`). This makes the coherence problem much narrower than
  in a general emulator. It is what makes both section 5's "publication-point"
  completion and true asynchronous completion tractable.

**Recommended next step (one candidate, two parts):**

1. **First, a counters-only, behaviour-neutral build.** Run it in the already
   prepared query-free `-ProductionBenchmark` mode. Attribute every sync to its
   cause (release, trap, wait-for-idle, flip, clear, target transition, zeta
   change, CPU fallback, segment end) and record whether it read back color. Also
   record segments, releases and traps per frame, the trap wait time, and the
   executor's thread CPU time. This is hours of work. It decides whether step 2
   removes 0 or ~50 of the 66 readbacks per frame. Section 7 has the plan.
2. **Then "publication-point coherence".** Keep real completion and color
   coherence at every point where the guest can observe GPU progress: segment
   end, software trap/ISR, flip, a CPU raster or clear, and any release issued
   outside a fence batch. Remove it where the guest can't observe it (release
   inside a batch, `WAIT_FOR_IDLE`). The code change is a few lines behind a kill
   switch. It removes no draw, completion or interrupt. It only stops reading
   back pixels that nobody can see until the next mandatory readback. Candidate127
   is not good evidence against this (section 2.3, item 3).

In parallel, as a separate candidate: **epoch-gated validation** of the texture
cache and the color snapshot (section 4, change 3). It is a certain CPU saving of
up to ~5–9 ms/frame, low risk, and doesn't depend on the sync-cause data.

The **structural** destination is a GPU-resident color path with fault-driven
lazy readback (section 5). It makes publication free and gives true asynchronous
completion without a completion queue. It shouldn't be started before an
observe-mode run measures how often guest code actually touches render-target
pages.

## 2. Where the time goes (question 1)

### 2.1 Measured facts (candidate138, instrumented, 3 × ~30 s windows)

Per-frame values below are derived from the `sync_interval`, `gpu`, `phase` and
`input` events in `perf_candidate138_idle-performance-events.jsonl`. Draws come
from cumulative draw deltas. Report boundaries make these approximate to a few %.

| Per frame | 560–590 | 600–630 | 650–680 |
| --- | ---: | ---: | ---: |
| Frame time (ms) | 44.10 | 45.82 | 45.83 |
| HW draws | 1305 | 1324 | 1319 |
| Syncs | 73.3 | 74.4 | 74.1 |
| Color readbacks (full surface) | 65.7 | 66.6 | 66.4 |
| Sync total (ms) | 18.9 | 20.3 | 20.4 |
| · color Map wait (ms) | 11.6 | 12.5 | 12.5 |
| · color copy (ms) | 5.5 | 5.7 | 5.7 |
| Depth readbacks | 4.0 | 4.0 | 4.0 |
| Depth-only event fences | 3.6 | 3.7 | 3.7 |
| Color allocations / reuses | 5.0 / 1.0 | 5.0 / 1.0 | 5.0 / 1.0 |
| Depth allocations / reuses | 2.0 / 0 | 2.0 / 0 | 2.0 / 0 |
| Pack / "submit" (ms) | 4.1 / 14.7 | 4.1 / 14.9 | 4.1 / 14.7 |

Other measured facts:

- **Phase timers** ([M], `[HW PHASE]` µs/draw, late in the candidate log):
  target 4.2–4.8, texture 4.9–5.2, stream 0.9–1.0, bind/draw 0.9, shader 0.1,
  uniforms 0.1. At ~1,320 draws that is ≈ 14.8 ms/frame, which reproduces the
  "submit" figure.
- **Texture cache** ([M]): 20.18 M lookups with 99.98 % hits (`[HW TEXTURE]`).
  The texture phase is therefore almost entirely *validation*, not uploads.
- **Executor CPU samples** ([M], 1196 SuspendThread samples, 540–555 s):
  - 432 (36 %) are in a tight loop in a module whose nearest exports are
    `memchr`/`_NLG_Return2`;
  - 79 (7 %) are in a second module labelled `strchr`;
  - 155 (13 %) are in d3d11/UMD-labelled exports;
  - 252 (21 %) are in the executable (target, `fetch_attr`, vertex-cache lookup,
    `hardware_batch`, …);
  - 173 (14.5 %) are in `ZwDelayExecution`;
  - 42 (3.5 %) are in `NtWaitForSingleObject`.
- **Guest main thread** ([M]): 87 % of samples are in `NtWaitForSingleObject`
  through `xbox_KeWaitForSingleObject ← sub_0014C180`.
- **GPU** ([M]): see section 1. Utilization and clock anti-correlate, which
  means the GPU is changing P-states under a bursty, serialized load.
- **The instrumented 8.1 % gain** ([M]): pack time fell 5.7 → 4.1 ms/frame with
  the same rows per frame. Input pruning thus accounts for ~1.6 of the ~4.0 ms
  improvement, measured by counters rather than inferred from FPS. Baseline137
  used the legacy cumulative sync format, so the rest of the improvement can't
  be attributed from counters.

### 2.2 Inferences from source

- **[S][I] Serial pipeline.** `nv2a_ack_thread` (`xbox_memory_layout.c:1095–1305`)
  works like this:
  - it polls `DMA_PUT` and scans `[last_put, put)` synchronously;
  - it advances `DMA_GET` only after the whole segment ran (line 1246), including
    every sync;
  - fences publish at segment end.

  The guest therefore waits on executor *throughput and latency*, and the
  executor idles (`Sleep(0)`, line 1299) while the guest wakes and builds the
  next segment. The ~15 % `ZwDelayExecution` share is most likely that poll. No
  stack was captured for this thread, so a driver-internal sleep can't be ruled
  out ([H]).
- **[S][I] Cost of one sync** (`synchronize`, `nv2a_hw_raster.c:321–396`):
  - unbind, then `CopyResource` of the full target, then `Map(DO_NOT_WAIT)`,
    `Flush`, and a `SwitchToThread` spin until real completion;
  - copy every row into guest memory *and* into a private snapshot (two 1.2 MB
    writes);
  - at the next color-writing draw, `target()` runs a full 1.2 MB `memcmp` of
    guest memory against that snapshot (`nv2a_hw_raster.c:441–447`).

  One readback therefore costs ~4.8 MB of CPU memory traffic (two 1.2 MB
  writes, and two 1.2 MB reads for the compare) plus a GPU drain. At 66/frame
  that is ~300 MB/frame from the readback/snapshot protocol alone.
- **[S][I] Texture validation.** On every draw, for each of 4 stages,
  `texture()` (`nv2a_hw_raster.c:509–511`) first scans the 512-entry cache, then
  `memcmp`s the *entire* raw guest texture (and palette) against its private
  copy. With a 99.98 % hit rate this is the whole texture phase (~5 µs/draw,
  ≈ 6.5 ms/frame).
- **[I] The 36 % `memchr`-labelled loop** is consistent with these
  `memcmp`/`memcpy` volumes (vcruntime-like export names). Without symbols I
  can't confirm it isn't driver code polled inside `Map`. Section 8 requests ETW
  stacks.
- **[I] Map wait isn't transfer cost.** The 7.7–8.1 s/interval "color-map wait"
  is GPU drain plus submission latency plus clock state. A 1.2 MB PCIe copy is
  well under 0.1 ms. Fewer syncs also reduce exposure to DVFS: each drain
  currently pays for a GPU that has clocked down while idle.
- **[I] Approximate accounting of a ~45 ms frame**, executor view, not additive
  to the decimal:
  - ~7 ms idle waiting for the guest;
  - ~18 ms sync not already counted in submit;
  - ~15 ms "submit", which includes target-transition syncs (≈1.6 ms), snapshot
    `memcmp`, uploads and texture validation;
  - ~4 ms pack;
  - ~1–2 ms parsing.

  This closes approximately, which supports, but does not prove, the attribution.

### 2.3 Misleading instrumentation or conclusions

1. **"Submit ≈ 14.7 ms" is not driver submission.** It is the entire
   `nv2a_hw_draw_raw` call (`nv2a_pb_exec.c:2251–2253`): target-transition syncs
   and uploads, the snapshot `memcmp`, texture validation, the 32 MB/frame vertex
   upload, and D3D calls. The phase counters show bind/draw is only ~1.2 ms/frame.
2. **"Color readbacks remain mandatory" (PERFORMANCE.md) overstates the
   requirement.** They're mandatory *before publication*. Section 1 shows that
   releases inside a batch are not publication points.
3. **"An earlier attempt to defer that barrier failed" is confounded.**
   Candidate127's first run is recorded as a *movie stall from 244 s*. That is
   the same class of stall later reproduced on the **original verified bytes**
   (baseline133, Bink/DirectSound status poll). The second run "reached training
   then stalled" with no recorded signature in the bundle. Two stalls with mixed
   signatures, against a known nonzero stall base rate, don't establish that
   deferral caused either.
4. **GPU timestamp tracing perturbs the very path being measured.**
   `RECOMP_HW_GPU_TRACE` issues two timestamp `End`s per draw for the first 64
   draws after each sync. `trace_gpu_finish` then does up to 129 `GetData` calls
   per sync. At ~18 draws/sync and 74 syncs/frame that is ~2,600 query
   operations per frame, concentrated in the sync path. `[GPU SLOW DRAW]`
   samples only the first ≤64 draws after a sync, so it is biased toward
   post-drain draws and can't describe the target mix. It recorded only
   640×480 targets, yet 5 color allocations/frame imply dimension or bpp changes.
5. **`RECOMP_GPU_TIMING` is CPU-only (QPC).** It can be kept in a "counters"
   configuration without GPU queries, so phase data needn't be given up to get
   query-free timing. Splitting the two configurations is cheap and valuable.
6. **`[GPU] … 0 pixels written … brightest pixel 0x00000000` are CPU-rasterizer
   cumulative statistics.** They are frozen in HW mode. "Rasterised N
   triangles" counts HW batches. Neither says anything about HW output.
7. **One run per arm can't resolve 8 % reliably.** Baseline137's own windows
   span 47.50–50.56 ms (6 %). With natural DVFS swings and ~6 nvidia-smi samples
   per window, the 8.1 % is plausible and partly corroborated by counters (pack),
   but it isn't established. Interleaved repeated runs are needed (section 7).
8. **CPU-sample symbols are nearest exports.** `memchr+249`, `strchr+983`,
   `D3DKMTOpenResource+860671`, `NVDEV_Thunk+5763723` and similar must be
   treated as module buckets.

## 3. Correctness review of implemented changes (question 2)

Overall, the implemented changes preserve the stated invariants. No draw is
skipped, no completion is fabricated, and publication is still preceded by real
completion. The items below are risks and residual holes. Most are pre-existing
and become important if coherence is relaxed further.

**Managed depth**

- Depth never lives in guest memory. It is the private `s_zbufs` with stride
  1024 (`nv2a_pb_exec.c:1598–1638`). Managed retention therefore can't create
  guest-visible staleness. All CPU consumers sync first: the zeta change at
  `hardware_batch:2193`, CPU fallback at 2367/2426, and clear at 3194. I traced
  the zeta change → `zbuf_current(1)` slot-reuse ordering. The download into the
  old buffer happens before any slot can be recycled, so it is safe.
- Pre-existing: four slots are recycled round-robin (`s_zbuf_next & 3`), not
  LRU. A title with more than four live zeta surfaces silently loses depth
  contents. This is unchanged by the candidate and worth a counter.

**Snapshots and resource reuse**

- **Lost-write window** (pre-existing, widened by any coalescing): the snapshot
  check runs only at the first color-writing draw after a sync
  (`nv2a_hw_raster.c:441–447`). Guest CPU writes to the target after that draw
  and before the next sync are overwritten, because `synchronize` writes back
  the whole surface (lines 362–370). D3D-mediated `Lock`s are safe: they wait
  for the fence, which publishes only after a sync, and the next segment's first
  draw detects the change. Non-blocking locks (no-overwrite style) that write
  pixels the GPU never touches would be lost, whereas real hardware would keep
  them. Page write-protection (section 5) is the only exact fix.
- **Same-size reuse** (`target()` 403–412) correctly discards content validity
  and re-uploads. If the first draw has color writes masked off, the upload is
  deferred while `g.target` already names the new surface. That is safe because
  `color_loaded=0` forces the upload before any color-writing draw, and
  `present_source` requires `color_loaded`.
- **Partial allocation failure** (color RTV/staging, depth DSV/staging/upload):
  I checked whether a half-built object set could be reused. The failure path
  leaves `g.target`/`g.depth_target` zeroed, so the next call takes the
  retire/drop path and doesn't reuse it. That is safe as written, but fragile.
  Validate the full object set wherever reuse is decided, not just `g.color` or
  `g.depth`.
- **Alias identity**: target and feedback identity compare *host pointers*
  (`nv2a_hw_raster.c:403`, `nv2a_pb_exec.c:2226–2228`). The contiguous window
  and the tiled aperture are two views of one section (`xbox_memory_layout.c`
  ~2519, ~2873). A surface referenced through the tiled alias and sampled
  through the contiguous one would escape the feedback check. Both currently
  resolve through `dma_resolve`, so this is latent. Canonicalize to the
  contiguous-section offset before any residency work.

**Conservative vertex-input pruning**

- `nv2a_vsh_input_mask` (`nv2a_vsh_hlsl.c:12–25`) mirrors `source()`'s
  multiplexer decoding and END detection exactly. Programs without END gather
  everything. The CPU fallback (`transform_vertex`) still fetches all 16 inputs.
  I found no divergence.
- Risk is future drift. The mask and the HLSL generator are separate walks.
  Derive the mask *from the generator* (record `inputs[i]` while emitting), so
  they can't diverge, and cache it per vertex-shader slot.
- Rows still upload 256 B per vertex (16 × float4). Unreferenced slots carry
  stale data from earlier draws. That is harmless to the GPU, but it is
  ~32 MB/frame of copying (section 4, change 6).

**Failure handling and completion frontier**

- Prefix/suffix semantics are coherent. A failed batch publishes nothing, and an
  earlier trap flush keeps its prefix.
- `DMA_GET` still advances after a failed segment (line 1246). That is correct
  for command consumption, but **D3D device removal** then becomes an indefinite
  guest hang with no publication and no diagnosis. Make device removal an
  explicit fatal error with a clear message. This matters more as GPU-resident
  content grows.
- `completed_draws` and the depth-only event path are correct. D3D11
  immediate-context work completes in order, so a mapped copy implies prior
  draws finished.
- **Software-trap latency** ([S]). The executor blocks (`INFINITE`) until the
  timer thread services the ISR (`kernel_bridge.c:2312–2323`). That thread waits
  on the wake event with a 10 ms timeout, and checks completion only after
  `kernel_drain_dpcs` (2665–2674). If the guest's acknowledgement of bit 20
  doesn't land in the same iteration, a trap costs up to ~10 ms. The 3.5 %
  executor `NtWaitForSingleObject` share bounds today's average at ~1.6 ms/frame.
  Count it before optimizing.

**GPU presentation**

- `nv2a_hw_present_source` is sound. It uses the GPU texture only when it
  matches a completed, snapshot-validated target, and adds a full-frame `memcmp`
  per flip.
- It *can't* help today. `FLIP_INCREMENT_WRITE` still does
  `nv2a_hw_complete()` (readback) first. The only work saved is the GDI-side
  copy (`xbox_FramebufferWindowPresent` copies the frame; the window thread
  blits at ~60 Hz). The expected effect is far below the between-window noise.

## 4. Ranked next changes (question 3)

| # | Change | Kind | Likely payoff (executor ms/frame) | Effort | Risk | Decide with |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Sync-cause counters + query-free production pair | evidence | enables 2 and 4 | hours | none | §7 phase 0 |
| 2 | Publication-point coherence (2A); then multi-target native cache flushed at publication points (2B) | structural-lite | 2A: 0 to ~15 (depends on releases per publication); 2B: +2–4 (target ping-pong, ~5–6 switches/frame) | 2A days, 2B 1–2 weeks | moderate | releases/segment, syncs by cause |
| 3 | Epoch-gated texture/snapshot validation + hashed texture lookup | incremental CPU | ~5–9 (upper bound: target + texture phases ≈ 12–13) | days | low | phase µs/draw, bytes compared/frame |
| 4 | GPU-resident color + fault-driven lazy readback + immediate publication + flip throttle (+ direct present) | structural | most of the remaining sync (~15–20) plus CPU/GPU/guest overlap | weeks | high | observe-mode fault rates |
| 5 | Small: GPU-side clears; target pool judged by counters; vertex-row compaction; trap-wake latency | incremental | ~1–2 each | days | low–moderate | per-item counters |

**Change 2A: publication-point coherence.** Behind
`RECOMP_HW_RELEASE_BARRIER=1` (restore the old behaviour):

- At `0x1D70`, if `xbox_Nv2aFenceBatchActive()`, record only that a release is
  pending. Otherwise complete immediately; this is the test harness path, where
  the store is immediate.
- Do the same for `WAIT_FOR_IDLE` (`0x110`).
- Keep `nv2a_hw_complete()` at segment end (already conditional on
  `HasRelease`), before the trap flush, at the flip, and before any CPU raster
  or clear.

Why it is correct: the stores in `g_pending_fences` are written only by
`FenceBatchEnd`/`FenceBatchFlush`, and both are already preceded by real
completion. A release inside a batch has no guest-visible side effect.

**Change 2B** keeps up to N native targets alive. It flushes dirty ones
(readback) at publication points, and reads one back on demand when a texture or
CPU path overlaps it. This removes the A→B→A full readback/upload ping-pong at
target switches. It needs no page protection because it relies on the same
publication argument. It does widen the lost-write window in section 3; that is
acceptable only for D3D-mediated access, so it must be gated by evidence.

**Change 3: epoch-gated validation.** The rule: guest CPU writes that must be
visible to a command were made before the `DMA_PUT` store that submitted it.
x86 store ordering, plus the executor reading PUT before scanning, therefore
guarantee that validation at a texture's *first use after the executor last read
PUT* sees every write it must see. Later writes are unordered against the
segment's commands, so either outcome is a legal hardware timing.

- Bump a global epoch:
  - at each segment start (`nv2a_pb_scan`);
  - after `xbox_Nv2aSoftwareNotify` returns, because the guest ISR/DPC ran
    synchronously with the executor;
  - whenever host code writes guest memory: `synchronize`, CPU clear/raster,
    `GET_REPORT`.
- Texture entries and the color snapshot store the epoch they were last
  validated at, and skip the `memcmp` within the same epoch.
- Replace the 512-entry linear scan with a hash on `(raw, format, w, h, …)`.

This removes most of the 66 snapshot `memcmp`s and most texture `memcmp`s per
frame while keeping byte-exact freshness at every ordering point.

**Change 5 details.**

- Clears currently force a full sync and then a CPU clear, followed by full
  color *and* depth re-uploads at the next draw (~2/frame). A GPU-side clear
  (`ClearView` with the clip rect, or a quad that honours the color mask and
  RGB565) keeps the target native.
- The dimension pool's earlier verdict was drawn from noisy FPS windows. Judge
  it by `color_alloc`/`depth_alloc` and target-phase µs/draw instead.
- Vertex rows: use per-mask input layouts, uploading only referenced attributes
  (~3.9 of 16 on average per `[HW GATHER]`).

## 5. GPU-resident color with selective readback (question 4)

**Viable, with a concrete design.** It is easier here than in a typical
emulator, for three reasons:

- no pending host GPU work reads guest memory (section 1);
- render targets live in the contiguous section, which has exactly two guest
  views (contiguous window and tiled aperture);
- the D3D11 context has a single owner thread.

### 5.1 State per resident target

The key is the canonical contiguous offset plus pitch, w, h and bpp. Only
32-bit linear targets are eligible; RGB565 keeps the eager path. Each target is
in one of three states:

- `CPU_DIRTY`. Pages are RW. The GPU copy is stale and is uploaded before the
  next GPU use. Track dirty pages from write faults so only dirty rows are
  uploaded.
- `SHARED`. Bytes are identical. Pages are `PAGE_READONLY` in *both* views. A
  write fault marks the page dirty, sets it RW and continues. No D3D call, no
  lock.
- `GPU_DIRTY`. Set by any draw. Pages are `PAGE_NOACCESS` in both views. Any
  access faults, which requests a readback that waits on a real `Map`. The
  readback writes the bytes through a **third, runtime-private, never-protected
  view** of `g_contig_mapping`, then sets both guest views to `PAGE_READONLY`
  (`SHARED`), then releases the waiters. A retried write then faults once more
  and becomes `CPU_DIRTY`.

The private view means guest threads never see a partially written surface, and
no protection window is opened during the copy. Keep generation counters (GPU
write generation, CPU generation) so that storage reuse is never confused with
content validity, as the current code already does.

### 5.2 Fault servicing and deadlock avoidance

- **Non-owner threads** (guest threads, timer/ISR thread, window thread, audio)
  enqueue a request and wait on a per-request event. The VEH does no D3D calls,
  no allocation and no host locks.
- **The executor services requests:**
  - at its loop top (replace `Sleep(0)` with a wait on {request, work});
  - once per method in `pb_walk` (one relaxed load);
  - inside every wait where it can block on guest progress. Today that is
    `xbox_Nv2aSoftwareNotify`'s `WaitForSingleObject(done)`, which must become a
    `WaitForMultipleObjects` loop that also services requests. Otherwise an ISR
    touching a resident target deadlocks. Audit every executor wait (`grep` for
    waits reachable from `nv2a_pb_exec_method`).
- **Owner-thread host code never relies on faults.** That covers `texture()`,
  `target()`, present, dump, and the CPU clear/raster paths. Each calls an
  explicit `coherence_acquire(range, access)` before touching guest bytes. A
  fault on the owner thread is a bug: log it, service it if not inside a D3D
  call, otherwise fail loudly.
- **IRQL and guest locks.** The executor never takes guest locks, so a faulting
  thread at raised emulated IRQL or holding a guest spinlock can only deadlock
  through an executor wait on guest progress. That is exactly the set above.
- **Coexistence with the existing VEHs** (`pfb_write_veh`, `irq_write_veh`,
  `ac97_write_veh`, `watch_veh`): claim only addresses inside a registered
  protected range whose state is protected; otherwise `CONTINUE_SEARCH`.

### 5.3 Must-handle cases

- **Kernel-mode accesses don't fault; they fail.** `xbox_NtReadFile` passes the
  guest buffer straight to `ReadFile` (`kernel_file.c:249`). On a protected page
  that returns `ERROR_NOACCESS`. Every bridge that passes guest pointers to
  Win32/NT APIs needs a `guest_range_acquire()` (an interval-set check, cheap
  when nothing is resident). The same applies to diagnostics: `inspect_guest.py`'s
  `ReadProcessMemory` will fail on protected pages.
- **Address reuse.** Free and remap hooks (contiguous free, virtual free/protect
  HLE) evict overlapping resident targets: read back first, because freed
  memory keeps its bytes on hardware. An unrelated reuse without a free costs at
  most one readback plus a transition to `CPU_DIRTY`. Correct, bounded.
- **Shared edge pages** (targets not page-aligned at both ends). Accept a
  conservative sync. If a target's shared pages fault more than K times per
  frame, demote it to eager mode for the rest of the epoch or level.
- **Native sampling, mips and feedback.**
  - Phase 1: `texture()` acquires (reads back) any overlapping `GPU_DIRTY`
    target, which is correct and no faster for render-to-texture.
  - Phase 2: linear A8R8G8B8 (0x12), one level, exact base/pitch/extent match →
    bind the target's SRV. This needs `BIND_SHADER_RESOURCE`, an SRV of
    `Texture2DArray` with size 1, and an explicit unbind before re-binding it as
    an RTV.
  - Feedback (sampling the bound target) keeps today's rejection/CPU path.
    Swizzled or mipped overlaps read back.
- **Eviction and shutdown.** LRU-bounded residency set. Eviction, `nv2a_hw_shutdown`
  and process exit read back every `GPU_DIRTY` target, restore RW in both views,
  and unregister.
- **Device removal.** `GPU_DIRTY` content since the last readback is lost. Log,
  restore pages RW with the last coherent bytes, and stop. Don't continue
  silently on stale data.

### 5.4 Kill switches and fallback

Use a global environment switch and per-target demotion to the eager path.
Unsupported formats, RGB565, partial-page pathologies, any acquire failure and
any VEH registration failure all fall back to today's behaviour.

### 5.5 What must be proven before enabling

1. **Observe-mode run (diagnostic build, not a candidate).** Keep today's eager
   readback. After each readback, set the target `PAGE_NOACCESS`. On a fault,
   record thread, host RIP and read/write, then unprotect for the rest of that
   epoch. Owner-thread code unprotects explicitly before its own accesses, and
   the file I/O bridges acquire first.
   - Output: for every phase (menus, movies, beach, combat, death/replay, HUD),
     the number of readbacks per frame that were followed by *any* non-owner
     access before the next GPU write.
   - Decision rule: if that number is ≤ 1–2 per frame everywhere, residency is
     worth building. If the title reads targets constantly, it is not.
2. **Fixtures:**
   - multithreaded reads and writes through both aliases while `GPU_DIRTY`;
   - partial and shared pages;
   - write-then-read and read-then-write;
   - a fault during a software-trap ISR (deadlock test);
   - `ReadFile` into a resident target;
   - address reuse and free;
   - eviction, shutdown restore and recreate;
   - pitched targets;
   - an injected acquire failure;
   - exact presented pixels.
3. Then the full gameplay gates, run more than once (section 7).

## 6. True asynchronous completion (question 5)

**[S][I] The key point:** the host GPU never reads guest memory after a method is
parsed, and depth is private. The only effect of GPU work that the guest can
observe is the color bytes. Once color is resident with fault-driven coherence,
semaphores and ISRs can be published *at parse time*. Any guest observation of
GPU output faults, and its readback waits on real `Map` completion, so the
observable order "fence visible ⇒ results visible" is preserved.

- **No staged snapshots or per-release frontiers are needed.** A lazy readback
  returns the results of everything parsed so far, which is a superset of the
  fence's prefix. That is a legal hardware timing: the GPU could have finished
  those already-submitted commands too.
- **Guest-side reuse of command, vertex-buffer or `DMA_GET` storage is already
  safe at parse time**, because inputs were copied.
- **Backpressure.** Without per-release drains the executor can run ahead. Insert
  an event query at each flip, and before passing flip k + 2 wait for flip k's
  query to report real readiness. This bounds the lag to two frames without
  inventing completion. DXGI `Present` throttling also applies if direct
  presentation is used.
- **Prefix/suffix failure semantics are unchanged for parser failures**, which
  are still synchronous at parse. D3D errors other than device removal don't
  surface per draw. Device removal is fatal in both designs (section 3).
- **Why "earlier deferral failed" isn't evidence against this.** That attempt
  deferred a barrier without changing coherence. Its two stalls are confounded
  by the pre-existing movie/DirectSound stall (section 2.3, item 3). Section 8
  asks for its diff and snapshots.

## 7. Direct presentation retest; experiment and regression plan (questions 6 and 7)

### 7.1 Should direct presentation be retested first?

**Not as a standalone performance experiment.**

- [S] It can't remove the flip readback or the per-flip snapshot `memcmp`.
  [I] Its executor saving is a sub-millisecond copy, far below the 6 %
  between-window spread.
- Include it as a third arm only if machine time is cheap. If you do, make the
  comparison fair:
  - same binary with an environment toggle;
  - query-free;
  - interleaved repeated runs;
  - per-window executor CPU time from `GetThreadTimes` as the primary metric,
    with FPS secondary.
- It becomes *necessary*, and its resize/minimize/focus gates matter, only under
  residency, where the flip no longer reads back.

### 7.2 Plan for the preferred next step

**Phase 0: counters-only build `perf140`** (candidate132 source plus counters, no
behaviour change). Add the following, all cheap increments or QPC, no GPU
queries:

- syncs by cause × {color readback, depth readback, event-only, no-op};
- segments, releases, traps, `WAIT_FOR_IDLE`, flips and clears per frame;
- releases per segment (histogram);
- trap wait ms;
- executor thread CPU ms per report (`GetThreadTimes`) and the ack-loop idle
  count;
- bytes `memcmp`'d per frame (snapshot vs texture);
- target (w, h, bpp) transition histogram.

Validate it as follows:

- native fixtures unchanged and passing;
- three interleaved query-free runs each of 132 and 140 show work counters
  (draws, rows, flips per frame) within 1 %;
- frame time within run-to-run spread. This bounds the counters' overhead.

**Go/no-go for change 2A:** if release-caused color readbacks minus
publication-caused readbacks is ≥ ~20 per frame, proceed. If releases are
essentially one per segment, skip 2A and go to change 3, then the observe-mode
run for residency.

**Phase 1: candidate `perf141` = 2A (plus 3 as a second flag, measured
separately).** Defaults on, kill switches available, so A/B uses one binary.

Native tests (extend `semaphore_test.c` and `raster_test.c`):

- a release inside a batch, more draws, then batch end: the fence is published
  only after completion, and surface bytes at publication equal the expected
  pixels;
- a release then a trap: completion and coherent color before the ISR;
- a release outside a batch: immediate completion;
- a failure after a release: nothing published, prefix semantics unchanged;
- a target switch inside a batch: both targets coherent at publication.

For change 3:

- a CPU texture edit between segments → re-upload;
- a texture edit by a trap ISR mid-segment → re-upload;
- a readback target sampled later in the same segment → re-upload;
- a CPU target edit before the next segment's first draw → re-upload.

Performance comparison:

- query-free `-ProductionBenchmark`, same route and windows;
- ≥ 3 interleaved ABAB pairs, flag on versus off;
- record per-window NVIDIA clocks and power and own-process 3D load.

**Success criteria:**

- the mean frame-time reduction exceeds max(2 × pooled between-run SD, 5 %);
- executor CPU ms/frame drops consistently;
- work counters are equal within 1 %;
- syncs per frame drop as predicted by phase 0;
- the paused-scene completed-flip captures match the same arm's own
  run-to-run behaviour, or are byte-identical if the off arm is deterministic.

**Correctness gates.** Run the 2000 s audio-enabled route (combat, damage/death,
mission replay, returned HUD, resumed movement/firing), query-free, at least
twice per arm. The original bytes also stall intermittently, so classify every
stall by snapshot signature before attributing it. Snapshot contents: all guest
stacks, the fence slot value versus pending fences, GET/PUT, NV2A interrupt bits
and the notify state. Then the launcher close test and the native suites.

**Failure criteria:**

- any stall whose snapshot shows the guest waiting on the graphics fence or
  event with GET == PUT and a stale fence;
- any visual divergence in captures;
- no improvement beyond noise.

On failure, leave the flag off by default and document it.

**Rollback.** Never overwrite the verified `43d7…` executable or candidate132.
Give each candidate a distinct basename plus PDB, and record its SHA256 in
`build-verification.json` before it runs. `play.ps1` keeps launching the
verified build. Kill-switch flags allow in-binary rollback. Never rebuild a
binary that has a running process.

## 8. Additional evidence requested

1. Candidate127's exact source delta, and the stall snapshots from both its
   runs: guest thread stacks, the fence slot versus pending values, GET/PUT,
   `0x400100`/`0x100` interrupt bits, and the notify state.
2. The phase-0 counters above from one query-free paused run, and from one
   combat/replay run.
3. ETW (WPR) CPU sampling with stacks and the symbol server for the executor
   thread over a paused window. This resolves the `memchr`/`strchr`/UMD buckets,
   shows where `Map` waiting is actually spent, and tells the ack-loop
   `Sleep(0)` apart from driver sleeps.
4. What `sub_0014C180` waits on, what signals it (ISR/DPC, bridge), and a
   histogram of wait durations.
5. For residency: the observe-mode fault statistics per game phase (section 5.5).
