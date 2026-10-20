# Target residency: conservative design draft

October 20, 2026. Design only; no implementation or policy change.

The objective is to reduce repeated target readback/upload work while preserving
every CPU-visible byte and completion dependency. The first stage retains eager
coherence at all existing synchronization boundaries. Removing a readback or
deferring guest publication is a later decision requiring consumer coverage.
See [the performance plan](PERFORMANCE_NEXT.md) and
[the measured costs and limits](QUICK_WINS_163.md).

## What the current implementation guarantees

In `src/kernel/nv2a_hw_raster.c`, `target()` synchronizes before changing a
color/depth binding. `synchronize()` copies dirty output into staging resources,
waits for availability, updates guest memory and the exact color snapshot,
then advances completion state. Depth-only work also waits for completion.

`TargetStorage` pooling reuses allocation storage keyed by dimensions and color
bytes per pixel. It explicitly retains no guest backing, revision or snapshot.
It is not a content cache. `nv2a_hw_acquire_input()` checks only the current dirty
color/depth targets; color checks canonicalize the configured tiled alias.
Retaining several dirty targets would invalidate that coverage assumption.

`NV097_BACK_END_WRITE_SEMAPHORE_RELEASE` in `nv2a_pb_exec.c` completes immediately
in barrier mode. The existing coalesced policy queues publication within a batch
and completes before publishing at its existing boundaries. Residency must not
silently weaken either contract. GET publication remains command consumption;
it says nothing about target or GPU completion.

## Stage 1: retain coherent target content

Introduce a bounded content entry separate from allocation pooling. Identify it
by canonical allocation identity and lifetime generation, byte range, pitch,
dimensions, format, tiling interpretation and depth representation/scale.
Pointer equality and dimensions alone are insufficient. Include overlapping
views and alias ranges in dependency lookup, with checked range arithmetic.

Each entry owns its native resources, exact CPU snapshot where required, CPU
and GPU content generations, completion serial and last-use information. Preserve
snapshots across binding switches only when the backing lifetime and full layout
still match. At every existing synchronization boundary, finish and write back
all relevant dirty entries before allowing publication. A completed entry can
remain cached after writeback and avoid an unnecessary later upload if its
guest bytes still match the exact validated snapshot.

This stage may reduce allocation/upload work. It does not claim fewer release
barriers, fewer required readbacks or a particular speedup. Keep exact CPU edit
validation and eager fallback. The current depth revision contract needs its
own audit; color snapshot rules cannot simply be applied to managed depth.

Before implementing it, specify a total byte budget including default, staging,
upload and snapshot allocations. Evict only after required writeback/completion.
Unsupported layouts, ambiguous overlaps, shared edge pages, missing snapshots,
allocation failures and stale generations use the existing eager path. A failed
GPU completion must stop publication, not mark the guest backing coherent.

## Stage 2: establish CPU-consumer coverage

Begin with observation while keeping eager readback and publication. Determine
which output generations are actually consumed or modified by the guest CPU,
other guest threads, CPU raster/clear paths, texture/palette upload, vertex/index
fetch, display/movie paths and host APIs operating on guest buffers.

Audit all aliases and lifetimes: allocation/free/remap, physical/tiled mappings,
page sharing, existing VEH/MMIO handlers and Win32/NT I/O guest pointers. Current
renderer-mediated acquisition checks alone cannot establish absence of arbitrary
CPU access. Owner-thread readback and copies need a private view or another
scheme without an unprotected public-page interval. Document blind spots.
Observation coverage must be demonstrated with intentional readers/writers;
zero observed accesses without that check is not evidence of safety.

Use standard CPU/GPU profiling for cost measurement. If access observation needs
new instrumentation, propose its specific coverage and overhead separately.
Do not add an accumulating collection of performance counters to this design.

## Stage 3: selective GPU-dirty residency

Only after coverage supports it, consider an explicit CPU_DIRTY / SHARED /
GPU_DIRTY state machine across all retained entries. CPU reads acquire completed
GPU content; CPU writes preserve required unaffected bytes and invalidate GPU
content generations. GPU reads acquire CPU changes and respect overlapping
backings. GPU writes advance their generation without allowing stale CPU access.

The D3D11 context owner performs acquisition/writeback. Non-owner threads enqueue
requests and wait without holding guest/kernel locks needed by the owner. The
owner must service requests when idle and while waiting for guest ISR/DPC work.
Fault handlers must not invoke D3D11 on arbitrary threads. Define lock ordering,
request cancellation, shutdown and device-loss behavior before implementing it.

Native render-to-texture reuse requires exact layout compatibility and explicit
unbind/sampling hazards. Conservatively flush overlapping or incompatible views.
Guest semaphore values must retain their existing completion and CPU-visibility
meaning. Any later asynchronous publication proposal needs a separate ordering
argument and review; it is not implied by content residency.

## Evidence required before enabling a stage

Fixtures must cover pitched BGRA and RGB565, depth scaling/revisions, allocation
reuse, partial and overlapping views, CPU writes between draws, read-through
aliases, concurrent acquisition, shared pages, I/O buffers, eviction, shutdown,
device loss and failed acquisition. Compare exact guest bytes and completed
rendered pixels against the eager path; test publication ordering separately.

Runtime gates include menus, movies, combat, death/replay and returned HUD, with
guest-state transition proof and the existing GET checks. Shared rendering bugs
must remain distinguishable from new corruption. Profile each stage separately
on the same route with physical GPU load recorded and offline exports deferred
until all captures finish. Keep the eager implementation available for rollback.

Review the backing/alias, lifetime and deadlock arguments before enabling
retention. Texture epochs qualification and the normal gameplay gate for the
environment-variable caching changes remain independent prerequisites for their
own promotion. Bulk readback remains off and is not a prerequisite.
