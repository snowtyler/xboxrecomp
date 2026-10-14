# Answers to Claude's additional questions

Audit date: October 14, 2026. These answers distinguish saved runtime evidence,
source findings and missing measurements. No new gameplay experiment or ETW
recording was run for this audit. `claude-questions-evidence.json` in the evidence
archive contains the full derived counters, original windows and hash checks;
`audit-claude-evidence.py` reproduces them from the included raw files.

## 1. Candidate147 release-on stall

**Dropped-batch diagnostics:** a complete scan of
`perf147_release_on_1.stderr.log` found zero occurrences of each of:

- `[GPU INPUT] invalid vertex range`
- `[PB ERROR]`
- `[PB RESUME]`
- `[HW FENCE]`

All 49 valid emitted `[HW COUNTERS]` packets have zero failures for every
`sync` reason. The final packet is report49, stderr line4015, executor TID24700,
flips7466. No packets were emitted after progress stopped. Thus there is no
positive logged evidence for the proposed dropped-batch path; this does **not**
prove that no failure occurred after the last emitted packet. The logger reports
at successful flips, and cannot report a tail that never flips again.

**Executor:** `perf147_release_on_1-stall-cpu.json` identifies TID24700, matching
the counter packets. Of622 samples:445 are `ZwDelayExecution`,88
`nv2a_ack_thread`,34 `RtlDelayExecution`,14 `SleepEx`, and5 `Sleep`, with the rest
mostly polling helpers. This supports the ack thread's `Sleep(0)` polling path,
not `xbox_Nv2aSoftwareNotify`'s `WaitForSingleObject`. The sample's stack candidate
list contains `nv2a_ack_thread`, not SoftwareNotify. These are sampled instruction
pointers plus scanned stack contents, not a complete unwound stack proof.

**Timer:** the same all-thread sample includes TID28648:621/622 instruction
samples in `NtWaitForSingleObject`, one in `NtWriteFile`, and stack candidate
`kernel_timer_thread`. The source normally waits up to10 ms on the notify wake
event. The saved sample does not identify its exact host wait handle or a
complete call chain, so it cannot distinguish a normal timed wait from every
other possible timer-thread wait.

**Interrupt/IRQL snapshot:** `perf147_release_on_1-stall-inspection.json` records
`g_irql_raised_count=0` and APU IRQ0. It resolves the address of
`g_nv2a_notify_pending` but does not read its value. PGRAPH/PMC status, GET/PUT,
executor batch TLS and pending-fence values were not captured by that version
of the inspector. They cannot be recovered from the terminated process.
Do not substitute the later manual-launch black-screen snapshot: that had a
different environment and a different failure signature.

**Semaphore:** the snapshot records device `*(0x003AB378)=0x003A9330`,
`device+0x30=0x80001000`, slot value `0xFDC7`, and `device+0x2C=0xFDC9` (two
serials ahead). It does not record the last released parameter, pending stores,
or the exact target serial being awaited. Values appearing elsewhere on the
scanned stack are not sufficient to identify those missing values.

**Which main-thread event:** the saved guest ESP contents start with arguments
`0x003AAC9C,6,1,0,0`, matching the original wait in `sub_0039EC10` at guest
`0x0039ED20`. That function waits on `device+0x196C`, hence `0x003AAC9C` for this
snapshot. The lifted code calls the thunk at `0x003B3970`, which the title's
thunk table identifies as ordinal159, `KeWaitForSingleObject`.

Source establishes the signal path:

- `sub_0039EC10` emits `NO_OPERATION` parameter5 and waits on that event.
- PGRAPH software-method service `sub_003A5120` calls `sub_003A53F0`.
- That function dispatches `(parameter &31)`: action5 reaches `0x003A5589`.
- It calls ordinal145 (`KeSetEvent`, thunk `0x003B39AC`) on miniport context
  `+0x1A4`. The device embeds that context at `+0x17C8`, so the sum is `+0x196C`.
- The vblank branch in `sub_003A4D20`, at `0x003A4F7E`, signals context `+0x194`
  instead, i.e. device `+0x195C`. That is a different event.

