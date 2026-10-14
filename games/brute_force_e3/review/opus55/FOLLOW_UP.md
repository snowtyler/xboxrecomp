# Follow-up review request: path to stable60 FPS

Prepared October 14, 2026. Draft for the user to give Claude; it has not been sent.

Please review the current implementation against `REVIEW.md`,
`games/brute_force_e3/PERFORMANCE.md` and `PERFORMANCE_NEXT.md`. The first goal is
correct, smoothly paced60 FPS in active gameplay; hundreds of FPS is a later
stretch goal. Please distinguish measured evidence, source proof and estimates.

## New evidence

- Query-free attribution found about65.5 color readbacks/frame, about59 caused
  by releases,3.5 submitted segments/frame,204 MB/frame requested texture
  comparisons and74 MB/frame snapshot comparisons in the historical fixed view.
- Texture epoch first-use validation is implemented independently from hashed
  lookup, with ISR/readback/host-write invalidation, decoder identity including
  source pitch, and native freshness/eviction fixtures. Requested texture
  comparisons drop to about25 MB/frame. Exact color snapshots remain.
- Candidate149 frame/CPU/work comparison improved52.600→41.421ms across three
  pairs, but hardware recorder children failed. A later complete concurrent
  hardware comparison was inconclusive as other GPU workload varied. Do not
  treat either as clean current gameplay speedup proof.
- Two normal-audio2000s gameplay/replay routes per arm passed reviewed captures
  and normal exits. The title still has intermittent baseline DirectSound movie
  stalls; live snapshots show GET=PUT, no executor pending fences, notify clear,
  APU IRQ pending and main stack through0x386483. Classify them separately.
- Release-only coalescing in candidate147 failed at the title screen: main
  graphics-event wait with idle executor. That run lacks a captured GET/PUT/TLS
  fence timeline, so the precise cause is not established. WAIT_FOR_IDLE remains
  synchronous. The flag remains off despite passing native prefix/trap/input
  fixtures.
- Manual launch later exposed a separate PowerShell environment bug: passing
  `$null` through SetEnvironmentVariable produced empty values, enabling native
  presence-checked flags, including coalescing/PFB polling/GPU profiling/mute.
  The launcher now truly removes those flags; regression and closure checks
  pass. The controlled performance scripts used the environment provider and
  are separate from that faulty manual-launch configuration.
- Default window rendering uses GDI plus `Sleep(16)`; title-bar FPS counts guest
  flips. Optional native presentation already exists. Smooth presentation and
  renderer throughput need separate measurements.

## Questions requiring a source-level answer

1. **Publication-point completion:** is current release-only deferral sound
   across nested/chained pushbuffers, trap prefixes, idle methods, target changes
   and CPU consumption of GPU-produced command/vertex/index/texture data? What
   exact ordered trace and fixture would distinguish lost publication, ISR/DPC
   scheduling deadlock and a missed CPU dependency in the candidate147 stall?
   Please identify concrete questionable lines and a minimal reproduction.
2. **Multi-target content:** which CPU accesses/writes remain invisible to the
   existing dependency guards? Is retaining dirty A→B→A target content safe with
   eager flush at publication, or does it already require access tracking?
   Include arbitrary guest CPU writes and backing/layout/address reuse; storage
   allocation reuse is not coherent content retention.
3. **Observe-mode correctness:** how can both public aliases stay observable
   while the owner reads/writes through a private view? Does any owner or I/O
   acquisition expose a writable public-page interval that would invalidate a
   conclusion of “no non-owner access”? Which aliases and bridge APIs must be
   audited before the observer's negative evidence can be trusted?
4. **Deadlock proof:** enumerate executor waits reachable from trap/ISR delivery
   and how resident-memory requests are serviced during each. Address raised
   IRQL, guest locks, multiple faulting threads, resource eviction and shutdown.
   Please challenge the request servicing and target lifetime design before
   implementation, including coexistence with existing MMIO VEH handlers.
5. **Asynchronous publication:** your review argues that lazy readback returning
   a superset of a fence's prefix is legal, while the earlier residency note
   assumed ordered frontiers/staged snapshots. Which guest observables actually
   require each model? Audit reports/queries, depth paths, interrupts, DMA_GET,
   input reuse and failed suffixes. Is the premise that color is the only
   observable pending GPU result valid in this runtime? Provide a concrete
   counterexample or proof rather than accepting that premise by default.
6. **60 FPS path:** after current epochs, which remaining measurements justify
   the next investment: native target retention/residency, vertex compaction,
   native clears or trap/pushbuffer scheduling? Avoid adding overlapping phase
   timers or promising a combined speedup from older traces. What headroom is
   realistic for active combat, and what should falsify the proposed approach?
7. **Frame pacing/uncapping:** review the guest flip counter, default redraw
   loop and optional native presenter. What should count as a completed unique
   displayed frame? Which guest time/vblank/simulation assumptions must be
   audited before60 Hz pacing or uncapped rendering can preserve game speed,
   cinematics, audio and controller behavior?

Deliverable requested: ranked findings with source references, required
correctness proofs/fixtures, instrumentation gaps and a revised dependency order.
No fence fabrication, removed draws, muted audio or disabled gameplay work.
