# Response: late-patched action-5 command and within-segment DMA GET

October 15, 2026. Review of `IRQ_FOLLOW_UP.md` and `../../PERFORMANCE_NEXT.md`
at `99a3960`, using the raw evidence in
`../../analysis/claude-perf150-irq-evidence.zip`. The archived source copies
(`nv2a_pb_scan.c`, `nv2a_pb_exec.c`, `kernel_bridge.c`, `kernel_hal.c`) are
identical to the tree at `99a3960`, apart from line endings. Nothing in the
runtime was changed for this review. The trace re-analysis described below was
done with throwaway scripts over the six `*-order.csv` files. The method is
described so it can be reproduced.

This review does not resolve candidate147's historical cause. It also does not
treat the NV2A IRQL precheck as a fix.

## Verdicts

1. **The reconstruction holds for `gate_2`, and it holds more strongly than
   `IRQ_FOLLOW_UP.md` claims.** The captured guest registers place the main
   thread at the event wait `0039ED20`, after the post-arm recheck at
   `0039ECB7`. They also show that the helper's second distance term was zero.
   The waited serial is `F011`, its release starts at `80158B20`, and the
   executor read the reserved words before the guest cleared the event. The
   distance `58B8` equals `record − GET` exactly. No other release in the
   segment produces `58B8`. I found no alternative explanation that fits all of
   the evidence. A few gaps remain, listed in §1.5. One correction to the
   framing: the defect is not specific to coalescing. Every run, including all
   three controls, armed waits into the segment the executor was walking, and
   about 750 of those waits won the race. Coalescing removes a delay that
   usually protected the reserved words (§1.4).
2. **Contract.** DMA GET must trail the executor's consumption cursor. It must
   also stay close enough to that cursor that nothing past `GET + 0x2000` has
   been read. A cursor-trailing publication with a stride of at most 0x400
   bytes satisfies both. Publish it with a full barrier, only from the
   walker's loop, and never as an out-of-ring address, `high`, or `PUT`. GET
   reports command consumption only. It must not be tied to GPU completion or
   to semaphore publication, and those rules stay as they are (§2).
3. **Control flow.** During a CALL, hold GET at the return address in the ring.
   After a forward JUMP, publish the target. For the wrap, publish the SDK's
   `low` only when the bound is exact. At a split payload, GET is already PUT.
   GET may move past an SDK NOP record only after `next` has been read. If the
   walk fails, freeze in-segment publication and keep the existing end-of-scan
   policy (§3).
4. **Testing.** Build a single-threaded, exhaustive-interleaving fixture with
   a C port of `0039EC10`/`0039EA90` and a storage-poisoning publisher. Before
   any runtime run, add a fetch-bound monitor and a wait-serial hook, so that
   qualification measures the invariant itself rather than stall counts (§4,
   §5).
5. **Yes, treat IRQ exclusion as separate work.** The stall involves no
   interrupt. The precheck is non-atomic, and it also masks at the wrong level
   (§6).

---

## 1. F011 reconstruction and GET-distance equation

### 1.1 What the SDK code does

From `irq-review-original-disassembly.txt`:

- **`0039E9E0` (emit release).** This writes six dwords at the writer pointer:
  `00041D70 serial`, `00041D90 0`, `00041D90 0`. That is one
  BACK_END_WRITE_SEMAPHORE_RELEASE followed by two one-word
  `SET_COLOR_CLEAR_VALUE 0` placeholders. It then stores a record at
  `device + 0x4C + 12·((serial >> 1) & 0x3F)` = `{serial, release address,
  device[0x3C]}`. After that it advances `device+2C` by 2 and kicks off unless
  flag 2 is set.
- **`0039EC10(serial, flags)`.**
  1. If the serial is already complete against the slot, return.
  2. If `serial == next`, emit a release.
  3. Look up the record (`0039EAF0`).
  4. Compute the distance. If it is below `0x2400`, take the poll path.
  5. Optional callback at `+15AC`, then recheck with a threshold of `0x2000`.
  6. Set `ebp = record.address`, then clear SignalState at `+1970`.
  7. Unless `flags & 8`, patch `+8/+C` to `WAIT_FOR_IDLE 0`.
  8. Patch `+10/+14` to `NOP 5`.
  9. Flush with `0039F140`.
  10. Recompute the distance at `0039ECB7`. At `0x2000` or more, wait on the
      event with no recheck of the slot. Below that, rewrite all four words to
      `NOP 0` and poll the slot.
