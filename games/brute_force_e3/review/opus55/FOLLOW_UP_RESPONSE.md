# Follow-up review: path to a stable 60 FPS

Reviewed at `84c3097` (main). Inputs: `FOLLOW_UP.md`, `PERFORMANCE.md`,
`PERFORMANCE_NEXT.md`, and the source diff since `REVIEW.md`. Raw logs for
candidates 140–149 are not in the repository, so the measured numbers below come
from the summaries in `PERFORMANCE.md`. Labels:

- **[M]** measured (as reported),
- **[S]** read directly from source,
- **[E]** estimate,
- **[H]** hypothesis.

## Ranked findings

1. **[S] Release coalescing cannot change the order in which fences or interrupts
   are published.** In both modes:
   - semaphore stores go to `g_pending_fences` (`xbox_memory_layout.c:804–829`);
   - they become visible only in `xbox_Nv2aFenceBatchEnd` or
     `xbox_Nv2aFenceBatchFlush`;
   - both of those already sit behind a completion (`nv2a_pb_scan.c`, segment
     end; `nv2a_pb_exec.c`, trap).

   With `RECOMP_PB_EXEC` this title registers only the semaphore binding. The
   PUT fence mirror sits in the `else` branch in `src/main.c:400–412`, so
   releases are the only publication path. The candidate147 stall must therefore
   come from one of three things:
   - **(a)** a batch failure that suppresses publication, including the
     interrupt;
   - **(b)** a timing-exposed race in trap/ISR delivery;
   - **(c)** a reader of mid-segment color bytes that nothing orders.

   §1 gives the trace that tells these apart.
2. **[S] Candidate147's flag changed three behaviours at once.** A single
   environment flag turned on:
   - release-barrier removal;
   - input-acquisition guards: a check on every pushbuffer word, NOP record,
     vertex range, texture and palette;
   - a completion before `GET_REPORT`.

   The guards also add new paths that call `xbox_Nv2aFenceBatchFail` or
   `pb_reject`. Any of those turns a guard problem into a silent loss of fences
   and interrupts. Split the flags before the next attempt (§1.3).
3. **[S] A failed batch silently suppresses the software interrupt.**
   `if(!xbox_Nv2aFenceBatchFlush())return;` in the `0x0100` handler consumes the
   trap without delivering it. With releases as the only publication path, this
   gives exactly the reported symptom: the guest blocks on its graphics event
   and the executor sits idle. A batch failure is now a terminal, unrecoverable
   condition, so it should be loud and fatal, like device loss. Today it is a
   silent hang.
4. **[S] Pre-existing race in interrupt write-one-to-clear (W1C) emulation.**
   `irq_write_veh` (`xbox_memory_layout.c:283–310`) handles a write by making the
   whole page `PAGE_READWRITE` for every thread, single-stepping, and then
   re-protecting. While the page is unprotected, another thread's W1C write to
   `0x400100` or `0x100` becomes a plain store and can set bits instead of
   clearing them.

   `kernel_nv2a_notify_complete` waits for bit 20 to clear, and the executor
   waits `INFINITE` (`kernel_bridge.c:2321`). A stuck bit therefore hangs the
   executor inside `xbox_Nv2aSoftwareNotify`. A faster executor changes this
   race's timing. This is a candidate for cause (b); it is not proven.
5. **[S] Vblank pacing is quantized to the 15.6 ms Windows tick.** The normal
   launcher sets `RECOMP_VBLANK=1`.
   - `kernel_vblank_tick` schedules `next_ms = now + 16` using `GetTickCount64`.
   - Its thread waits `WaitForSingleObject(...,10)` with no high-resolution
     timer (no `timeBeginPeriod` or high-resolution waitable timer anywhere in
     the tree).
   - On the default 15.6 ms tick, vblanks therefore fall on 15.6/31.2 ms
     boundaries: below 60 Hz and jittery [E].
   - If the title paces presentation on vblank, this caps throughput well below
     60 FPS once the renderer gets faster. It must be measured and fixed before
     any 60 FPS claim (§7).