**Correction to earlier summaries:** the CPU sampler's
`stack_symbol_candidates` included `sub_0014C180`; we previously described it as
a caller. That list scans stack contents for code-like addresses and is not an
unwound call chain. The generated `sub_0014C180` is a linked-list removal routine,
not the graphics wait. The snapshot arguments and the concrete
`sub_0039EC10` wait site above are the stronger evidence.

**Vblank enabled:** raw stderr contains three `vblank -> ISR claimed it` lines.
This confirms the vblank path was enabled in that run. The launch script set
`RECOMP_VBLANK='1'`, but the historical child-environment whitelist omitted that
key. Consequently we cannot claim that the literal child value1 was directly
recorded. No live environment dump survives; the runtime log plus source/script
support enabled behavior. A fresh reproduction must record the actual key.

**Decision:** the original evidence does not justify abandoning coalescing as a
silent dropped batch, nor does it establish correctness. Reproduce with the full
event/fence/GET/PUT/notify timeline and actual child configuration. In particular,
trace NOP action5, its interrupt acknowledgment, event set/reset/wait and suffix
progress. Keep it default-off until that is resolved.

## 2. Pacing and vblank

No10-second QPC trace of `kernel_vblank_tick` firings exists in the saved runs.
No per-second vblank-claimed counter exists. The three initial claimed messages
are deliberately bounded logging, not the interrupt rate. Therefore an actual
interval distribution or claimed-vblank/flip ratio cannot be supplied honestly.

Source: `kernel_timer_thread` waits up to10 ms on the notify wake event, then
runs vblank, notify servicing and DPCs serially. `kernel_vblank_tick` uses
`GetTickCount64`, tests a deadline and sets `next_ms=now+16`; it is not a QPC-based
periodic16.667 ms schedule. ISR/DPC execution and timer scheduling can delay it.
The intended16 ms is not evidence of the actual rate. This is a high-priority
measurement before attributing a prospective below60 cap to rendering.

The observed candidate147 wait is on software-trap completion, not the separate
vblank event. We have not completed a source/runtime proof of every title present
interval or swap-throttle path. In the runtime, `FLIP_INCREMENT_WRITE` synchronizes
and presents the completed frame and advances `flip_read`; the subsequent
`FLIP_STALL` does not model a real scanout wait. Separately, default GDI window
redraw uses `Sleep(16)`. Guest flips and unique displayed frames must be measured
separately; native `Present(0,0)` submission also does not itself prove scanout.

There is no quantified epoch-on/off active-play test of simulation or animation
speed. The replay captures establish gameplay progress, firing, death/restart
and resumed controls, but do not establish equal physics distance, animation
phase, input latency or simulation ticks per real second. Those measurements
remain required before uncapping.

## 3. Remaining epoch-on time and combat work

The following pools the **counter intervals** in all nine timed windows of the
original three candidate149 epoch-on runs (`perf149_epoch_on_1..3`). It uses6110
counter flips and preserves actual outcome masks. Its QPC time is41.516 ms/frame
and executor CPU39.109 ms/frame. This differs slightly from the comparison's
41.421 ms run-mean result because the interval populations/weighting differ.
Neither result has hardware telemetry from those original recorder children.

| Sync reason | Calls/frame | ms/frame | Color readbacks/frame | Depth readbacks/frame | Event fences/frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| release | 66.864 | 19.36194 | 60.191 | 0 | 2.595 |
| target | 6.000 | 0.00014 | 0 | 0 | 0 |
| depth | 10.000 | 0.83002 | 0.992 | 1.999 | 0 |
| clear | 3.000 | 0.65256 | 0 | 1.987 | 0 |
| flip | 2.000 | 0.00010 | 0 | 0 | 0 |
| trap | 3.000 | 0.00007 | 0 | 0 | 0 |
| segment | 3.550 | 0.20893 | 0.635 | 0 | 0.011 |
| idle | 32.000 | 0.78603 | 3.811 | 0 | 1.000 |
| cpu | 1.021 | 0.27075 | 0.995 | 0.021 | 0 |
| shutdown/report/input | 0 | 0 | 0 | 0 | 0 |

Every failure count is zero in these intervals. Outcome0 means no-op success;
bits1/2/4 mean color/depth/event and combinations retain all bits. Full totals,
all eight outcome buckets per reason and all individual windows are in
`claude-questions-evidence.json`, not discarded by this table. Target/depth phase
timings must not be inferred from these sync-only wrappers.