- **`0039EA90` (distance).** The calculation:
  - `GET = [[dev+17C0]+0x44] | 0x80000000`, which is USER DMA_GET.
  - If GET falls outside `[dev+24, dev+28)`, use
    `[[dev+4F8]+0x324C] & ~1 | 0x80000000` instead, which is PFIFO
    CACHE1_DMA_SUBROUTINE's return offset.
  - Normalise for wrap against the writer `dev[0]`: add the ring size
    `dev[0x40]` to GET if `GET ≤ writer`, and to the record address if
    `addr ≤ writer`.
  - `d1 = max(0, addr' − GET')`.
  - `d2 = max(0, record[+8] − dev[0x1FE0])`, returned in EDX.
  - Return `d1 + d2` in EAX.

The SDK's design is visible here. The ring has no write barrier toward the GPU
for an already-submitted reserved slot, so after arming the slot the guest
re-reads GET. If the GPU's fetch point is within `0x2000` bytes of the record,
the GPU may already have fetched the slot, so the guest polls. **The protocol
assumes the fetch point never runs more than about 8 KB ahead of the reported
GET.** Hardware DMA GET is the pusher's fetch pointer, so it honours this.
The runtime publishes GET only after `nv2a_pb_scan` returns
(`xbox_memory_layout.c`, `nv2a_ack_thread`), so it violates the assumption
whenever a segment is longer than 0x2000 bytes.

### 1.2 What the gate_2 snapshot proves

From `perf150_irq_title_qf_gate_2-guest-inspection.json`, main thread 24152:

| Captured | Meaning |
| --- | --- |
| `EDI = FE000030` | `mov edi,[0x3B3970]` executed, so this is the KeWaitForSingleObject thunk. The thread is in the `0039ED14+` wait block. |
| `ESI = 003AAC9C` | `device + 0x196C`, so the target event. |
| `ECX = 003A9330` | `mov ecx,esi` at `0039ECB5`, so the device. This is the input to the final recheck. |
| `EAX = 58B8` | The return value of the call at `0039ECB7`. EAX is zeroed at `0039EC8B`, and no later instruction before the host call writes it. So this is the **post-arm** recheck, not the pre-arm one. |
| `EDX = 0` | `d2` as left by `0039EA90`. **The second term contributed zero**, which removes the main uncertainty that `IRQ_FOLLOW_UP.md` raised. |
| Stack `[esp..+10]` | `003AAC9C, 6, 1, 0, 0`: Object, WaitReason, UserMode, non-alertable, infinite. |
| Stack `[esp+14..+20]` | Saved EDI, ESI, EBP and EBX of the caller. |
| Stack `[esp+24..+2C]` | Return address `0039A73C`, then the arguments `serial = F011` and `flags = 4`. Flags = 4 means bit 8 is clear, so WAIT_FOR_IDLE was patched as well. |
| `dev[0] = 80158B98` | Writer pointer = PUT. Both GET `80153268` and the record `80158B20` are ≤ writer, so both are normalised by `+size` and the adjustment cancels. |
| `dev[24]/[28] = 80002000/80182000` | GET `80153268` is inside the ring, so the DMA_SUBROUTINE fallback was not taken. |

The wrap normalisation cannot change the result. For any writer outside
`[GET, addr)`, the difference is `addr − GET`. A writer inside that range would
clamp `d1` to 0 and take the poll path, which did not happen. The other
releases in the segment would give `F00F → 3E48`, `F013 → 58D0` and
`F015 → 5918`. Only `F011` gives `58B8` with `d2 = 0`.

**Reserved-word addresses.** The `F011` parameter is at `80158B24`, so the
release starts at `80158B20`. The reserved commands are at `B28/B2C` and
`B30/B34`. The executor traced the next release's parameter at `80158B3C`
(header at `B38`, immediately after the six-word release) at QPC `…1058974`.
The main thread's clear followed at `…1058981`, **about 0.7 µs later**. The
original code writes the reserved words only after that clear. So the
executor read the placeholders (`SET_COLOR_CLEAR_VALUE 0` ×2) before the guest
wrote anything, by under a microsecond.

**Corroborating state at 135 s.**
- `PGRAPH_TRAPPED_DATA = 0x05600141`: the last trap was the action-1 marker,
  not NOP 5.
- `notify_pending = 0`, `PGRAPH_INTR = 0`.
- Slot = `F015`, so `F011` is complete. The wait path never rechecks the slot.
- The ring has zero lost records, and there is no NV_ORDER_COMMAND
  `0x100/5` after sequence 393941.
- The event header dword is `0x00040000`. Type byte 0 is a *notification*
  (manual-reset) event under the NT/Xbox dispatcher numbering, so there is no
  automatic consumption that could have swallowed a set. The only way to stay
  blocked is for no set to happen, and none is traced. This depends on the
  header at 135 s being the original object type, which is a reasonable
  assumption.

### 1.3 Alternatives considered

