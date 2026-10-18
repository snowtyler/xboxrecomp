# Candidate151: conservative within-segment GET progress

Prepared October 14, 2026, against local `99a3960`, following Claude's
`IRQ_GET_RESPONSE.md` (GitHub blob `77d3d741aca2d12b40c5e869dea561ac46868ae0`).
This is an opt-in experiment. It does not resolve candidate147 or promote IRQ
gating, coalescing, or GET progress to the normal launcher.

## Contract and implementation

`RECOMP_NV2A_GET_PROGRESS` enables walker-owned GET publication using an
`InterlockedExchange` before subsequent command loads. The walker publishes
consumed command storage at a stride of 0x400 bytes, including inside long
payloads, and forces updates at safe control-flow boundaries. Handler calls
return before their parameters become reusable. The marker parameter and
linked record are handled separately; GET never enters the CPU record.
The candidate's PDB-guided native disassembly verifies a memory XCHG at
`nv2a_get_publish+0xD` against USER GET `+800044`. The PFB write helper also
contains memory XCHG and LOCK CMPXCHG. This evidence is saved as
`analysis/get-publish-native-disassembly.txt`.

Ring-bound provenance is explicit. Publication is permitted only with exact
SDK bounds, never with learned bounds, at the exclusive high bound, or at the
submission's final PUT. The two pieces of a wrap share a submission boundary;
the second walker can publish the exact SDK low bound. The acknowledgement
thread retains the single final GET=PUT store after both scans and batch ends.
It is unchanged even on failure, as required for an isolated experiment.

CALL publishes its ring return address and holds there throughout an external
subroutine. Jumps into external geometry hold at the consumed jump's end;
returning forward into the root publishes the target. Backward ring jumps and
in-ring CALLs freeze further progress and increment diagnostics. This is more
conservative than the review sketch: moving GET backwards cannot undo storage
reuse already permitted by an earlier GET. Qualification requires both counts
to remain zero. The parser's existing control flow is unchanged.

`xbox_Nv2aFenceBatchFailed` makes handler-side rejection visible to the walker.
Parser rejection, handler failure and a split-resume mismatch freeze further
publication. GPU completion and semaphore publication are unchanged. Command
storage consumption is independent of either.

## Evidence closure and observations

`audit_get_protocol.py` saves original-XBE disassembly and a reference inventory
in `analysis/get-protocol-*`. It scans decoded function bodies for `+17C0`,
PFIFO GET/subroutine, USER GET offsets and absolute register-address references,
including overlapping function definitions, and deduplicates
instruction addresses. This is a bounded source audit; it cannot prove the
absence of arbitrary indirect aliases.

- `0039F140` contains SFENCE, a PFB `+100410` read/modify/write and a polling
  loop. The generated C emits a no-op for SFENCE. Under default trapping,
  `pfb_write` executes InterlockedExchange before the guest continues. Runtime
  snapshots explicitly verify that `g_pfb_page` is nonzero. PFB polling stays
  off during qualification. The generated MEM32 accesses are volatile and the
  flush helper call remains between the patch stores and GET-distance recheck.
- `0039EAF0` selects the recent-serial record at `+4C + 12*((serial>>1)&63)`
  and has additional obsolete-record/address-order handling. The fixture
  models the relevant recent-serial case, including the three-dword table
  layout; it does not claim to port obsolete-record recovery or optional
  callbacks.
- `0039EA90` uses GET for wait distance, and `0039ED40` uses it for ring space.
  Both have the `324C` fallback, avoided by keeping GET in the ring.
- `00399BAC` encodes the action-1 flip marker (`value<<5 | 1`) and writes
  the NOP header/parameter before the subsequent flip methods. `0039C333`
  separately emits the action-3/4 marker and inline record. Only the registered
  action-3 form causes a record skip in this walker; action 1 remains an
  ordinary trapped method. Both emitter bodies are included in the audit.
- The USER GET equality loops in initialization (`003A376B/003A379B`) and
  destruction (`003A398A/003A39BF`) wait for the submitted endpoint. Preserving
  the single final PUT acknowledgement keeps their ordering.
- `0039EED0` and `0039EFE0` also contain PFIFO `3240 == 3244` checks followed
  by `400700` checks; USER GET progress does not alter those registers. Their
  alternate disabled-device path redirects the USER pointer to software
  storage. The PFIFO `324C` reads/writes in the interrupt/context code are
  independent of publishing an out-of-ring USER GET.
- `bridge_KeWaitForSingleObject` assigns g_eax from the blocking host call
  only after that call returns. Its preceding bridge code does not write
  g_eax or g_edx. The saved recheck registers therefore survive the observed
  infinite wait, subject to the asynchronous snapshot limitation.