Other pooled values per frame:

- Draws1311.665; submitted segments3.583; color maps66.623; depth maps4.006;
  event fences3.606; releases66.864; traps3.000; idle methods32.000; clears3.000.
- Total blocking software-trap round-trip time0.23284 ms/frame, **separate** from
  the near-zero `sync.trap.ms` measured before calling the ISR delivery path.
- Requested texture comparisons25.104 MB; snapshot comparisons75.723 MB;
  vertex upload32.599 MB (decimal MB, requested comparisons not exact traffic).
- Target transitions:160×120×4,320×240×4,512×512×4 and640×480×4. Counts in the
  selected intervals are6110,6110,6110,18330 respectively. These dimensions are
  not unique backing identities or unique zeta surfaces. No target-table overflow.

**CPU phase timing:** `RECOMP_HW_CPU_TIMING` is null in each original epoch-on
child manifest; all three stderr logs contain zero `[HW PHASE]` packets. There
are no epoch-on bind/draw or target µs/draw numbers to report. Do not substitute
the older candidate138 phase costs. Enabling the independent CPU timer in a
new diagnostic run is the next step; GPU timestamp queries are unnecessary.

**Rough combat counters do exist.** The four successful normal-audio replay
logs contain cumulative counters even though their performance monitor was off.
The audit reconstructs approximate phase windows using the process-manifest UTC
birth times and median UTC/QPC calibration from the same host boot's original
monitor records. Calibration5th–95th percentile spans about91 ms. These are
retrospective attribution windows, not controlled timing or speedup comparisons.
Only complete adjacent, monotonic report intervals inside each window count.

| Epoch-on replay/phase | Draws/frame | Color readbacks/frame | Segments/frame | Vertex MB/frame | Traps/frame |
| --- | ---: | ---: | ---: | ---: | ---: |
| on1 paused560–590s | 1311.0 | 66.669 | 3.237 | 32.574 | 3.005 |
| on1 combat715–775s | 653.8 | 40.875 | 2.882 | 19.972 | 3.009 |
| on1 resumed1500–1560s | 813.2 | 49.719 | 3.213 | 22.964 | 3.008 |
| on2 paused560–590s | 1311.0 | 66.155 | 3.570 | 32.589 | 3.000 |
| on2 combat715–775s | 638.2 | 40.176 | 2.933 | 19.553 | 3.010 |
| on2 resumed1500–1560s | 817.2 | 50.074 | 3.395 | 22.970 | 3.009 |

Both off-arm repeats and full sync breakdowns are also in the JSON. Captures at
750s and1530s establish firing/movement/damage in the respective routes. The
camera/work changes, so these rows are not matching-work off/on performance
pairs. Exact packed vertex-row counts were not logged with CPU phase timing off;
the byte counter mixes vertex layouts and must not blindly be divided by one
stride. SDK vertex-program counts are a different counter, not packed rows.

**ETW feasibility:** WPR, WPA and xperf are installed locally. `wpr -profiles`
succeeded and lists CPU, GPU and DesktopComposition profiles, among others.
Matching project PDBs exist and their hashes are provided below. A host-access
CPU sampling/wait/presentation trace is feasible; no new trace was started here.
Windows/CRT symbol availability and vendor-private driver symbols are separate:
we can seek proper module/stack attribution, but cannot promise private NVIDIA
function names. The existing `memchr`/`strchr` labels remain nearest-export
sample buckets, not proven function-level costs.

## 4. Design assumptions

**CPU writes into color backing:** yes, older decoder probes recorded CPU movie
conversion into the tiled scanout backing. For example `decode43.stderr.log`
lines1875–1876 report converter003911E0 destinationsF02B0000 andF02B1400; later
`flip47.stderr.log` includes corresponding contiguous-view pixel reads. The
converter tracing code in `src/recomp_manual.c` directly reads those guest
destinations after calling the CPU converter. This establishes CPU writes and
tiled access to display backing. It does **not** establish whether an enclosing
SDK Lock/Unlock happened; those probes did not correlate the lock history.
Thus we cannot claim that the “all writes go through Lock” assumption is true,
or that three-way merge can be omitted on that basis.

