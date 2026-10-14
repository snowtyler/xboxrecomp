# Response to ANSWERS.md

Reviewed at `e6f4988`. This reads `ANSWERS.md` against source; nothing was run.
Labels: **[M]** measured (as reported), **[S]** source, **[E]** estimate,
**[H]** hypothesis.

## What the new evidence settles

1. **Release barriers are now the dominant cost, measured directly.**
   - [M] In epoch-on runs, the `release` sync reason costs 19.36 of 41.5 ms per
     frame (47 %).
   - It does 60.2 of the 66.6 color readbacks per frame.
   - There are 66.9 releases spread over 3.58 segments, about 18.7 releases per
     segment.
   - Traps cost 0.23 ms per frame. Target, flip and trap syncs are almost free
     today, because a release has usually just synced.

   [E] A working coalescing change is therefore the largest single lever. It
   would cut color readbacks from ~60 to ~10 per frame: segment ends, ~6 target
   transitions that would become real readbacks, depth, and clears. That frees
   an estimated 10–15 ms per frame. Part of the GPU drain reappears at the
   remaining sync points.
2. **The candidate147 stall is a lost wakeup, not a dropped batch or an executor
   deadlock.**
   - [M] The executor is in its `Sleep(0)` poll and the timer thread is in its
     ordinary wait.
   - [M] IRQL is 0. No failure counters, no `[PB ERROR]`, no invalid ranges.
   - [M] The main thread waits on device+0x196C. Only the software-trap action-5
     path (`0x003A5589`, ordinal 145 `KeSetEvent`) signals it.
   - [M] The semaphore slot reads 0xFDC7 while the inserted serial is 0xFDC9.

   The previous hypotheses of a stuck notify state machine or a wedged executor
   don't fit these samples.
3. **Combat is lighter than the paused view on every counter.** [M] It has
   640–820 draws, 40–50 readbacks and 20–23 MB of vertex data per frame, against
   1,311 draws, 66 readbacks and 32.6 MB. The paused beach is a conservative
   proxy for draw cost. Frame time in combat still has to be measured directly.
4. **CPU writes to scanout backing through the tiled alias are confirmed**
   (movie converter → 0xF02B0000). Keep the three-way merge in the retention
   design, and keep tiled-alias canonicalization in every guard.

## Highest-priority hypothesis: NV2A interrupts ignore guest interrupt masking

[S] `kernel_raise_interrupt` (`kernel_bridge.c:2251`) calls the guest ISR
directly. Neither `kernel_nv2a_notify_tick` nor `kernel_vblank_tick` checks
`xbox_IrqlBlocksInterrupts()`. The APU (`apu_core.c:146`) and OHCI
(`ohci.c:1197`) do check it, and hold the interrupt off until no thread is at
DISPATCH or above. The `kernel_hal.c:64–78` comment describes exactly the race
that this check prevents.

[S] `cli` and `sti` don't appear to be modelled: they are listed only as
flag-preserving mnemonics (`lifter.py:430`). A guest `cli … sti` critical
section therefore doesn't exclude an ISR that runs on the timer thread.

[H] **Mechanism.** `sub_0039EC10` emits NOP action 5, kicks off, then waits on
+0x196C. If the guest's clear-check-wait sequence relies on raised IRQL or
`cli` to keep the action-5 ISR out, the ISR can run inside that window on our
timer thread. The guest then either clears the event after it was set, or
checks state the ISR is still changing. Either way the wakeup is lost.
Release coalescing removes ~0.3 ms readbacks ahead of the trap, so the trap
reaches the ISR much sooner after the kick. That explains why barrier mode
rarely hits the race and coalescing does.

This fits every item in the snapshot:

- the executor is idle because it already processed the trap;
- the timer thread waits normally because the ISR already ran;
- IRQL is 0 by the time anyone looks;
- the event is unsignalled.

It is still a hypothesis until the timeline below confirms it.

**Checks, cheapest first:**