| Alternative | Status |
| --- | --- |
| A masked or late ISR lost the action-5 wakeup (the IRQ hypothesis) | **Excluded for this instance.** The action-5 command never executed, so no notify was raised for it. Neither the gate nor the ISR timing could matter. |
| An action-5 SetEvent was consumed by another waiter, or reset by the host | **Excluded.** It is a notification event, and no set follows the final clear. |
| The guest patched a stale or wrong record | **Excluded.** EAX would not equal `F011.address − GET` with EDX = 0. |
| The helper's adjustment terms (`d2`, wrap, subroutine fallback) produced `58B8` from a different record | **Excluded.** EDX = 0, GET is in bounds, and the writer is above both addresses. |
| The executor read a cached or snapshot copy rather than live guest memory | **Excluded by source.** `pb_walk` dereferences guest memory word by word, and `nv2a_hw_acquire_input` only syncs prior GPU writes. |
| The guest's NOP store was in flight but invisible (store-buffer ordering) | **Excluded for this instance.** The clear that precedes the patch is ordered after the executor's read, by 0.7 µs of invariant QPC. In general the ordering *does* matter once GET publication exists (§2.4). |
| The executor skipped `B28..B37` (NOP-record skip, jump) | **Not observable but implausible.** Only action-3 NOPs trigger the skip, no jump was traced, and the next header at `B38` is exactly where the six-word layout puts it. |
| An unsubmitted tail, or the executor waiting on a notify | **Excluded by the snapshot.** The tail is empty, there is no pending batch, and GET = PUT. |
| The gate caused the stall | **No evidence either way.** 1/3 gated runs versus 0/3 controls gives a one-sided Fisher p of 0.5. §1.4 shows the hazard is present in every control run. |

### 1.4 The hazard occurs in every run; gate_2 is the one loss

I re-walked all six order traces. For each event clear on `003AAC9C`, I
identified the waited release from the action-5 NOP that eventually woke it
(release = NOP parameter VA − 0x14). I then asked whether that release lay
inside the segment the executor was walking when the guest armed it. That
segment is the last observed PUT interval, and the executor writes GET only
when that interval ends.

| Run | Completed waits | Armed into the in-flight segment (all ≥ 0x2000 past GET) | Lost |
| --- | ---: | ---: | ---: |
| control 1 | 5,261 | 158 | 0 |
| control 2 | 5,577 | 182 | 0 |
| control 3 | 5,507 | 181 | 0 |
| gate 1 | 4,798 | 117 | 0 |
| gate 2 | 1,720 | 1 (+ the failing one) | 1 |
| gate 3 | 4,694 | 115 | 0 |

So about 755 waits across six runs armed a reserved slot that the executor
was already walking toward while the reported GET was more than 8 KB behind.
Every one of them depended on the guest writing before the executor arrived.
That is roughly one loss per 750 exposures. This is weak as a rate estimate,
but it shows the precondition is ordinary in controls. All in-flight cases
were at least 0x2000 past GET. That is expected, because the SDK takes the
event path only when the distance is at least 0x2400, which confirms the
classification.

**Why coalescing exposes the race.** In `nv2a_pb_exec.c` (`case 0x1D70`),
barrier mode calls `nv2a_hw_sync_reason(NV2A_SYNC_RELEASE)` at every release
before queueing it. In both modes the semaphore store is published later, at
the next trap flush or at batch end. So barrier mode does *not* publish
earlier. What it does is **stall the walker at the release dword for a GPU
drain, immediately before the four reserved words.** That pause gives the
guest a wide window to arm. Coalescing removes the pause, so the walker runs
straight through the reserved words. In gate_2 the walker went from
`153268` to `B3C` in 1.33 ms, passing five releases. The root defect,
stale GET, is the same in both modes. Barrier mode is not proven safe; its
remaining window is just much smaller.

### 1.5 What is still unverified

These items do not undermine the mechanism, but they should be captured in
the next stall snapshot (§5, stage 0):

- **The patched words.** Dump the ring at the record address `+0..+0x18`.
  With GET = PUT and the main thread blocked, they should still read
  `00041D70 F011 00040110 0 00040100 5`. This is the one direct check the
  evidence lacks.
- **Record lookup `0039EAF0`.** Not disassembled. The record for `F011` should
  be at `device + 0xAC` (`003A93DC`). Capture `device+0x4C..+0x34C`,
  `+0x3C`, `+0x40`, `+0x1FE0`, `+0x15AC`, `+0x15B0` and `+0x4F8`, and PFIFO
  `0x3240..0x324C`.
- **Flush helper `0039F140`.** Not disassembled. Its body determines whether
  the guest's NOP stores are ordered before its GET load (§2.4).