6. **[S] Benchmark manifests don't record the full configuration.**
   `probe.py` writes a fixed key list to `-process.json`. It omits
   `RECOMP_VBLANK`, `RECOMP_TITLE_KEVENTS` and the `RECOMP_USB*` keys, which
   `probe_runtime.ps1` and `play.ps1` set. Record every `RECOMP_*` and
   `BRUTE_FORCE_E3_*` variable from the actual child environment.
7. **[M] The epoch result confirms the cost model; the executor is still the
   limit.**
   - Texture-epoch validation measured 52.6 → 41.4 ms (21 %).
   - Executor CPU went from 50.3 to 39.0 ms/frame, so CPU time accounts for
     94 % of the frame.
   - Removing per-segment validation work moved frame time almost 1:1.
   - The next ~25 ms must come from the release readbacks, the snapshot
     compares (74 MB/frame), per-draw submission, and vertex handling.
8. **[S] The input-acquisition guards fix a gap that also exists in barrier
   mode.** In barrier mode, a draw whose output is consumed by the CPU later in
   the same segment, with no release in between, is not coherent either. Make
   the guards an independent flag, qualify them in barrier mode first, and only
   then layer coalescing on top.

## 1. Publication-point completion and the candidate147 stall (question 1)

### 1.1 Is release-only deferral sound?

[S] It is sound for publication ordering, across all the cases you listed:

- **Nested and chained pushbuffers.** `CALL`/`JUMP` and NOP records run inside
  one `nv2a_pb_scan`. The batch depth is per executor thread (TLS) and is
  begun once.
- **A wrapped ring.** It scans as two consecutive batches, each publishing only
  after its own completion.
- **Trap prefixes.** Completion, then flush, then the ISR, unchanged.
- **Standalone releases.** They still complete when `FenceBatchActive()==0`.
- **Idle methods.** `WAIT_FOR_IDLE` still completes. It is unobservable, but
  keep it until coalescing passes on its own.
- **Target changes.** A target change reads back the old target before it is
  reused, so every target touched earlier in the segment is coherent when the
  segment publishes.

**What is not covered: CPU consumption of GPU-produced bytes between a release
and the next completion.** The new guards cover these executor-side consumers:

- pushbuffer words and NOP records;
- program vertex ranges in both the hardware and CPU paths;
- textures and palettes;
- host vertex/index arrays, which can't overlap anyway.

Still unguarded or unordered:

1. **Guest-thread reads of the current target before the segment publishes.**
   These are unordered with respect to the GPU writes, so stale bytes are a
   legal hardware outcome. They are not a correctness issue.
2. **Guest writes to the current target after the first post-sync draw.**
   These are lost at the next whole-surface readback (the lost-write hole in
   `REVIEW.md` §3). Coalescing moves that readback later, which widens the
   window.
3. **Segment end with color written but no release.** `HasRelease()` is false,
   so there is no completion. `DMA_GET` still advances, and the acknowledgement
   thread keeps PGRAPH idle bits permanently set (`NV2A_IDLE`), so the guest sees
   "idle" while the color is stale. This is pre-existing and the same in both
   modes. A cheap fix is to complete at every segment end whenever color is
   dirty: ~3.5 per frame [M segments/frame].

### 1.2 Questionable lines

