# Next performance work: stable 60 FPS

Planning baseline: October 14, 2026. This is a proposed sequence, not enabled
behavior or a promised speedup. Candidate149 and its existing evidence remain
frozen; the corrected launcher has not changed the executable.

### October 18 upstream integration

The follow-up sequence is now specified in `CANDIDATE153_QUALIFICATION.md`:
repeat short-route pairs on the integrated executable, add a monitored
barrier-mode GET-off control, then qualify barrier-mode GET-on gameplay,
death and replay before promoting GET progress. Compare rendered gameplay
in matched scenes with GET progress on in both barrier/coalesced arms;
movie artifacts do not settle that question. Audio measurements pair the
WAV tap with raw QPC/device counters and host starvation accounting, after
a known-duration producer-stall fixture checks the counter's meaning.

Selected v0.14.0 floating-point/flag, port-I/O, audio and positive-zero vertex-W
changes are ported on `codex/integrate-v014`; see `UPSTREAM_V014_INTEGRATION.md`.
Candidate152 reached HUD gameplay in a 600-second normal-audio/barrier run,
with auto DSP doorbell discovery. Movie artifacts and audio underruns remain;
this does not establish a performance gain or full replay qualification.
The named launcher is unchanged. GET progress/coalescing/IRQ gating stayed off
in this run. The walker and publication policy are preserved, but candidate151's
runtime GET results must not be treated as qualification of the new guest timing.

### October 14 GET implementation update

The current experiment follows `review/opus55/IRQ_GET_RESPONSE.md` and the
implementation record `review/opus55/GET_PROGRESS.md`. Candidate151 adds
independent, default-off `RECOMP_NV2A_GET_PROGRESS` and
`RECOMP_NV2A_GET_MONITOR` flags. Keep the IRQ gate off in every GET arm.

The deterministic fixture reproduces the missed action-5 wakeup with progress
off and exercises every read-point interleaving, torn patches and immediate
command-storage reuse. GET progress requires SDK-verified ring bounds, uses
full-barrier stores at a 0x400-byte stride, holds at CALL return addresses,
and freezes on parser or handler failures. Backward ring jumps and in-ring
CALLs are unsupported for this experiment and invalidate qualification.
Completion and semaphore publication rules, and the final GET=PUT store,
retain their existing behavior.

Qualification proceeds through the monitored short route, normal-barrier full
gameplay/replay gates, coalesced full gates, then performance. A short-route
watchdog exit is not a correctness pass. The monitor must see stale GET in
controls and no bound violations in progress arms; wait hooks, active PFB
trapping, failures and captures must also be checked. Neither GET progress nor
coalescing has been promoted. The earlier IRQ proposal below is historical.

The five candidate151 short-route pairs are complete: all five progress arms
kept maximum GET lag at 0x3FC across 148,161,279 ring reads, with zero bound
violations, unsupported control flow or waits after reserved-slot fetches.
Four controls reproduced the lost wakeup; the fifth progressed despite stale
GET. This qualifies the monitored short-route invariant only. Pixel artifacts
remain visible in coalesced captures, and full gameplay/replay gates are still
required. See `review/opus55/GET_PROGRESS.md` for results and limitations.

### October 14 review update: diagnose coalescing first

Claude's `review/opus55/ANSWERS_RESPONSE.md` moves the coalescing stall ahead of
the general gameplay profile below. The epoch-on counters attribute 19.36 of
41.52 ms/frame to release synchronization, so this is the largest measured
opportunity. The proposed 10–15 ms saving remains an estimate.

The immediate sequence is:

1. Audit the original wait and action-5 handler, then collect an ordered trace
   of the event, IRQL, trap, interrupt, observed PUT, release and publication.
2. Compare release-only coalescing with the current interrupt behavior and an
   optional NV2A IRQL gate, three short runs of each on the same binary. Add
   CLI/STI modeling only if the guest path demonstrates a need for it.