- **EAX/EDX survival** relies on the host KeWait thunk not writing guest
  registers before it returns. That is consistent with every other register
  in the snapshot, but it is a property of the bridge and should be
  documented.

**Candidate147.** It has the same outward shape (a title-screen graphics-event
wait with coalescing on), and it is consistent with this mechanism. It remains
unproven. The fetch-bound monitor and wait hook in §5 are the discriminators
if that configuration is ever re-run.

---

## 2. Within-segment GET update points

### 2.1 Three frontiers that must stay distinct

| Frontier | Meaning in this runtime | Guest-visible through |
| --- | --- | --- |
| **Consumed (fetch)** | `pb_walk`'s cursor. Every word before it has been read and passed to `nv2a_pb_exec_method`, and that call has returned. | DMA GET (this proposal) |
| **GPU complete** | Host GPU work for earlier draws has finished (`nv2a_hw_sync*`). | Nothing directly; CPU-side acquisition paths |
| **Published** | The semaphore store has been written (trap flush or batch end, after completion). | The fence slot `*(dev+30)` |

GET reports the first frontier only. On hardware, GET passes a release long
before the back end writes its semaphore, and the SDK handles that: after
arming, the poll path spins on the slot. **Do not publish a semaphore early to
"match" GET, and do not wait for GPU completion before advancing GET.** The
existing rules for publishing semaphores and failing batches stay as they are.

The consumption frontier is safe for storage reuse because the front end
copies its inputs synchronously:
- Parameters are passed by value.
- INLINE_ARRAY data is copied into `inline_buf`.
- ARRAY_ELEMENT16 and DRAW_ARRAYS write indices into `s_gpu.idx`.
- Split-payload resume state stores the method, count and address, not
  contents.
- The NOP-record words are read before the skip.

Vertex, index and texture memory that commands *point to* is not command
storage. Fences govern it, as on hardware. Run an audit grep for any
`mem+va` pointer retained beyond the loop iteration. I found only diagnostic
paths: `s_headers` stores values, and `pb_capture_first` dumps
`root_start..root_end` and should be labelled "may contain reused storage".

### 2.2 Invariants for a published GET

Let `G` be the last value made globally visible, `C` the cursor, `A` any ring
address the walker reads, and `low`/`high` the SDK ring bounds.

- **I1 (storage).** `G ≤ C` in ring order. Never publish a byte that has not
  been consumed.
- **I2 (fetch bound).** Before reading any ring word at `A`,
  `ringdist(G, A) ≤ S`, with `S < 0x2008`. Derivation: the guest waits only
  if `record − G ≥ 0x2000`, and the reserved words are at `record + 8 …
  record + 0x17`. For a stale read at `A` to be safe, the guest must see
  `G > A − 0x2008`. **Use `S = 0x400`.** It costs about 22 locked stores for
  the 22 KB gate_2 segment and leaves an 8× margin.
- **I3 (range).** `low ≤ G < high`. Never publish `high`; map it to `low`.
  Never publish a subroutine or out-of-ring address. The runtime does not
  maintain `0x324C` (see the comment at `NV2A_PFIFO_DMA_SUBROUTINE`), so an
  out-of-ring GET would send `0039EA90` to its fallback, read 0, and compute
  nonsense.
- **I4 (end).** In-segment publication never writes `PUT`. `GET == PUT`
  remains the single post-scan store, after `xbox_Nv2aFenceBatchEnd`, so any
  code keyed on equality sees the same ordering as today.
- **I5 (source).** Publish only from `pb_walk`'s loop, between dword reads.
  Never publish from inside `nv2a_pb_exec_method` or a trap round trip. At a
  trap, the cursor is the NOP parameter's end, which for action 3 is the
  record start, so a trap never exposes record bytes.

### 2.3 Safe and unsafe update points

**Safe points.** Each value is published only if it advances `G` by at least
`S`, or if the event requires it:

1. After a complete command (header and all parameters returned from
   `exec_method`): `C` = the next header.
2. Inside a long payload, after any parameter returns. This is needed for
   I2: one INLINE_ARRAY can be 2,047 words (8,188 bytes).
3. After a forward JUMP within the root: the target. Skipped bytes count as
   consumed, as on hardware.
4. After a CALL header: the return address, held for the subroutine's whole
   duration.
5. After a RETURN: continue from the return address (no new value is needed).
6. After an SDK action-3 record skip: `next`.
7. Between the two wrap scans: `low`, only when the bound came from the SDK
   binding (§3).

**Unsafe points:**
- Before the walk, or `PUT` at any time before batch end.
- Inside `exec_method`, a trap, or a record.
- Any address outside the ring, or equal to `high`.
- Anything beyond a failure point.