| Location | Issue |
| --- | --- |
| `nv2a_pb_exec.c` `0x0100` handler: `if(!xbox_Nv2aFenceBatchFlush())return;` | Silently drops the interrupt after any earlier failure in the segment. |
| `nv2a_pb_exec.c` `acquire_vertex_inputs` | Calls `xbox_Nv2aFenceBatchFail()` on an address-range overflow, while `fetch_attr` would only have read memory. A guard turns a read anomaly into lost publication. Clamp or log instead, or make it fatal-loud. |
| `nv2a_pb_scan.c` `pb_walk` acquisition → `pb_reject(unknown)` | An acquisition failure becomes "unknown"; the segment end then commits nothing, with no reason logged. |
| `xbox_memory_layout.c` `xbox_Nv2aFenceBatchEnd(int commit)` | Discards pending stores with no log. Count every discard and its reason. |
| `xbox_memory_layout.c` `irq_write_veh` | Global unprotect plus single-step window (finding 4). |
| `kernel_bridge.c:2312–2323, 2665–2674` | The executor waits `INFINITE` on a state machine driven by a 10 ms poll. If bit 20 is never cleared, it never completes. |

### 1.3 The trace and reproduction that distinguish the three causes

**Minimal reproduction matrix.** Run each configuration, same build, same route,
up to the title screen (about 103 s). Proposed environment flags, separated:

| Arm | Release barrier | Input guards | Report completion | Purpose |
| --- | --- | --- | --- | --- |
| A | on | off | off | Control (known good) |
| B | on | **on** | **on** | Guards and report completion alone |
| C | **off, drain only** | off | off | Release waits for GPU completion with an event query but writes no color back: barrier timing, coalesced data |
| D | off | off | off | Pure release coalescing |
| E | off | on | on | The candidate147 configuration |

- If B fails, the guards are the cause, with batch failure as the mechanism.
- If C passes and D fails, the stall depends on data: something consumes
  mid-segment color (cause c).
- If C and D both fail, the stall depends on timing: suspect trap/ISR delivery
  (cause b).

Run each arm at least three times, because the title screen stall rate under
coalescing is unknown.

**Always-on ring trace** (fixed size, roughly the last 4,096 events, dumped by
the watchdog, a stall snapshot, or exit). One record per event:

- segment begin/end: start/end VA, PUT read, words, unknown, rejection reason;
- release: offset, value;
- `FenceBatchFail`: call site and reason;
- `FenceBatchEnd`: commit flag, published target and value;
- trap: parameter; flush result; each transition of the `pending` state
  (0→1→2→0) with QPC; `0x400100` and `0x100` before and after; ISR claimed;
- sync: reason and outcome;
- GET write.

On the guest side, record `KeWaitForSingleObject` entry and exit for the main
thread (object VA, timeout, status) and `KeSetEvent` with its caller (DPC or
thread).

**Diagnostic snapshot at stall.** Capture:

- all thread stacks, *including the timer thread and the executor*;
- the semaphore slot value against the last released value;
- GET/PUT;
- the pending notify state;
- the IRQ registers;
- emulated IRQL holders.

**Classification:**

- **Lost publication**: a release was recorded with a value greater than the
  slot value, and the batch failed or didn't commit.
- **ISR/DPC deadlock**: the executor stack is in `xbox_Nv2aSoftwareNotify`;
  pending is 1 or 2; bit 20 is set, or the ISR is unclaimed, or IRQL is raised;
  the timer thread is blocked or spinning.
- **Missed CPU dependency**: every fence is published (slot equals the last
  release), notify is clear, GET equals PUT, and the executor is idle in its
  `Sleep(0)` poll. Then trace the event the main thread waits on back to its
  signaller, and to the data that signaller reads.

**Fixtures to add.**

- Inject a batch failure before a trap, and assert that a loud diagnostic or a
  fatal stop occurs. A silent return must fail the test.
- Write W1C from two threads while a third holds the interrupt page in its
  single-step window, and assert that no bit is set by a clear.
- In barrier mode: draw → CPU consumer of the same bytes in the same segment →
  release. The guards must acquire.
- A segment with draws and no release: color must be coherent when GET
  advances (after the fix).

## 2. Multi-target content retention (question 2)

### 2.1 Accesses the current guards don't see [S]

- **Every guest-thread read** of target bytes, through any alias. It is ordered
  only through publication points.