`RECOMP_NV2A_GET_MONITOR` works with progress off or on. It counts ring reads,
lag at or above 0x1C00, invalid GET, untrusted bounds, publications, freezes and
unsupported branches. Summaries are cumulative and available in snapshots.
Order tracing records GET publication and reserved-word fetches in addition to
the existing releases, traps, completion/publication and event history.

`BRUTE_FORCE_E3_GET_TRACE_HOOKS=ON` installs idempotent observations at SDK wait
entry, after the post-arm distance recheck, at the poll path, and at both exit
paths. The checked installer is committed because generated C is ignored.
Hooks record the waited serial, selected record, GET, distance terms, chosen
path, total wait time and poll-loop duration. These are diagnostics and can
affect race timing; poll duration includes host descheduling and is not CPU time.
The detail record preserves raw EDX and EAX-minus-EDX. When the helper returns
early with a negative first distance, it never computes d2: EAX is zero and
EDX still holds normalized GET. Interpret that record as a clamped first
distance with an uncomputed second term, not as two arithmetic distances.

Snapshots additionally retain device position/callback fields, all 64 fence
records and their reserved words, PFIFO 3240..324C, GET statistics and PFB trap
status. They remain asynchronous observations, not atomic GPU dumps.
The observer also captures main/executor state after a target graphics-event
wait lasts five seconds, covering stalls later than the scheduled inspection.
This observer extension starts with control 3 in the short-route series;
controls 1–2 and progress runs 1–2 used the initial observer. The binary and
in-process instrumentation are identical throughout. Observer metadata records
its script hash for the extended version. Post-failure ring dumps may contain
storage already released by GET.

## Deterministic and existing checks

`tests/nv2a_pb_control/get_progress_test.c` runs the actual walker with a
single-threaded guest model of the relevant SDK wait and full GET-distance
arithmetic, including wrap, d2 and subroutine fallback. The gate_2 layout uses
its original addresses and F00F/F011/F013/F015 releases. With progress off,
arming after F011's reserved words were consumed produces WAIT with no event;
with progress on it chooses POLL and the release publishes normally.

The fixture starts the guest at every actual dword read point, with immediate
and interleaved patch stores, then repeats each run with immediate poisoning of
acknowledged command storage. Executed command and publication hashes must
agree. Structural cases cover maximum payloads, external CALL/RETURN, forward
and external jumps, exact and learned wraps, split payloads and SDK records.
Failure cases cover unknown and unreadable commands, bad continuation records,
handler rejection and the parser budget. Unsupported ring CALL/backward-jump
cases must freeze progress. In-segment publication never equals final PUT.
Immutable-stream results are compared across GET arms as well as reuse arms.

The four GET configurations (progress off/on crossed with barrier/coalesced)
join the original pushbuffer test. Existing GPU, semaphore and IRQ fixtures
are retained, along with accounting tests and the default-launch environment
regression. Qualification-analyzer tests reject blind controls, bound
violations, missing PFB trapping and incomplete traces; hook installation is
checked for idempotence and missing-label rejection.

## Runtime qualification

Build a distinct candidate, preserving historical binaries:

```powershell
cmake -S games/brute_force_e3 -B games/brute_force_e3/build -DBRUTE_FORCE_E3_OUTPUT_NAME=brute_force_e3_perf151 -DBRUTE_FORCE_E3_GET_TRACE_HOOKS=ON
cmake --build games/brute_force_e3/build --config RelWithDebInfo -- /m:1 /nr:false
games/brute_force_e3/verify_get_progress.ps1 -Name perf151_get_title_qf -Pairs 5
```

The workflow alternates one binary's GET-off and GET-on arms with coalescing
on, IRQ gating off, normal audio and GPU profiling off. The control must
expose monitor violations; the progress arm must have zero violations,
unsupported branches, failures and waits after stale placeholder fetches.
`analyze_get_progress.py` links fetched reserved words to their release instance
and post-arm recheck. It reports poll-loop durations separately. Captures need
visual review, and watchdog exit alone is never accepted as progress proof.

Only after these checks pass should normal-barrier full gameplay/replay gates
start, followed by coalesced full gates and then performance. Known DirectSound
movie stalls remain a separate classification. No speedup is claimed here.

### First control: direct reproduction with the IRQ gate off

`perf151_get_title_qf_control_1` reproduced the title-screen graphics-event
stall at about 104 seconds. Its 180-second watchdog exit was normal, while
the final event wait remained blocked for about 75 seconds. The observer lost
no records. The monitor recorded 5,543,817 far reads and maximum lag 63,500
bytes (0xF80C), with no invalid GET, unknown bounds or unsupported branches.