### 2.4 Memory ordering

The race is a store-buffering pattern:
- Executor: `store G` then `load slot`.
- Guest: `store slot` then `load G`.

Excluding "both stale" needs a full barrier on **both** sides.

- **Executor side.** Publish with `InterlockedExchange` (a locked instruction
  on x64) so the GET store is globally visible before later command loads. A
  plain volatile store is not enough.
- **Guest side.** The original code has `0039F140` between the patch and the
  recheck. Codex's follow-up check found that it executes `SFENCE`, writes
  PFB `+0x100410`, then polls it, and that **the generated C drops the
  `SFENCE`**. The guest's only remaining barrier is therefore the default PFB
  page trap (`pfb_arm_write_trap`, `PAGE_NOACCESS`): the fault serialises,
  and `pfb_write` uses `InterlockedExchange`. With `RECOMP_NV2A_PFB_POLL` set,
  or if the trap fails to arm, no guest-side barrier exists. **Qualification
  must verify the trap is active** (the `NV2A PFB: coherent write-combine
  flush completes on write` line, plus a counter of PFB faults taken). It
  must not merely assume the launchers leave the flag unset. Separately,
  consider making the recompiler emit a full fence for `SFENCE`/`MFENCE`/
  `LFENCE`, so ordering does not depend on a debug trap staying enabled.
- **Compiler side.** Check the generated C for `sub_0039EC10`, and for
  `0039F140` if it is inlined. The compiler must not move the patch stores
  past the GET load. Guest stores to different addresses are reorderable
  unless they are volatile or separated by an opaque call.

---

## 3. Control flow, storage reuse and failures

| Case | Recommendation |
| --- | --- |
| **CALL → subroutine** (`return_va` set, `child = 1`) | Publish `return_va` (the CALL word is consumed) and hold it. Do not publish subroutine addresses (I3). I2 concerns only ring words, which are not read during the subroutine. If a CALL target lies *inside* the ring, count it as a diagnostic and still hold GET. Nested CALLs are already rejected. |
| **RETURN** | Resume at `return_va`. Normal stride publication continues from there. A RETURN without a CALL is already a reject in `child`. |
| **JUMP forward within the root** | Publish the target before reading it. |
| **JUMP backward within the root** | Not expected from the SDK. Hardware would move GET backward. Publish the target, which is faithful and keeps I1 because those bytes are about to be re-read, and count it with a one-line log. The expected count is 0. If it is ever nonzero, investigate before continuing qualification. |
| **JUMP out of the ring** (`child = 1` via jump) | Hold at `from + 4`. On the jump back to `T` in the root, publish `T` before reading. A jump to a ring address outside the root still `break`s, as today. |
| **Wrap** | The ack thread scans `[last_put, put_hi)` and then `[put_lo, put)`. The first walk `break`s on the JUMP to `low`. Publish `low` between the two pieces **only if** the bounds were read from the SDK binding (`[PB RING] SDK bounds …`). With learned bounds, hold until the final `GET = PUT`. Never publish `high`. Today `nv2a_pb_set_ring_bounds(low, high)` receives learned and SDK values through the same call, so the walker cannot tell them apart. **Pass provenance explicitly**, for example `nv2a_pb_set_ring_bounds(low, high, NV2A_RING_SDK_VERIFIED)`. All in-segment publication, not only the wrap, should require SDK-verified bounds, because I3 depends on them. With learned bounds, GET progress stays off for that segment, and a counter records it. Run a fixture where the record is before the wrap and GET is after, with the writer below GET (§4, F). |
| **Split payload** (`s_pending_*`) | Mid-payload publication is safe (I1). At segment end, `GET = PUT` mid-method is unchanged from today. A `[PB RESUME]` mismatch remains a diagnostic. |
| **SDK action-3 records** | The cursor never lands inside a record; it moves from the record start directly to `next`, and only after `acquire_input` and the read. Do not "optimise" by publishing `record + n`. |
| **Storage reuse** | After `G` advances, the guest may overwrite `[old G, G)`. The poisoning fixture (§4, I) proves that nothing re-reads that region. Diagnostic dumps must say that they may show reused bytes. |
| **Failures** (`pb_reject`, unreadable, unknown header, budget, bad record, rejected trap flush) | The walker sees only its own `pb_reject`. Command handlers fail independently: `nv2a_pb_exec.c` has eight `xbox_Nv2aFenceBatchFail()` call sites (a release sync failure, an invalid vertex range, …), and a rejected trap flush simply `return`s at line 3132. In each of these cases the walker keeps going. **GET progress must therefore gate on the batch-failed state, not on the walker's own failures.** Expose a cheap query such as `xbox_Nv2aFenceBatchFailed()`, or have `BatchFail` set a walker-visible latch. Check it after every `exec_method` return, before any publication. Once it is set, freeze in-segment GET for the remainder of the scan. Stop in-segment publication at the last fully consumed command before the failing word. Batch-fail semantics stay unchanged: no semaphore publication from the failed prefix. Leave the existing post-scan `GET = PUT` policy unchanged, so the experiment changes only one variable. Log `[PB GET] frozen at X on failure; final policy PUT`. Whether failures should instead freeze GET is a separate decision; it would turn silent overwrites into a loud ring-full hang. |