- **Guest writes to the current target after the first draw that follows a
  sync.** The next whole-surface readback in `synchronize` overwrites them.
- **Accesses through the low physical alias** (`0x00xxxxxx`). Low RAM is
  separate storage from the contiguous window in this runtime, so these accesses
  never touch target bytes at all. This divergence predates the candidate.
- **The window thread reading `s_fb_va` directly** before the first flip
  (`fb_present.c` window loop). Unordered.
- **Host I/O into guest buffers.** `xbox_NtReadFile` and `xbox_NtWriteFile`
  (`kernel_file.c:249/289`) behave like guest writes and reads.

### 2.2 Is dirty A→B→A retention with eager flush at publication safe?

**Reads: yes.** A guest read of a retained, still-dirty B before publication is
unordered with respect to B's GPU writes, so seeing older bytes is a legal
timing.

**Writes: no, not as specified.** Retention widens the lost-write window from
"first draw → target switch" to "first draw → publication". A guest write to
pixels the GPU never touches would survive on hardware but is overwritten by the
whole-surface flush.

**Two exact fixes:**

1. **Three-way merge at flush** (no page protection). Keep the base snapshot
   from the last readback or upload, which `color_cpu_copy` already is when it
   is valid. At flush, for each pixel:
   - guest ≠ base → keep the guest byte, because the CPU wrote it;
   - otherwise → take the GPU byte.

   If the GPU didn't write a pixel, its readback equals base, so the result is
   exact. If both wrote it, either value is a legal ordering. The cost is one
   extra 1.2 MB compare per flush [E ~0.1–0.2 ms], and it also closes today's
   single-target hole. A target uploaded without a snapshot needs the uploaded
   bytes kept as its base.
2. **Write protection** (part of residency): exact, but it brings all the
   machinery in §3–4.

**Further rules retention requires:**

- **Key** by canonical contiguous offset plus full layout (pitch, w, h, bpp,
  format).
- **At most one dirty entry may cover any byte.** Binding X first flushes and
  evicts every retained entry that overlaps X's range with a different key.
- **Acquisition covers every dirty retained entry, not only `g.target`.** This
  applies to the texture, vertex, pushbuffer and NOP-record guards. Use an
  interval list.
- **Publication points flush every dirty entry.** That means:
  - every segment end that publishes, which per §1.1 item 3 should be every
    segment end with dirty color;
  - before a trap's ISR;
  - flip;
  - CPU clear/raster.
- **Address reuse is safe under D3D lifetime rules.** Freeing a resource that is
  in use requires a fence wait, which is a publication point, so the entry is
  already clean. Arbitrary reuse without a wait is covered by the merge for
  pixels the GPU didn't touch, and is a hardware race for pixels it did.
  Allocation reuse never implies content validity.

**Verdict:** retention plus three-way merge is sound under the same publication
model, without access tracking. Without the merge, it is not.

## 3. Observe-mode correctness (question 3)

**Owner access through a private view.** Map a third view of
`g_contig_mapping` at a host address P that is never protected. The executor
translates every contiguous or tiled address to `P + (va − 0x80000000)` for its
own reads and writes:

- readback write-back;
- snapshot and texture compares;
- uploads, including the `UpdateSubresource` source;
- vertex and pushbuffer reads.

Translate once at the `mem + offset` sites, or give the executor its own base
pointer for contiguous addresses. The public contiguous and tiled views can then
stay protected while the owner works. No owner access opens a window.

**Owner-induced blind spots** can only occur if some owner path still uses the
public view. Then:

- treat the resulting fault as "owner exposed";
- leave the page unprotected for the rest of the epoch;
- classify that epoch as **unknown**, not as "no access".

Report coverage as the fraction of GPU-output epochs that stayed fully
protected. Negative evidence is trustworthy only for those epochs.

**Rules for the observer itself:**

- Never use the unprotect-and-single-step pattern from `irq_write_veh`. It
  stops observing every other thread for the duration.
