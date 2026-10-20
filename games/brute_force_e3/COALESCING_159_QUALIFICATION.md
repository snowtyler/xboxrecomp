# Coalescing qualification and sync measurements

Status: short GET qualification and repeat lean measurement complete; rendered captures reviewed with strict route failures. Coalescing remains off by default. Results reviewed October 19, 2026.

## Five coalesced short pairs

Candidate157 is the qualified integrated binary. Both arms used the direct post-arm observer, ordered trace, active PFB trapping and IRQ gating off; controls set GET progress to literal `0`. Every run reached its 180-second watchdog normally.

| Pair | Control far reads | Control max lag | Direct / trace violation | Control stall | Progress reads | Progress max lag |
| --- | ---: | --- | --- | --- | ---: | --- |
| 1 | 18,940,148 | `0xCF8C` | 0 / 0 | no | 31,741,396 | `0x3FC` |
| 2 | 4,892,816 | `0xBA08` | 1 / 1 | yes | 31,510,206 | `0x3FC` |
| 3 | 18,101,080 | `0xCD60` | 0 / 0 | no | 31,408,339 | `0x3FC` |
| 4 | 1,795,928 | `0xBA08` | 1 / 1 | yes | 31,687,442 | `0x3FC` |
| 5 | 18,990,139 | `0xCF74` | 0 / 0 | no | 31,941,850 | `0x3FC` |

All five progress arms passed across 158,289,233 ring reads and 18,194 post-arm event decisions, with zero far/invalid/untrusted reads, read-past decisions, freezes, backward jumps or in-ring CALLs. Controls 2 and 4 each had one direct violation, one matching old-placeholder trace violation, and a graphics-event stall. The other controls progressed despite stale GET. Stall counts are not a performance metric or a sensitivity requirement for every control.

The five final progress captures show advancing intro playback with known Bink artifacts. They establish short-route liveness, not rendered coalescing correctness. Raw summaries and identities are in `analysis/perf157-coalesced-pairs-summary.json`.

## Same-window sync accounting

Reanalysis of candidate158's representative 500–530 s gameplay interval gives 46.35 release calls/flip, 42.69 readback/fence-bearing release calls/flip, 43.63 color and 3.07 depth readbacks/flip, and 2.73 submitted segments/flip (2.70 with releases). Average release cost is 0.828 ms/call, or 0.899 ms per readback/fence-bearing call when legacy no-op time is included in the numerator.

A constant-mean-call-cost model retaining one call per nonempty segment gives an optimistic 36.14 ms/flip saving before forced drains and replacement syncs. This is a model, not a strict bound or predicted FPS gain: target sizes, no-op distribution, visibility reports, CPU acquisitions and other ordering boundaries change the retained work. Candidate158 did not record readback bytes; do not infer them from a single assumed target size.

Candidate159 adds logical successful readback payload bytes (width × height × four bytes for each color/depth staging plane), per-reason bytes/counts, readback/fence-bearing wall time, and an experimental sync CPU counter subsequently withdrawn below. These exclude mapped row padding and unknown physical bus traffic. Every native report balances total readback bytes against reason attribution. New APU producer-thread CPU/source counters share QPC time with the device and executor counters. Whole-thread CPU clocks must be interpreted over long intervals.

## Rendered comparison: limited visual result, strict route failed

Both 640-second image arms completed normally. Barrier GET monitoring covered
1,224,557,887 reads and 10,666 post-arm event decisions; coalesced monitoring
covered 1,621,704,506 reads and 12,445 decisions. Both had maximum lag `0x3FC`
and zero far/invalid/untrusted reads, freezes, unsupported branches or read-past
decisions.

All six gameplay inputs landed at their requested flips in both arms, and all
seven completed-flip captures (28,000 through 31,000 at 500-flip intervals)
were written. The barrier menu inputs at 4,500 and 6,600 arrived one flip late;
the coalesced menu input at 6,900 arrived one flip late. The strict validator
correctly failed both routes. Its criteria have not been relaxed.

Manual review of all seven pairs shows comparable terrain, camera, character
position and HUD through the movement and firing sequence. Bright yellow
foliage and other conspicuous rendering features are shared by both modes;
no coalescing-only block corruption was observed in these samples. At flip
31,000 ammunition differs (29 barrier versus 32 coalesced), so these are not
identical simulation states. This limited observation separates the shared
rendering issue from the earlier movie evidence; it does not qualify all
coalesced gameplay or replace the failed strict route and remaining full gates.
Raw receipts retain their failures in `analysis/perf159_scene_*-flip-route.json`.