**Implementation sketch.** This is opt-in, for example
`RECOMP_NV2A_GET_PROGRESS=1`, and independent of `RECOMP_NV2A_IRQ_GATE` and
coalescing.

```c
/* nv2a_pb_scan.c */
static void (*s_get_publish)(uint32_t va);   /* bound by the ack thread */
static uint32_t s_get_pub;                   /* last published VA */
void nv2a_pb_bind_get_publisher(void (*fn)(uint32_t)) { s_get_publish = fn; }
static int s_ring_verified;                  /* set only from SDK-bound provenance */
static void get_progress(uint32_t ring_va, int force)
{
    if (!s_get_publish || !s_ring_verified || xbox_Nv2aFenceBatchFailed()) return;
    if (ring_va < s_ring_low || ring_va >= s_ring_high) return;
    if (!force && ring_va - s_get_pub < 0x400u) return;   /* same lap only */
    s_get_pub = ring_va; s_get_publish(ring_va);
}
/* pb_walk: before each header read and each parameter read, when !child:
 *   get_progress(va, 0);
 * after JUMP to an in-root target:  get_progress(target, 1);
 * after CALL:                        get_progress(return_va, 1);
 * after NOP-record skip:             get_progress(next, 0);
 * never after pb_reject or a handler-side BatchFail (checked above);
 * never with va == end_va of the final piece. */

/* xbox_memory_layout.c */
static void nv2a_get_publish(uint32_t va)
{
    InterlockedExchange((volatile LONG *)((char *)g_nv2a_memory + NV2A_USER_DMA_GET),
                        (LONG)(va & 0x0FFFFFFFu));
    nv2a_order_trace(NV_ORDER_GET, va, /*reason*/0, 0, 0);
}
```

Initialise `s_get_pub` to the segment start at `nv2a_pb_scan` entry. Handle
`ring_va − s_get_pub` across the wrap explicitly, or reset it at the wrap
publication. Add `NV_ORDER_GET` to the trace enum.

---

## 4. Deterministic regression

Extend `tests/nv2a_pb_control`. It is already single-threaded and already
stubs `exec_method`, the fence batch and `acquire_input`. Add:

- **A guest model.** A C port of `0039EC10`, `0039E9E0`'s release and record
  layout, and `0039EA90`, including the wrap normalisation, the `d2` term and
  the `0x324C` fallback. It operates on a fake device struct
  (`0x0, 0x24, 0x28, 0x2C, 0x30, 0x3C, 0x40, 0x4C…, 0x1FE0`) and reads GET
  from the publisher's captured value.
- **A per-dword read hook** (`NV2A_PB_TEST_HOOKS`). It is called before every
  header and parameter read, so the guest can act at any point in the walk.
  `acquire_input` covers this only when coalescing is on; the hook must also
  run in barrier mode.
- **A publisher callback** that records every published value and checks
  I1–I4.