3. At a reproduced stall, compare the release/NOP addresses against submitted
   PUT and inspect the unsubmitted ring tail. A watchdog exit alone proves
   neither a fixed lost wakeup nor correct rendering.
4. If the trace establishes a gating fix, qualify gating with normal barriers
   through the full gameplay/replay gates, then qualify coalescing on top.
5. Resume vblank interval measurement, gameplay CPU profiling, and target
   retention/residency work after that correctness decision.

Candidate150 is an opt-in diagnostic build for steps 1–3; the default launcher
keeps interrupt gating and coalescing off. The direct wait routine clears the
event before emitting NOP 5, and the audited wait/action-5 routines contain no
CLI/STI or direct IRQL calls. The proposed masked-interrupt race therefore
needs runtime evidence rather than being treated as established.

The six candidate150 runs are now reviewed in
`review/opus55/IRQ_FOLLOW_UP.md`: three controls progressed, two gated runs
progressed, and one gated run stalled at about 102 seconds. The failed trace
has no final action-5 command or event set; its waited release was published.
The executor passed the reserved trap location before the guest armed it while
DMA GET still reported the old segment boundary. The next candidate should
test conservative within-segment GET progress, with command-consumption and
completion/publication kept distinct. Add a deterministic late-patch regression
and cover split payloads, branches/returns, wraps, SDK records and failures
before runtime qualification. Interrupt gating remains a separate experiment.

## Target and current evidence

First target: sustained 60 FPS in representative gameplay with normal audio,
controller input, correct pixels and smooth presentation. The frame budget is
16.67 ms. Aim for enough rendering headroom that a limiter can hold that budget;
a limiter by itself cannot fix slow rendering. Track completed guest frames and
displayed new frames separately, alongside missed deadlines and long stalls.

Historical fixed-view measurements are roughly 40–50 ms/frame. Reaching 16.67 ms
there needs about 2.4–3 times the throughput. Reconstructed combat windows have
less draw/readback/upload work than the paused view; combat frame time still
needs direct measurement.
The candidate149 three-pair result of 52.600→41.421 ms lacks hardware telemetry;
the later concurrent hardware-recorded comparison was inconclusive. Neither
proves current controller gameplay performance after the launcher correction.

Useful established evidence:

- Texture epochs reduced requested texture comparison lengths from about 204
  to 25 MB/frame. This is not an exact memory-traffic or elapsed-cost measurement.
- Older instrumented traces show about 65–66 color readbacks/frame and about
 19–20 ms/frame of synchronization. Query-free counters attribute roughly 59
  color readbacks/frame to releases, over about 3.5 submitted segments/frame.
  The timings overlap other phases; they must not be added as independent costs.
- About 1,300 native draws/frame and roughly 32 MB/frame of vertex upload remain
  in the historical paused view. Packing was about 4 ms/frame in older traces.
- Release coalescing passed native fixtures but its intentional-on runtime
  experiment stalled at the title screen. Its exact live GET/PUT and executor
  fence state were not captured, so its cause remains unresolved.
- Default `src/video/fb_present.c` redraws through GDI and `Sleep(16)`. The title
  bar counts guest flips, not uniquely displayed images. Optional native
  presentation exists, but it has not earned default promotion.

Hundreds of FPS is a later throughput goal: 200 FPS allows 5 ms/frame ; 300 allows
3.33 ms. That likely needs larger changes to coherence, vertex processing and
submission, plus an audit of game timing. Removing a cap cannot supply this
throughput and must not accelerate simulation, audio, cinematics or input.

## 1. Establish a useful gameplay profile

Use a fresh diagnostic launch with only explicitly selected flags; record the
actual child configuration and binary hash. Preserve the corrected launcher
environment regression. Start measurements after reaching gameplay rather than
paying the whole movie route for every profiling iteration.