The first comparable native counter windows are flips 28,512–28,973 in barrier
mode and 28,502–28,968 in coalesced mode. Only complete report intervals inside
28,500–29,000 are used; their exact endpoints differ.

| Per completed flip, except call costs | Barrier | Coalesced |
| --- | ---: | ---: |
| Release calls | 66.79 | 0 |
| Readback/fence-bearing release calls | 62.99 | 0 |
| Submitted segments | 3.51 | 2.68 |
| Color readbacks | 65.74 | 8.34 |
| Depth readbacks | 3.03 | 3.03 |
| Logical color payload, decimal MB | 78.71 | 8.17 |
| Logical depth payload, decimal MB | 3.54 | 3.54 |
| Texture comparison, decimal MB | 187.37 | 187.37 |
| Snapshot comparison, decimal MB | 74.63 | 4.10 |
| Executor CPU, ms | 49.18 | 45.47 |
| All synchronization wall time, ms | 24.68 | 21.84 |
| Release wall time, ms | 21.99 | 0 |
| Release wall cost per call, ms | 0.329 | — |
| Release wall cost per work call, ms | 0.349 | — |

Coalescing removes about 90% of logical color payload here, but the remaining
synchronization is expensive. Coalesced idle drains alone cost 12.38 ms/flip,
trap drains 3.03 and segment drains 2.96; barrier idle drains cost 1.11 ms/flip.
Removing release calls shifts costs to later ordering boundaries. The barrier
constant-call-cost model suggests 20.85 ms/flip before replacement costs, yet
the observed total-sync difference is only 2.84 ms/flip in this window. Neither
number is a qualified speedup under concurrent GPU load. Keeping targets
resident and observing actual CPU reads remain priorities regardless of the
coalescing outcome. Later flip-window results remain in each `-combat.json`.

## Runtime configuration and limits

Candidate159 EXE SHA-256: `127f17d09b90526f00091fdcd02ca5a669ea3988ada2b0dae5f2c0dfad8053fb`. GET, GPU completion and semaphore publication policies are unchanged. The named prototype remains candidate157. Native semaphore tests (four), raster/coalesced-input tests (two), and APU model tests (three) passed.

The scene workflow uses the same guest-flip controls and exact completed-flip captures in barrier and coalesced modes. Gameplay inputs start after the intro, with a four-flip camera turn. Flip-indexed input is applied when the guest polls the controller; it cannot force the guest to poll at every flip. Keep actual input receipts and scene review alongside the strict result.

Hardware observers found Penumbra using substantial time on the same GPU. It remains open. Timings therefore describe concurrent load, not an idle baseline. Executor CPU can include driver busy-waiting for GPU readback; high executor CPU alone does not prove all release cost is useful CPU computation.

The lean barrier run disables every surface-dump flag, the sampler, and WAV output, while retaining normal audio, GET monitoring, query-free renderer counters and APU/device timing. The analyzer rejects images/samples/WAV output in this mode. Scene-aligned diagnostic cost comparisons use whole native counter intervals inside guest-flip windows; the lean baseline uses QPC windows. Neither substitutes for repeated performance qualification.

The original `perf159_scene_lean_barrier` exited with code 0 at 263.95 s,
before the requested combat windows and final snapshot. Its log ends during
ordinary playback without an identified shutdown cause. It is excluded from
qualification. The complete workflow therefore retains `completed_with_failures`;
a fresh `perf159_scene_lean_retry` preserves the failed evidence separately.

## Lean measurements and audio production

The repeat completed its normal watchdog at 640.35 s. GET monitoring passed
across 633,648,768 reads and 7,689 post-arm event decisions: maximum lag
`0x3FC`, zero far reads, read-past decisions or unsupported execution. The final
snapshot verified active PFB trapping. Launch metadata and output inventory
confirm no surface-dump flags, images, sampler output or WAV, and the optional
per-sync CPU timer was absent. All requested profile windows contain complete
native reports; logical readback bytes balance their reason totals.

In the retry's 500–530 s window, 26.827 s of complete renderer reports contain
246 flips and 24.391 s executor CPU (90.9% of one core). Release sync costs
73.12 ms/flip across 47.97 calls/flip (44.30 work calls), averaging 1.524 ms
per call or 1.651 ms per work call. Color/depth logical payload is 54.58/3.65
MB per flip. All sync costs 87.16 ms/flip; texture comparison remains 122.90
MB/flip. Guest wait reports cover a slightly different 26.834 s, including
25.318 s event waiting (94.3%) and no completed poll-path waits.