| Test | Asserts |
| --- | --- |
| **A. gate_2 replica** | Segment `153268 → 158B98` scaled down. Releases `F00D…F015` at the observed offsets, back-to-back `F011`/`F013`, then the action-1 marker. The guest calls `wait(F011)` when the walker reaches `F013`'s header. **Flag off: the model takes WAIT and no NOP 5 ever executes. This reproduces the bug and validates the test.** Flag on: the model takes POLL, and `F011` is published at the marker trap. |
| **B. Interleaving sweep** | For every dword read point `k` in the segment, start `wait(F011)` at `k`. For every `k`, the outcome must be either (i) WAIT and then exactly one NOP-5 trap and event set, or (ii) POLL and then eventual publication. Never WAIT without a trap. This covers tearing: header read old and parameter new (`COLOR_CLEAR = 5`, no trap, which must be POLL), header new and parameter old (`NOP 0`), and WAIT_FOR_IDLE patched with the NOP not yet patched. Run it with coalescing on and off. |
| **C. Long payload** | A 2,047-word INLINE_ARRAY before the release. Every read satisfies I2 with `S = 0x400`, and GET advances inside the payload. |
| **D. CALL/RETURN** | A 0x8000-byte subroutine outside the ring containing draws, then a release in the ring. GET holds the return address throughout, is never out of ring, and the model never hits the `0x324C` fallback. |
| **E. JUMP** | Forward in-root (GET = target), out-of-ring and back, and backward (counted, GET = target). |
| **F. Wrap** | Record before the ring-end JUMP. The guest arms after the walker passes the wrap. Writer below GET. GET is never `high`. The `0039EA90` port yields `< 0x2000` once the slot has been passed. Repeat with learned bounds: no wrap publication. |
| **G. Split payload** | A segment ends mid-INLINE_ARRAY. Resume is correct, in-segment GET ≤ the split point, and final GET = PUT. |
| **H. SDK action-3 record** | GET never lands in `(record, next)`. The record is read before any publication past it. |
| **I. Poisoning** | For A–H, the publisher overwrites `[old G, new G)` (wrap-aware) with `0xDEADBEEF`-style headers immediately. The executed `(method, param)` stream and the release/publish sequence must be identical to an unpoisoned run. **This is the direct proof of "no acknowledged unread storage".** |
| **J. Failures** | Unreadable word, unknown header, budget, bad record continuation, **and handler-side failures the walker does not see**: a stub `exec_method` that calls `xbox_Nv2aFenceBatchFail()` mid-segment, and a rejected trap flush. No publication past the last good command; GET is frozen from the handler failure onward. Batch-fail behaviour is unchanged. Final GET follows the existing policy. |
| **K. Publication unchanged** | With the flag on and off, the semaphore `PUBLISH` sequence and its timing relative to traps and batch end are identical. Only `GET` events differ. |
| **L. Equality** | In-segment publication never equals PUT. The final GET is written after `FenceBatchEnd`. |

An **optional stress litmus test** (non-gating) uses two host threads: the
executor does publish → load word, and the guest does store word →
`InterlockedExchange` (standing in for the PFB fault) → load GET. Run about
10⁷ iterations and assert there is no stale/stale outcome. Removing either
barrier should eventually show the outcome, which documents why both are
required.

Keep the 2 IRQ fixtures, 9 GPU, 4 semaphore and standalone PB fixtures, the 8
Python tests, and the launcher-environment regression (with the new flag added
to its disabled-by-default list).

---

## 5. Staged qualification

Stall counts alone cannot qualify this change. The baseline loss rate is about
1 in 750 exposures, roughly 1 in 6 short runs, with a very wide interval. Each
stage below measures the invariant directly.

**Stage 0: instrumentation and evidence closure (no behaviour change).**
- Disassemble `0039EAF0`, `0039F140`, the action-1/action-3 marker emitters,
  and **every reader of `[dev+17C0]+0x44` and `+0x324C`** in the XBE. For each
  GET reader, record what it concludes. Any reader that infers *completion*
  from GET passing an address would need separate handling. None is expected,
  because hardware GET also leads completion, but the current runtime makes GET
  stronger than hardware does and a title could depend on that by accident.
- Add a generated hook at `0039EC10` entry and after `0039ECB7`. Record the
  serial, record address, `d1`, `d2`, the published GET and the path taken
  (poll or wait). This makes "wait entered after the slot was read" an exact
  count instead of an inference from wakeups.
- Add a **fetch-bound monitor** (`RECOMP_NV2A_GET_MONITOR`), valid with the
  feature off as well. On each ring read, count the reads where
  `ringdist(G_visible, A) ≥ 0x1C00`. Controls should show thousands per run,
  which shows the monitor sees the hazard.
- Extend the stall snapshot as listed in §1.5.
- Record whether the PFB trap armed, and count the PFB faults taken. The
  guest-side ordering depends on it, because the generated C drops
  `0039F140`'s `SFENCE`.
- Add ring-bound provenance (SDK-verified vs learned) to the order trace.

**Stage 1: fixtures.** §4 in full, plus the existing suite. Run them in
barrier and coalesced configurations.

**Stage 2: short route, coalescing on, IRQ gate off.**
- Same route as candidate150, one binary, alternating flag-off and flag-on.
  Use at least five pairs; more is better.
- Pass criteria for flag-on:
  - zero monitor violations;
  - zero waits entered with the slot already read (from the hook);
  - zero `[PB RESUME]` mismatches, PB errors, batch failures or backward
    jumps;
  - no graphics-event stall.
- Flag-off must show monitor violations above zero; otherwise the test is
  blind.
- Report poll-path entries and spin time per wait. These are new CPU costs.

**Stage 3: GET progress with normal barriers (coalescing off), full
gameplay/replay gates.** This checks storage reuse under the earlier GET
back-pressure:
- geometry stability (the "unstable geometry / lost commands" symptom
  described at the old top-of-tick GET);