- On a fault: record the thread, RIP and read/write flag; set the page RW for
  the rest of the epoch; continue execution.

**Aliases and APIs to audit before trusting a negative result:**

- **Contiguous view and tiled view.** Both must be protected.
- **Low-RAM physical alias.** It is not an alias here, so an access through it
  silently reads other memory. Count those accesses separately using address
  arithmetic in the bridges; the pages can't be protected because the XBE image
  lives there.
- **Kernel-mode Win32 I/O.** It fails rather than faults on protected pages.
  Every bridge that hands guest pointers to Win32/NT APIs needs an
  acquire-and-record step: `NtReadFile`/`NtWriteFile`, the async I/O path
  (`kernel_bridge.c` ~3506), `DeviceIoControl`, and the save paths.
- **User-mode host readers.** These fault correctly but must be attributed: APU
  voice DMA, the XAudio2 submission copy, the FMV host path, and HLE `memcpy`.
- **Existing VEH handlers.** `pfb`, `irq`, `ac97` and `watch` each claim
  specific pages. Ours must return `CONTINUE_SEARCH` for non-AV codes, including
  `EXCEPTION_SINGLE_STEP`, and for addresses outside its ranges. Disallow
  `RECOMP_WATCH` on target pages.
- **External readers.** `inspect_guest.py` uses `ReadProcessMemory`, which fails
  on protected pages. Note this in its output.

## 4. Deadlock proof for residency servicing (question 4)

**Executor waits reachable from the trap path** [S]:

1. `xbox_Nv2aSoftwareNotify`: `WaitForSingleObject(done, INFINITE)`. This
   depends on the timer thread running guest ISR/DPC code, which may touch any
   memory.
2. `map_readback` and `wait_completion` spins: bounded by the GPU.
3. Optional DXGI `Present`: bounded by the GPU/DWM. In windowed flip mode it
   doesn't wait on the window thread.
4. **CRT locks.** `fprintf` and stderr `_lock_file` (the counter report) are
   shared with audio and kernel logging. `malloc` in `texture()` and `target()`
   takes the process heap lock.
5. `Sleep(0)` poll: harmless.

**Request-queue design** (faulting threads ask the executor to service). It
needs:

- servicing inside wait 1, by replacing it with `WaitForMultipleObjects` on
  {done, request};
- inside each spin in wait 2.

It still deadlocks if a thread holding a CRT lock faults while the executor is
blocked on that same lock (wait 4). That is unlikely, but it is real for any
logging that formats guest memory.

**Recommended instead: the faulting thread services its own fault under a
renderer mutex.**

- Put all renderer state behind one mutex, and enable
  `ID3D11Multithread::SetMultithreadProtected(TRUE)` so the immediate context
  can be used from any thread under that mutex.
- The executor holds the mutex only *inside* raster API calls (draw, sync,
  present, acquire). Inside those it must:
  - never wait on guest progress (`SoftwareNotify` is called outside the mutex);
  - never take CRT stderr or heap locks: preallocate buffers and log through a
    lock-free ring;
  - touch guest memory only through the private view, so it can't fault while
    holding the mutex.
- A faulting thread, whether a guest thread, the timer ISR/DPC, audio or the
  window thread:
  - takes the mutex;
  - rechecks the target state;
  - reads back through the private view;
  - sets the public pages to READONLY;
  - releases the mutex and retries.
- **Why it can't deadlock:** the mutex is a leaf lock. Its holder acquires no
  lock that a waiter for the mutex could hold, apart from driver-internal locks,
  and a guest-memory fault cannot occur while holding those.
- **Emulated IRQL and guest spinlocks** don't matter: the executor never waits
  on them while holding the mutex.
- **Multiple faulting threads** are serialized by the mutex. Everyone after the
  first sees a SHARED page and retries.
- **Eviction** happens under the mutex. A fault on an address no longer covered
  by any entry retries if `VirtualQuery` shows the page RW, and returns
  `CONTINUE_SEARCH` if the page isn't ours. This avoids a livelock.
