# NV2A interrupt-gating investigation

October 14, 2026. Follow-up to `ANSWERS_RESPONSE.md`. The local branch was
fast-forwarded to `b22e9e0685958821e8f160bb0a0f4cd473047461` before this work.
Candidate150 contains uncommitted diagnostic changes over that revision.
Neither interrupt gating nor release coalescing has been promoted to default.

## Original guest instruction order

Capstone disassembly of the original XBE, retained in
`analysis/irq-review-original-disassembly.txt`, gives this order:

| Address | Operation |
| --- | --- |
| `0039EC90` | Clear event SignalState at device+`1970` |
| `0039EC98` | Optionally replace the reserved header with WAIT_FOR_IDLE |
| `0039ECA2` / `0039ECA9` | Write NOP header and parameter 5 |
| `0039ECB0` | Call `0039F140`, the PFB flush helper |
| `0039ECB7` | Call `0039EA90`, the GET-distance helper |
| `0039ECBC` | If distance is at least `2000`, take the event-wait branch |
| `0039ED20` | Wait on device+`196C` (`003AAC9C`) |

The other branch rewrites the reserved methods to NOP 0, flushes, and polls the
fence slot. The clear precedes NOP 5. Neither the audited wait body
`0039EC10–0039ED34` nor action-5 handler `003A5589–003A55A4` contains CLI/STI,
PUSHFD/POPFD, or a direct IRQL import. This is a bounded audit, not proof about
every transitive caller or optional callback. Arm B was therefore not added.

The action-5 handler sets context+`1A4`, which is device+`196C`; it does not
compare the semaphore serial before setting the event. `0039E9E0` emits the
release at the current serial, increments device+`2C` by **two**, and calls the
kickoff helper unless flag 2 is set. Thus `FDC9` versus `FDC7` alone does not
establish that a separate release for `FDC8` exists. Device+`2C` is advanced
after emission, so that two-step difference can be the normal next-serial state.
Without the actual waited serial, it is not proof of an outstanding release.

## Experimental change and diagnostics

`RECOMP_NV2A_IRQ_GATE=1` checks `xbox_IrqlBlocksInterrupts()` before NV2A vector
3 delivery in the common interrupt function. A masked attempt returns before
guest arguments or ISR execution. Notify state and interrupt source bits remain
pending for the existing timer retry. This applies to notify and vblank.

This deliberately matches the APU/OHCI precheck. It is **not atomic exclusion**
against another thread raising IRQL after the check; the trace records the
mask count again before ISR entry so that limitation is observable.

`RECOMP_NV2A_ORDER_TRACE=1` enables a 65,536-record, 48-byte ring. Records have
QPC, thread, event, four arguments and matching sequence markers at both ends.
A read-only observer copies the ring every 0.5 seconds into CSV without
suspending threads. It records losses and inconsistent records explicitly.
Writers do no file I/O, guest-pointer traversal or graphics calls inside the
trace lock. Instrumentation can change race timing; a non-reproduction is not
proof that the original failure disappeared.

Recorded events cover target-event clear/set/wait, IRQL threshold transitions,
trap/notify boundaries, interrupt attempts, observed PUT changes, release
command addresses and publication. PUT is observed by the executor, not a
trace of every original CPU write. Event fields are captured adjacent to the
operation rather than atomically with guest state changes. The event's final
automatic-consumption store is not separately traced.

Snapshots include GET/PUT, notify, executor batch TLS, device fields, serial and
slot, and up to 64 KB between PUT and the guest writer. Bounds/truncation are
reported. Snapshots are asynchronous observations, not atomic GPU dumps.
Batch failure, rejected trap flush, and rejected batch end now also emit an
immediate `[HW FENCE]` line; they do not depend on a later flip report.

Candidate150 EXE SHA256:
`07942cb7968bbda21050c4bf613e7a01b3e51d0398bc58048c99cec8f68fa9e7`