Record bounded, buffered QPC timestamps at successful completed flips and at
new-frame presentation requests, plus actual compositor/display events where
available. A returned blit or Present call does not itself prove scanout; label
queue/request timestamps accordingly. Flush the buffer outside timed windows. Keep GPU timing
queries off by default; verify observer overhead separately. Report median,
p95/p99 intervals, percent of 16.67 ms deadlines missed, longest stalls and
repeated/dropped displayed frames, distinguishing rendering from pacing.

Capture both a fixed view and a repeatable 60–120 second combat route: movement,
camera turns, firing, damage, effects and HUD. Use query-free CPU phase timers,
sync causes, real readbacks/fences, target transitions, validation lengths,
uploads, CPU thread times and resolved stacks. Record external GPU load/clocks.
Profile input-to-render delay and guest/executor idle time separately.

Penumbra may remain open. Use CPU/work attribution and native microbenchmarks
for iteration, and classify changing-load wall-time measurements as inconclusive.
Comparable windows can be selected using hardware conditions before examining
their FPS results; do not cherry-pick fast windows or divide FPS by utilization.
An idle-load qualification may use a naturally quiet interval or another machine;
closing the other project is not a prerequisite for development or correctness.

Deliverable: current gameplay bottleneck table, pacing trace and stable capture
points. Do not use old paused-view timings as current combat measurements.
Track and fix the intermittent DirectSound movie stall separately, with normal
audio retained. Known route failures are excluded from performance statistics,
not accepted as part of a successful smooth-gameplay qualification.

## 2. Resolve the release-coalescing failure

Reproduce release-only coalescing, with texture epochs/hash independently off,
and retain a barrier-override control. Capture before termination: all thread
stacks, executor TLS pending fences and batch state, fence destination/value,
GET/PUT, trapped method, interrupt/notify state, IRQL holders and target state.
Distinguish the graphics wait from the known baseline DirectSound movie stall.

Trace release, completion, batch publication, GET advancement and ISR/DPC
acknowledgment as a compact ordered timeline. Check releases in chained/nested
pushbuffers, trap prefixes, failures, standalone calls and CPU inputs produced
by previous GPU work. Do not defer WAIT_FOR_IDLE or invent fence completion to
make the route advance. Ask Claude to review the timeline and source proof.

If corrected, qualify publication-point completion on its own before combining
with texture changes. Expected payoff is fewer real drains/readbacks, not fewer
guest-visible fences or draws. Measure the actual reduction rather than assuming
all 59 release readbacks/frame are removable.

Deliverable: reproduced cause, minimal fix, expanded fixtures, matching normal
gameplay routes and independent comparison. If unresolved, keep it off and
continue observe-mode work rather than blocking every other optimization.

## 3. Retain native target content and measure CPU refinements

Design a bounded cache of target *content*, keyed by canonical backing and full
layout. Existing dimension pooling reuses storage only. Track content generation
and dirty state separately, preserve CPU dependencies and flush every relevant
dirty target before publication. Native render-to-texture sampling needs exact
layout matching and explicit sampling/rendering hazards. Unsupported cases keep
the eager path. Do not remove color snapshot validation solely because no
renderer-mediated CPU read was noticed.

Validate the concurrency/coherence argument with Claude before enabling content
retention. If it depends on proving arbitrary CPU access/write visibility, build
the access observer first instead of treating batch boundaries as sufficient.

Independently evaluate implemented texture hash lookup using CPU phase timing
and realistic native cache benchmarks, then hash-only/epoch-only/combined runs.
Only keep it if it improves the remaining lookup cost without freshness changes.
Evaluate vertex layout/row compaction against the actual shader input masks;
measure bytes and packing time while preserving all referenced attributes,
layout semantics and CPU fallback. GPU-native clears must preserve rectangles,
channel masks, formats and depth/stencil behavior; prioritize them only if the
new gameplay profile shows material clear/upload cost. Existing clear sync was
small in the earlier diagnostic, so it is not the main bet for 60 FPS.

## 4. Build color-access observation before residency