- **Shutdown order:** set draining → under the mutex, read back everything and
  unprotect → disable the table → tear down D3D → remove the VEH last.
- **Any D3D call that sources guest bytes must use the private view.** Otherwise
  a fault occurs inside the driver while the mutex is held.

## 5. Asynchronous publication (question 5)

**Which model each observable needs** [S]:

| Observable | Produced | Required model |
| --- | --- | --- |
| Color target bytes | Asynchronously by the host GPU | Lazy, but must reflect at least the published prefix. A superset is legal. |
| Depth/zeta | Private host buffer (`s_zbufs`), never written to guest memory | None. This is a pre-existing fidelity gap, not an ordering constraint. |
| `GET_REPORT` | At parse time, on the CPU path. `zpass_enable` forces CPU raster after a full sync. | Already ordered. |
| Semaphore / ISR | At publication, from parse state | Requires its color prefix, which lazy acquisition provides. |
| `DMA_GET`, input reuse | Inputs are copied into D3D-owned memory at parse | None. |
| Failed suffix | A parser failure suppresses publication. Device loss is now fatal. | Unchanged. |

**Proof sketch.** With depth private and inputs copied, the host GPU's only
pending effect that the guest can observe is color. A lazy readback returns
every parsed command's output: a superset of any published prefix. The question
is whether a guest observer could distinguish that superset from real hardware.
That requires a read that is ordered after fence F but before a later,
*already-submitted* GPU write to the same pixels. D3D-mediated `Lock` waits for
the resource's last-use fence, which includes the later write, so hardware
returns the superset too. Raw unlocked reads are hardware races.

The earlier "ordered frontier / staged snapshot" model is needed only if one of
these holds:

- the GPU reads guest memory asynchronously (not here);
- the runtime adds a GPU-produced observable whose value must match an exact
  prefix (GPU occlusion queries, GPU-written reports, guest-visible depth or
  stencil).

**The premise holds for the current feature set.** Enforce it with a registry:
every new GPU→guest output must go through the coherence layer, or async
publication is disabled.

**Counterexample to watch for:** a title reading render-target pixels without
`Lock` right after a fence and depending on hardware latency (expecting older
content). Observe mode (§3) detects such reads as non-owner reads soon after a
fence.

## 6. Path to 60 FPS (question 6)

**Use exclusive accounting instead of more phase timers.** The executor stores a
single "current bucket" value at each boundary:

- parse;
- gather/pack;
- validation;
- upload;
- D3D state/draw;
- sync wait;
- sync copy;
- trap wait;
- acquisition sync;
- idle poll;
- other.

A sampler thread reads that value at about 1 kHz. The buckets sum to wall time
by construction and cost one store per transition. Add ETW stacks with symbols
for attribution inside each bucket.

**Measurements that decide the next investment, in order:**

1. **Exclusive buckets plus counters** for epoch-on, in the fixed view *and* a
   60–120 s combat route. Check draws, rows, readbacks and segments per frame
   in combat against the paused view.
2. **The coalescing bisection** (§1.3). If coalescing passes, re-measure:
   - expected: 59 → ~0–3 release readbacks/frame;
   - snapshot compares down roughly in proportion;
   - an upper bound of ~10–15 ms/frame [E, from the older 0.25–0.3 ms per
     readback; part of the GPU drain reappears at the remaining sync points].
3. **Per-draw D3D cost.** `draw()` rebinds every piece of state on every draw;
   bind/draw measured ~0.9 µs/draw, plus 12–13 % of samples in the UMD.
   Filtering redundant state is low risk. Size it with the D3D bucket before
   building it.
4. **Vertex compaction.** Measure the gather/pack and upload buckets against
   about 32 MB/frame of rows. Expected 1–3 ms [E].
5. **Native clears and retention.** These matter only if the target-transition
   and clear sync buckets are material in combat.