PDB SHA256:
`245f72b4719fb652ab6072018ca7d3a0c17a090ada5c5debd6a968e1a91495c5`

`analysis/build-verification-perf150.json` records the base, core source hashes,
build options and artifact hashes. The previously frozen prototype, 147 and
149 executables were checked unchanged. The existing ignored generated event
clear hook in `recomp_0104.c` is also used; it was not newly regenerated here.

## Qualification scope

The new native fixture exercises a real cross-thread IRQL raise, connected guest
ISR, pending source preservation, retry after lowering, and ISR IRQL restoration.
Both gate-off and gate-on cases passed.

The runtime experiment is three sequential 180-second control/gate pairs on the
same candidate, with release coalescing on and texture hash/epochs off. Actual
child records include vblank=1, normal audio, and absent GPU timing/trace flags.
The route uses Start at 75/95 seconds and menu choices at 105/110/115 seconds;
captures are at 100/125/160, with main/executor inspections and sampling at 135.
It is an early campaign route, not full gameplay/replay qualification or a
performance benchmark. Penumbra remains open.

The earlier `perf150_irq_title_control_1` setup run was stopped at 51.54 seconds
because the short-route launcher had inherited GPU profiling queries. It is
excluded. The corrected workflow prefix is `perf150_irq_title_qf`.

## Repeated-run results

All six observers report zero discarded/overwritten records. All six processes
reached their normal watchdog exit (code 3), **including the stalled run**.
That exit code is not a correctness pass. Captures show five routes moving into
the campaign cinematic and `gate_2` frozen on the title screen. Visible pixel
artifacts remain in these experimental captures; pixels were not qualified.

| Arm / repeat | Completed event waits | Unmatched final wait | NV2A entries reporting raised mask | Result |
| --- | ---: | ---: | ---: | --- |
| control 1 | 5,261 | 0 | 1,884 | Progressed to 180 s |
| gate 1 | 4,798 | 0 | 19 | Progressed to 180 s |
| control 2 | 5,577 | 0 | 1,824 | Progressed to 180 s |
| gate 2 | 1,720 | 1 | 12 | Graphics-event stall at ~102.46 s |
| control 3 | 5,507 | 0 | 1,612 | Progressed to 180 s |
| gate 3 | 4,694 | 0 | 14 | Progressed to 180 s |

Gate deferrals reporting raised IRQL were 1,558 / 415 / 1,247. Some deferrals
report zero and some entries report nonzero because the check and second trace
read are not atomic. This is observed precheck behavior, not full masking.
No listed PB/input/fence error occurred, and emitted sync-failure counts were
zero. Longest completed waits were about 206–222 ms. The failed run's final
wait never returned during the remaining ~77 seconds.

The 2 IRQ fixtures, 9 GPU fixtures, 4 semaphore fixtures and standalone PB
fixture passed (16 total), as did the 8 Python accounting tests and launcher
environment regression including both new flags. The semaphore and PB targets
were rebuilt against the diagnostic sources. These do not qualify runtime
coalescing.

## The failed run points to stale DMA GET, not an executed action-5 wakeup

Use `perf150_irq_title_qf_gate_2-order.csv`, its main/executor inspections, and
`perf150_irq_title_qf_gate_2-critical.csv`. Sequence numbers below refer to the
full CSV. QPC values are in seconds on the host's monotonic clock.

| Sequence | QPC | Observation |
| ---: | ---: | --- |
| 394030 | 79331.1045638 | Observed PUT `153268 -> 158B98`, GET still `153268` |
| 394212 | 79331.1058971 | Executor reads release `F011` at parameter VA `80158B24` |
| 394214 | 79331.1058974 | Executor has already reached the next release `F013` at `80158B3C` |
| 394216 | 79331.1058981 | Main clears event+SignalState |
| 394217–18 | 79331.1059020 | Main enters wait and resets the host event |
| 394219 | 79331.1063002 | Executor reaches SDK marker `05600141` at `80158B5C` |
| 394221 | 79331.1063008 | Publishes `F013` before that marker's notification |
| 394235 | 79331.1063741 | Marker trap returns normally |
| 394236–38 | 79331.1066056 | Reads and publishes `F015`; no later command execution |