- pixel and replay gates;
- normal audio and controller;
- monitor at zero.

**Stage 4: GET progress plus coalescing, full routes.** Same gates.
Separately classify the known DirectSound movie stall.

**Stage 5: performance.** Compare coalescing+progress against barrier+progress
on the gameplay profile from `PERFORMANCE_NEXT.md` §1, with matched hardware
conditions. Measure the poll-path spin explicitly.

**Promotion.** Each flag is independent and defaults off. Promote GET progress
on its own after stage 3. Promote coalescing only after stage 4. The IRQ gate
stays out of all of these arms.

---

## 6. IRQ exclusion is separate work

- **It is not related to this stall.** The action-5 command never executed,
  so there was nothing to deliver.
- **The precheck masks at the wrong level as well as being non-atomic.**
  `xbox_IrqlBlocksInterrupts()` counts threads at **DISPATCH_LEVEL or above**.
  On hardware, DISPATCH masks DPCs and thread dispatch but **not** device
  interrupts, which run at DIRQL. Only raising to at least the NV2A's DIRQL,
  `KeSynchronizeExecution` or CLI masks the ISR. So the precheck over-masks:
  it delays the NV2A ISR whenever any thread holds a spinlock or runs a DPC.
  The 415–1,558 deferrals per run are that latency. It under-excludes too,
  because a raise after the check races the ISR, and the guest's uniprocessor
  exclusion of DPCs against threads is not modelled at all.
- **New interaction.** With GET progress, more waits take the poll path,
  which spins without lowering IRQL. If any spinning caller is at DISPATCH,
  the gate would defer the ISR that the executor's trap is waiting on, which
  could deadlock. Keep the gate off throughout §5.
- **If exclusion is pursued, start from evidence, not the hypothesis.** Audit
  the `KfRaiseIrql`, `KeSynchronizeExecution` and CLI sites that touch NV2A
  ISR or DPC state, and demonstrate an actual data race on a shared field. If
  that race exists, the atomic design is one lock per `KINTERRUPT`. The ISR
  delivery thread takes it around `fn()`, and the guest takes it on
  `KeSynchronizeExecution` and on raises to at least that DIRQL. DPC-level
  exclusion is a different mechanism: a virtual-CPU dispatch lock, or a DPC
  queue drained only when no guest thread is at DISPATCH. Both need deadlock
  review against every executor wait that depends on guest ISR or DPC
  progress.
- **Do not promote the precheck.** It is an observation tool, not a fix.

---

## 7. Summary for `PERFORMANCE_NEXT.md`

- The gate_2 mechanism is established: stale DMA GET violated the SDK's
  0x2000 fetch-bound assumption, and the late-armed action-5 slot was passed.
  The precondition appears in every run. Coalescing widened the window by
  removing the per-release drain.
- The next candidate is opt-in, cursor-trailing GET progress (stride 0x400,
  locked store, ring-only with SDK-verified bounds, never `high` or `PUT`,
  frozen on any walker or handler-side batch failure). It
  keeps the GPU-completion and semaphore-publication rules unchanged.
- Fixtures A–L come first, with the poisoning and interleaving-sweep tests as
  the core. Then the stage-0 monitor and hook, then short routes measured by
  the invariant, then barrier-mode full gates, coalesced full gates and
  performance.
- IRQ exclusion is a separate, evidence-first track. The precheck is not a
  fix.
- Candidate147 is still unresolved.

## Addendum: follow-up review (October 15)

Codex's follow-up agrees with this review's recommendations and makes three
tightening points. The sections above now include them.

1. **`0039F140`** executes `SFENCE`, writes PFB `+0x100410`, then polls it.
   The generated C drops the `SFENCE`, so guest ordering rests on the PFB
   trap's fault and `InterlockedExchange`. Qualification must verify that
   the trap is active (§2.4, stage 0). This was not re-verified here, because
   the generated sources are not in the repository.
2. **Handler-side failures.** `exec_method` can fail the batch (eight
   `BatchFail` sites, and the silent trap-flush return at
   `nv2a_pb_exec.c:3132`) without the walker knowing. GET progress must gate
   on the batch-failed state (§3, the sketch, fixture J). I confirmed this in
   source.
3. **Ring-bound provenance.** Learned and SDK bounds share
   `nv2a_pb_set_ring_bounds`. Pass provenance explicitly and publish only with
   SDK-verified bounds (§3, the sketch). I confirmed this in source.

The agreed sequence is: evidence hooks and the fetch-bound monitor, then the
deterministic interleaving and storage-reuse fixtures, then opt-in GET
progress, then runtime qualification. Neither coalescing nor IRQ gating is
ready for promotion. Candidate147 remains unresolved.