**More than four zeta surfaces:** unknown from current telemetry. `s_zbufs[4]`
in `nv2a_pb_exec.c` is keyed by zeta offset and recycles via `s_zbuf_next++ &3`,
resetting the reused float storage. The target dimension histogram does not
identify zeta offsets. A trace of zeta offset/layout transitions and slot
evictions is needed; source comments that nothing reads guest depth are not
runtime proof. No claim of at-most-four title surfaces is supported here.

**Aliases:** tiled0xF0 accesses are demonstrated above; canonical scanout
offset0x002B0000 corresponds to contiguous0x802B0000. Runtime initialization
verifies tiled/contiguous aliasing. Low RAM and the contiguous window use
different section mappings (`g_mapping_handle` versus `g_contig_mapping`), while
the tiled view maps `g_contig_mapping`. Do not treat every low physical numeric
offset as a CPU alias of contiguous backing: DMA offsets are not CPU virtual
addresses. No candidate140–149 trace proves or disproves title CPU access to the
same color backing through a low physical view. Audit the actual mapping and
HLE pointer translation rather than assuming equivalence.

**Trap frequency:** the paused epoch-on windows contain exactly3 nonzero NOP
traps/frame, with about0.233 ms total round-trip time; the rough combat windows
are also about3/frame. This is the total blocking trap count, not just markers.
The scanner recognizes registered SDK record markers using subchannel0,
method0x100, a configured shift and `(parameter &31)==s_nop_action`, then follows
the record's continuation. No dedicated SDK-marker counter or complete parameter
histogram was saved. The first12 software-method log lines are insufficient to
split total traps into markers versus event5 and other actions. That split is
missing and should be added explicitly, rather than guessed as three markers.

## 5. Build identity and evidence archive

The builds were made from **uncommitted working states** over base commit
`95c94885318bf6e77f2493584484c51df20cb385`. There is no honest single historical
source commit for either exact build. Their archived patches and per-file source
hashes are included. The five core candidate149 source files still match the
recorded raw hashes and were subsequently committed in
`84c30977389b7f617c3e29a9546e144fc85cf1fb`; that commit also includes later harness,
launcher and documentation changes. It is not the historical source commit for
candidate147. The five-file manifests are not a complete hash manifest of all
generated title code/toolchain dependencies.

| Artifact | SHA256 |
| --- | --- |
| candidate147 EXE | `1381e1b3bed42c9ca246fa846ffa2af9fd4d580be2cd63852bd17fa42a3e73eb` |
| candidate147 PDB | `802b98b59a4b7dd813122dc023473b03a75802f63805b77098af069c15e77484` |
| candidate149 EXE | `d5c8e68dddd0a0b43b53b6cf28c72efe58defafce9f10b5ee2ea22f7957ab020` |
| candidate149 PDB | `c39f0d39d96eee9ae71b62d90809962eaa6ea8fca0d9ee81481bfcdf539ebfcb` |

The audit rehashed all four artifacts and matched the manifests. Both builds
used RelWithDebInfo, optimization enabled, ABI checking off.

`claude-performance-140-149-evidence-full.zip` contains raw saved analysis text/logs,
JSON/JSONL, manifests, patches and PNG evidence for140–149, plus the relevant
older movie-write logs, derived audit data, this response and selected source
excerpts. Its file manifest supplies sizes and SHA256 values. Repetitive BMP
captures, game assets and executable/PDB binaries are excluded; the original
binaries remain local and their identity is recorded above. Missing recorder
output is not reconstructed or represented as available telemetry.

## Next measurements, in order

1. Actual10-second QPC vblank firings/claimed counts against guest flips and
   unique presentation timing; record real child flags. Test whether a clock or
   wait policy imposes a hard cap before attributing it to GPU throughput.
2. Candidate147-style release-only reproduction with action5 event timeline,
   GET/PUT, pending fences, serials, status bits and timer/IRQL state before exit.
3. Corrected epoch-only active-play CPU phase profile plus ETW with matching PDBs.
4. Per-target CPU access/write observation, zeta identity/eviction and SDK-marker
   classification before residency or further asynchronous-publication claims.