There is **no NOP 5 and no target-event SetEvent after the final clear**. The
last action-5 SetEvent was at sequence 393954, followed by a successful earlier
wait return. This is not evidence of an action-5 ISR landing inside the final
clear/check/wait window: the required final action-5 trap never ran.

The main snapshot retains `EAX=58B8`, the GET-distance result, and caller stack
arguments consistent with waiting on serial `F011`. Its release starts at
`80158B20`; the SDK's reserved action-5 parameter is therefore at `80158B34`.
The executor had passed that location before the clear, because it had already
read the following release parameter at `80158B3C`. The original code writes
NOP 5 only after that clear, before entering the wait. This explains why no
action-5 command appears in the executor trace for the final wait.

The reported distance matches the old GET boundary exactly:
`80158B20 - 80153268 = 58B8`, comfortably above the SDK's `2000` wait threshold.
The GET-distance helper also contains wrap/position adjustments; the failed run
did not capture EBP or the full SDK fence-record table, so the argument/address
reconstruction should be reviewed with that limitation in mind.

At 135 seconds the main thread remains at the event wait, SignalState=0,
notify=0, IRQL count=0, GPU status bits clear, GET=PUT=`158B98`, writer=PUT,
slot=`F015`, and next serial=`F017`. The unsubmitted tail is empty. The executor
snapshot has no failed/pending batch. Thus the waited release was published;
the missing item is the late-patched action-5 method, not an unsubmitted suffix
or an executor still awaiting notification. The later last-flip counter age is
~78 seconds. Candidate147's historical failure is not retroactively proven to
have this identical cause, but this reproduces its graphics-event-wait shape.

Source agrees with the mechanism: `nv2a_ack_thread` writes DMA GET=PUT only
**after the entire `nv2a_pb_scan` returns**. During a segment, the SDK can see
GET thousands of bytes behind the actual command walk. Coalescing makes that
gap easier to cross before the guest installs its reserved trap.

## Next implementation and review questions

Do not promote the IRQL precheck or coalescing on this evidence. Arm A failed;
Arm B still lacks a CLI/STI use case. Full barrier-mode gating qualification
was conditional on a demonstrated fix, so it was not started.

The next isolated candidate should expose conservative **within-segment DMA GET
progress after commands have been consumed**, preserving GPU completion and
semaphore publication rules. It must not set GET to the whole PUT before the
walk, and must not replay or synthesize action-5 notifications to unstick a
wait. The initial implementation should be opt-in and independently toggleable
from interrupt gating, with the failed run frozen as the control evidence.

Before a runtime comparison, add a deterministic regression for the SDK pattern:
reserve the action-5 location, let the executor pass it inside a long submitted
segment, arm it from the guest, and perform the GET-distance recheck. The guest
must observe current progress and choose its fence-poll path. Cover partial
method payloads, ring wrap, jump/CALL/RETURN, SDK NOP records, failures, and
normal/coalesced publication without acknowledging unread storage or unfinished
GPU work. Then repeat short routes, normal-barrier full gameplay/replay gates,
coalesced full routes and finally performance comparisons.

Questions for Claude:

1. Does the `F011` / `80158B34` reconstruction and GET-distance equation support
   the late-patched, already-consumed method explanation? Any other use of the
   fence-record position adjustment that changes that reading?
2. For this synchronous front end, which conservative GET update points preserve
   command-storage reuse through CALL/RETURN, inline SDK records and split
   payloads? In particular, separate command consumption from GPU completion.
3. Should IRQ exclusion be addressed independently with atomic serialization,
   rather than promoting the APU/OHCI-style precheck as a fix for this stall?

The captures, timings and raw traces are diagnostics. No new speedup or smooth
60 FPS result is claimed.