1. **Disassemble `sub_0039EC10` around `0x0039ED20` and the action-5 handler.**
   Look for `cli`/`sti`, `pushfd`/`popfd`, `KfRaiseIrql`/`KeRaiseIrqlToDpcLevel`,
   `KeSynchronizeExecution`, `KeClearEvent`/`KeResetEvent` (ordinals for the
   reset path), and the order of these operations:
   - write the waited serial or a "waiting" flag;
   - emit NOP 5;
   - kick off (PUT write);
   - clear the event;
   - recheck the slot;
   - call `KeWaitForSingleObject`.

   Also check whether the action-5 handler compares the slot with a stored
   serial before calling `KeSetEvent`.
2. **Ordered timeline.** Add these events to a ring trace that survives a stall,
   with QPC timestamps and thread ids:
   - guest `KeSetEvent`, `KeClearEvent`/`KeResetEvent` and
     `KeWaitForSingleObject` entry and exit on 0x003AAC9C;
   - `KfRaiseIrql`/`KfLowerIrql` transitions;
   - executor trap start, flush, `SoftwareNotify` return;
   - the `pending` state transitions and `kernel_raise_interrupt` calls;
   - PUT writes as observed by the acknowledgement thread;
   - the slot value at each publish.

   Two orders identify the lost wakeup:
   - `KeSetEvent` before the guest's clear-or-wait;
   - the ISR running while the guest thread is raised.
3. **Discriminating fix experiments.** Run each with coalescing on, everything
   else off, at least three title-screen runs:
   - **Arm A.** Deliver NV2A notify and vblank interrupts only when
     `xbox_IrqlBlocksInterrupts()` is false. The interrupt is level-triggered,
     so leave the notify pending and retry next tick, as OHCI does. Keep the
     executor's existing completion and publication order unchanged.
   - **Arm B.** Additionally make lifted `cli`/`sti` (and `popfd` IF) take part
     in the same gate, as a per-thread raise/lower. Only needed if step 1
     shows the guest uses them.

   If A or B fixes the stall with no other change, coalescing can proceed. This
   also hardens barrier mode, where the race is merely less likely.
4. **Rule out an unsubmitted fence.** Record GET/PUT. Also check whether the
   releases for 0xFDC8 and 0xFDC9 and NOP 5 lie before or after PUT in ring
   memory. Scan from the last segment end to the guest's write pointer
   (device+0x24/0x28 ring bounds are bound already).
   - If they are past PUT, the guest never kicked them off. This is a different
     bug, an SDK kickoff condition, and it needs its own trace.
   - If they are at or before PUT and were executed, the slot should be at least
     0xFDC9 unless publication was lost. That reopens the batch-failure path,
     with no counters emitted after the last flip.

## Smaller notes

- **Add a counter for each `FenceBatchEnd(commit=0)` and each
  `FenceBatchFlush()` failure,** written out immediately rather than at flips.
  `ANSWERS.md` correctly notes that a failure after the last flip packet would
  not have been reported.
- **The `idle` reason** costs 32 calls and 0.79 ms per frame, including 3.8
  color readbacks. Keep `WAIT_FOR_IDLE` synchronous for now. Revisit it after
  coalescing.
- **Target retention becomes relevant only once coalescing works.** Today's
  target transitions are no-ops because a release just synced. After
  coalescing, the four-target chain (160×120 / 320×240 / 512×512 and 640×480
  three times) becomes ~6 real readbacks plus uploads per frame.
- **Measure vblank before anything else that touches pacing.** Record per-tick
  QPC timestamps and a claimed count against guest flips over 10 s. The
  `GetTickCount64() + 16` scheduling remains a likely sub-60 Hz source.
- **Add the zeta offset/eviction trace and the SDK-marker/action split for traps
  as planned.** At 3 traps per frame and 0.23 ms per frame, traps are not a
  performance target, only a correctness one.

## Revised order

1. Disassembly check (step 1), then the IRQ-gating arms (A/B) with the ring
   timeline, under coalescing.
2. If fixed: qualify interrupt gating on its own in barrier mode through the full
   gates, then coalescing on top. Do this before any further epoch, retention or
   residency work, because it is worth more than all of them combined [E].
3. Vblank interval measurement and unique-frame accounting.
4. CPU phase timing plus ETW on the coalesced build: per-draw state,
   gather/pack, upload.
5. Retention with merge for the target chain, then the observe-mode/residency
   decision.