6. **Observe mode, then residency and async**, if 2–5 leave the frame above
   about 20 ms.

**Realistic headroom [E].** From 41.4 ms:

- working coalescing → roughly 27–31 ms;
- redundant-state filtering plus vertex compaction → roughly 22–26 ms;
- 60 FPS (16.7 ms) in the fixed view then needs overlap (residency/async)
  and/or clear and target retention.

Combat is unknown. If combat has 1.5–2× the draws and rows, per-draw and vertex
costs dominate, and 60 FPS in combat needs the async/residency work plus
submission work. Also add the vblank cap (finding 5).

**Falsifiers:**

- Coalescing removes ≥ 50 readbacks/frame but executor CPU falls by < 8 ms →
  the per-readback model is wrong.
- Combat shows the D3D/pack buckets above ~50 % of the frame → residency isn't
  the next bet.
- A diagnostic GPU-time sample above ~12 ms/frame → the GPU becomes a real
  constraint.
- Observe mode shows frequent genuine CPU reads → residency yields little.

## 7. Frame pacing and uncapping (question 7)

**A unique displayed frame** is a guest `FLIP_INCREMENT_WRITE` with a sequence
id whose image is then shown.

- **GDI path:** count window-thread blits whose `s_present_idx` differs from the
  previous blit. The `Sleep(16)` loop both repeats and drops frames.
- **Native path:** tag each `Present` with the guest frame id, and confirm
  display with `IDXGISwapChain::GetFrameStatistics` (`PresentCount`,
  `PresentRefreshCount`, `SyncQPCTime`).
- Report as separate series:
  - guest flips per second;
  - unique presents;
  - displayed refreshes with a new id;
  - repeats;
  - drops.

**Audit before 60 Hz pacing or uncapping:**

1. **The vblank source** (finding 5).
   - Measure the real interval: log the QPC of each `kernel_vblank_tick`
     firing.
   - Move it to a high-resolution waitable timer with an absolute 16.683 ms
     schedule.
   - Determine whether the title waits on vblank to present: presentation
     interval, swap throttling, or the `sub_0014C180` event.
2. **The KeTickCount resolution mismatch.** `KeTickCount` is written from
   `GetTickCount()` in the acknowledgement loop (15.6 ms steps), but
   `KeTimeIncrement` claims 1 ms. Timer DPCs run on a 10 ms poll quantized to
   the system tick.
3. **Time sources the title reads.**
   - The TSC is scaled to 733 MHz (good).
   - `KeQueryPerformanceCounter` returns host QPC/QPF pairs (consistent).
   - Check whether simulation advances per frame, per vblank or per elapsed
     time. Instrument the guest's calls and correlate them with simulation
     state.
4. **Audio clock coupling.** Check DirectSound position polling and whether
   movies sync to the audio clock (Bink).
5. **Input sampling cadence** per guest frame.

Uncapped rendering is safe only if simulation, animation and cinematics are
time-based, or if rendering is decoupled from simulation (it isn't, for a
recompiled title). Otherwise "uncapped" means "fast-forward".

## Revised dependency order

1. Split the flags (barrier, guards, report completion). Make batch failures and
   suppressed interrupts loud. Add the ring trace and the full environment
   manifest.
2. Run the §1.3 bisection on the title screen route, with fixtures for the IRQ
   W1C race and the silent-trap drop.
3. In parallel: measure exclusive buckets and counters in the fixed view and in
   combat. Measure vblank intervals.
4. Complete at every segment end when color is dirty. Qualify the guards in
   barrier mode.
5. Coalescing, isolated, through the full gates.
6. Fix vblank resolution and pacing. Add unique-frame accounting.
7. Redundant-state filtering and vertex compaction, by bucket size.
8. Retention with three-way merge, or native clears, if transitions or clears
   are material.
9. Observe mode → residency/async with the mutex design (§4), only if the frame
   is still above about 20 ms.