This is a dump-free, sampler-free sync bottleneck under concurrent load,
not an estimate of dump overhead. Its 9.17 flips/s and larger per-call cost
cannot be compared causally with earlier runs because GPU contention and scene
state differ. The lean route has no independent frame review; interpretation
of gameplay state rests on the previously reviewed route and native inputs.

Audio production changes far less than flip rate:

| Retry QPC window, s | Flips/s | Source frames/s | APU CPU, % of one core |
| --- | ---: | ---: | ---: |
| 0–60, pre-game | 74.68 | 44,365 | 2.57 |
| 240–300, intro | 81.34 | 44,926 | 1.44 |
| 360–420, gameplay route | 17.17 | 42,123 | 2.34 |
| 420–480, gameplay route | 5.02 | 42,087 | 1.47 |
| 480–540, gameplay route | 13.54 | 42,159 | 1.89 |
| 540–600, gameplay route | 10.24 | 41,984 | 1.63 |

The 500–530 s APU window separately records 42,108 source frames/s and 1.65%
producer CPU. Device reports play about 42,099 frames/s over 28.336 s, leaving
about 3.483 s of unplayed wall time using the separately measured fed-device
rate. A 1,000 ppm uncertainty accounts for only 28 ms in that interval. The
fed probe is from candidate158, not a fresh same-run clock calibration; its
endpoint quantization and drift limits remain in `COMBAT_PROFILE_158.md`.

This does not support a source rate proportional to renderer FPS, nor a
saturated APU producer core. Production is already below 48 kHz during high-FPS
intro playback and remains near 42.1 kHz across widely different gameplay
flip rates, including the earlier coalesced arm. Investigate pacing, scheduling,
lock/trap delays and submission cadence independently; low producer CPU alone
does not identify which wait is responsible. No audio pacing change or audible
quality pass is included.

## Phase CPU measurement correction

Candidate159's experimental per-call `GetThreadTimes` sums failed a physical
sanity check: in flips 28,512–28,973, the release total claimed 36.20 ms CPU
per flip against only 21.99 ms wall. These short-phase CPU values are discarded,
including analogous legacy SDK wait CPU sums. Counts, QPC wall time, byte
attribution and long-window thread CPU/APU measurements are unaffected.

Candidate160 replaces the short-span clock with `QueryThreadCycleTime` and
reports cycles and cycle shares only. Whole-thread CPU milliseconds still come
from long-window `GetThreadTimes` deltas. Cycles are not converted to elapsed
time, following [Microsoft's documentation](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime).
The comparison stays on the same candidate159 binary in both arms; its phase
CPU sums are excluded. The lean arm disables that experimental per-sync timer.

Candidate160's separate 20.15-second startup smoke reached the watchdog normally
and produced valid version-2 wait/cycle and renderer records with zero clock
errors. The 2–15 s analysis retained 8.01 s of wait-report intervals and 8.04 s
of renderer intervals. Poll cycles were 2.92% of observed guest-thread cycles;
no event stage occurred in this startup window. The actual-hook fixture covers
both stages. This is instrumentation validation only: the full GET monitor
analyzer rejects this short smoke because it observed no post-arm event checks.
It is not a gameplay or GET qualification pass. Candidate160 EXE SHA-256:
`b93a4274def33e3c7b88092570729f59bcef8ae2380db90e3ed85db9cebae4b5`.

## Validation and next work

Final checks: 38 Python tests, two actual-hook MSVC tests, lean launch-boundary
and launcher-environment regressions, PowerShell syntax and Python compilation
checks passed. The prior native semaphore/raster/input/APU checks passed as
recorded above. General performance summaries also discard legacy phase CPU
milliseconds; raw native logs are retained. Evidence identities, actual failures
and reviewed frame hashes are recorded in
`analysis/perf159-qualification-verification.json`.

Next is a focused audit/observer design for expensive idle and other remaining
drains, target residency and genuine CPU consumers, with existing eager
coherence retained initially. Keep shared rendering and audio pacing as
separate tracks. Resolve exact input/state comparability and finish the full
coalesced correctness gates before any default change. Candidate157 remains
the named prototype; candidates159/160 are diagnostics, and nothing is pushed
by this qualification step.