Start with an opt-in observer retaining eager readback and existing publication.
Measure, by target/phase, which GPU output epochs are followed by a real
non-owner CPU read or write before the next GPU write. Include menus, movies,
beach, combat, death/replay and returned HUD.

Cover every actual alias of eligible backing; audit allocation/free/remap,
shared edge pages, Win32/NT I/O guest pointers and existing VEH/MMIO handlers.
Use a runtime-private view for owner access and readback writes where necessary.
An owner acquisition that exposes an unprotected public page creates an
observation blind spot; absence of faults during that window is not proof that
the guest did not access it. Document observation coverage and missed accesses.

Decision: few genuine CPU consumers relative to current readbacks justify
selective residency. Claude suggested roughly1–2 necessary readbacks/frame as
a favorable result; use actual per-phase results and conservative shared-page
fallbacks. A high access rate may instead favor better eager batching/copies.

Then implement a bounded CPU_DIRTY/SHARED/GPU_DIRTY state machine, generations,
alias protection, owner-thread acquisition and queued non-owner requests.
Service requests while idle and while blocked on guest ISR/DPC progress. The
fault path must not call D3D from an arbitrary thread or hold a lock that prevents
its owner from servicing it. Keep explicit host API acquisition and unsupported
format fallback; device loss must stop rather than silently publish stale data.

Fixtures: concurrent reads/writes through all aliases, shared pages, trap-time
faults, I/O buffers, allocation reuse/free, pitched formats, eviction/shutdown,
acquisition failures and exact rendered pixels. Review the deadlock/lifetime
design with Claude before a gameplay candidate.

## 5. Overlap work only after coherence is proven

Audit every guest-observable GPU result, not just color: depth access paths,
reports/queries, interrupts, fences, DMA_GET/input reuse and failure prefixes.
Determine whether parse-time publication plus lazy real-completion acquisition
is sufficient, or whether some observable requires an ordered completion
frontier. The earlier investigation and Claude's review disagree here; resolve
that proof explicitly before implementation.

Use bounded backpressure tied to real GPU readiness. Do not grow an unbounded
queue, drop work or return success before the required observation is coherent.
Audit every executor wait for access-request service and shutdown cancellation.
Separate correctness of earlier published prefixes from failures in later work.

This is the largest likely route to 60 FPS: stop draining the GPU many times per
frame and let the guest, executor and GPU overlap. It is a structural project,
not a safe one-line removal of waits. Reprofile after each isolated step.

## 6. Deliver smooth 60 and then consider uncapped mode

Once render throughput has headroom, qualify native presentation and replace
fixed polling redraw with frame-driven, explicit pacing. Keep CPU-produced
movie/menu presentation, correct completed-frame selection, resize, focus,
minimize/restore and closure. Measure unique presented frames and latency.

Add a saved 60 FPS setting using a precise pacing mechanism and an explicit
presentation/vsync policy. A repeating 16 ms sleep plus processing time is not
a precise 60 Hz schedule. Keep input sampling responsive and audio/game timing
independent. Verify representative combat, not just a stationary mean of60.

Before offering uncapped rendering, audit guest vblank, time sources, simulation
steps, animation/cinematics and input behavior. Distinguish uncapped renderer
throughput from displayed refresh and simulation rate. Hundreds of FPS remains
an empirical stretch goal; do not promise it from current evidence.

## Validation and immediate next move

Use native fixtures and short attribution runs during implementation. Run the
existing repeated timing and full normal-audio/controller/replay gates before
promotion, with exact hashes, matching work/captures and classified stalls.
Preserve the verified executable and candidate132; every new candidate gets a
new basename/PDB. Keep per-feature fallback and never promote a combined change
based on one noisy improvement.

Immediate next move: implement the gameplay/presentation timing trace, profile
the corrected epoch-only configuration, and reproduce the release-only failure
with complete diagnostics. Send the findings and the accompanying review brief
to Claude before target residency or asynchronous publication code.