Waited serial EFF1 selected record `8005FD1C`; the post-arm hook read GET
`8005B220`, EAX `4AFC`, EDX zero, then chose the event path.
`8005FD1C - 8005B220 = 4AFC`. Fetch sequence 533468 read the second placeholder
as COLOR_CLEAR_VALUE 0 at `8005FD30`. The clear observation was sequence 533478,
about 20.3 microseconds later; the patch stores follow that hook. These are
trace-point timings, not atomic guest-instruction timestamps. The recheck was
sequence 533479. The compact
window is `perf151_get_title_qf_control_1-critical.csv`.

The 135-second snapshot directly contains the selected record and six words:
`00041D70 EFF1 00040110 0 00040100 5`. It also preserves EAX `4AFC`, EDX zero,
GET=PUT `5FD94`, no pending batch or notify, and trapped data `05600141`.
The PFB trap is active and matches the guest's PFB aperture. The final capture
is frozen on the title screen and retains the existing experimental pixel
artifacts. This is a reproduced stale-GET loss in a gate-off control, not a
correctness pass or a resolution of candidate147.

### Completed five-pair short-route result

All ten processes reached the 180-second watchdog exit (code 3); four controls
were nevertheless stalled. All five progress arms finished the route without
a graphics-event stall or a wait entered after the reserved slot was fetched.
The progress arms checked **148,161,279 ring reads**, with zero far reads and a
maximum lag of **1,020 bytes (0x3FC)** in each. Every run had zero invalid GET,
untrusted bounds, freezes, backward jumps, ring CALLs, PB errors, resume
mismatches and batch failures. All observers reported zero discarded or
overwritten records and no read error. Actual child configurations keep IRQ
gating, PFB polling, texture hash/epochs and GPU profiling off, with normal
audio, vblank and controller scripting on. The EXE hash is identical in all arms.

| Arm | Ring reads | Far reads | Max lag, bytes | Waits after slot fetch | Final graphics wait age, s |
| --- | ---: | ---: | ---: | ---: | ---: |
| control 1 | 9,530,237 | 5,543,817 | 63,500 | 1 | 75.33 |
| progress 1 | 28,687,908 | 0 | 1,020 | 0 | 0 |
| control 2 | 29,373,559 | 17,923,787 | 53,132 | 1 | 9.66 |
| progress 2 | 29,638,679 | 0 | 1,020 | 0 | 0 |
| control 3 | 9,219,470 | 5,366,250 | 47,624 | 1 | 75.35 |
| progress 3 | 29,943,297 | 0 | 1,020 | 0 | 0 |
| control 4 | 1,819,470 | 861,503 | 47,624 | 1 | 93.78 |
| progress 4 | 30,136,563 | 0 | 1,020 | 0 | 0 |
| control 5 | 30,359,391 | 18,168,714 | 63,500 | 0 | 0 |
| progress 5 | 29,754,832 | 0 | 1,020 | 0 | 0 |

Far-read counts are dword-load observations, not independent race exposures.
The fifth control progressed despite stale GET, demonstrating why absence of
a stall is insufficient. Instrumentation changes race timing; 4/5 versus 0/5
is not an estimate of the production stall rate. All four lost waits followed
an old placeholder fetch and a post-arm distance of at least 0x2000.

Selected 100/125/160-second captures show progress arms advancing into campaign
cinematics. Progress 2 includes a cinematic fade to black; earlier captures
show mission briefing progression. The existing coalesced pixel artifacts are
visible across these runs. **Pixels and full gameplay/replay are not qualified.**
The new automatic observer successfully captured the main thread and executor
at the control 3 and 4 stalls, without trace loss.

Completed poll loops total about 12.62–12.82 seconds per progress run. These
are wall durations, include descheduling, and exclude a poll still active at
process exit. Stalled controls have shorter work histories, so these totals
are not a matched performance comparison. No speedup or 60 FPS claim is made.

The original pushbuffer fixture, four GET configurations, nine GPU fixtures,
four semaphore fixtures and two IRQ fixtures passed (**20 native tests**).
Accounting and qualification checks passed (**12 Python tests**), and the
launcher environment regression passed. The sweep covers 5,709 read points,
two patch schedules and two reuse modes in each of four configurations:
91,344 scheduled runs, plus the structural and failure cases.

Candidate151 EXE SHA256:
`f19bea59ab04259eb2c6b2e87eecada091a9d0bf9d03e8edc29c1195d42fc341`.
PDB SHA256:
`298cf882ef0d3ca0935183e8eed629ca57d2a385f0f1068b338be2aad4774237`.
Historical prototype, perf147, perf149 and perf150 hashes remain unchanged.

The normal launcher still removes both new flags. GET progress, coalescing and
IRQ gating remain off by default. Next qualification is normal-barrier GET
progress through full gameplay/replay, then coalesced full gates, and only
then performance. Candidate147 remains unresolved.
