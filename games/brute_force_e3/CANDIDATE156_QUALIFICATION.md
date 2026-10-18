# Direct wait and guest-state qualification

Candidate156 includes the v0.14 integration and the qualification refinements
below. Its full normal-barrier gameplay/death/replay gate passed. Candidate157
then promoted GET progress to the default; see `GET_DEFAULT_PROMOTION.md`.
The monitor, coalescing and IRQ gate remain opt-in in the application.

Executable SHA256:
`cf037b7998b9a98526df1aae1f6d3d72f2309c7a06fdb539ed21a18fc2be3e1c`.

## Long-run wait check

`[PB WAIT]` reports post-arm checks, event-wait decisions and event decisions
after the walker consumed the final reserved dword. The observer uses a bounded
64-entry receipt table indexed exactly like the SDK, `(serial >> 1) & 63`.
Each receipt binds the selected record's full serial and release-command
address. The walker publishes it after the actual final-word load, including
after a segment ends. A cursor alone would confuse previous laps or unrelated
subroutine reads with the selected release.

The post-arm hook does constant work and produces no ordered-trace traffic.
Only release and reserved-final-word observations update the atomic receipts;
there is no additional atomic operation on every command word. Qualification
requires observer activity and zero `read_past` with GET on. Correct polling
after consumption is allowed; this counter concerns the event-wait path.
`inspect_guest.py` also reads the counters from `g_nv2a_wait_stats`, and the
long-route analyzer checks both reports and the process snapshot.

The deterministic fixture checks this observer across the existing 91,344
schedules, plus wrong addresses, recycled serial slots, completed segments
and poll/event decisions. Its positive control works in both completion modes.
A live coalesced GET-off control, `perf154_direct_coalesced_control_1`, recorded
one direct violation, exactly matching one ordered-trace old-placeholder
fetch/wait and a graphics-event stall. A live barrier-mode nonzero receipt
has not yet been observed; the earlier barrier control established stale GET.

Candidate154's progress arm exited with code 0 after 67.67 seconds, before
its scheduled inspection. It is excluded from qualification; its cause is
unresolved. Preserve its artifacts rather than treating an early exit as a pass.

## Explicit rollback

Progress and monitor values now parse `0` and `1`; absent, empty or unknown
values use their declared defaults. Candidate156 used default zero; candidate157
uses default one after the barrier gate passed.
`RECOMP_NV2A_GET_PROGRESS=0` remains off even when its default becomes one.
Control runners set zero explicitly rather than removing the variable.

The launcher supports both an inherited explicit zero and `-DisableGetProgress`,
restores the caller's environment and keeps the monitor off. Its regression
tests these cases, including an ambient one overridden by the rollback option.

## Guest death and replay evidence

See `GUEST_STATE_EVIDENCE.md` for original instruction addresses, object guards,
source/carrier hashes, REA Evidence IDs and limitations. Snapshot observations
include the recognized campaign's failure flag, world/interface pointers,
pending transition requests and guest SDK swap serial. Reads cover ordinary
and contiguous guest RAM and reject unrecognized object layouts.

Replay qualification additionally enables read-only guest checkpoints:

- `0x00111D43`: the Squad Killed predicate has already passed its mission-state,
  network and object-class checks; observe the remaining failure bit.
- `0x0008468F`: Replay Mission prepares its descriptor. This alone is insufficient.
- `0x0010B524`: the transition UI enqueues the stored descriptor.
- `0x0008A38E`: the transition consumer selects the descriptor.
- `0x0008A4CF`: the mission reload routine returns.

`validate_guest_route.py` requires a clear-to-set failure flag, the positive
full death predicate, preparation followed by accepted/consumed matching
requests, reload return, then a recognized campaign with the flag clear and
advancing swaps. Copied descriptors can match by interface and payload, so
pointer replacement is neither necessary nor sufficient. Matching flags alone,
opening a menu or replacing a world pointer cannot pass. The source XBE hash
and the shared QPC timebase must also match. Completed HUD/movement/firing
captures remain a separate required review.

`-Replay` enables quarter-second read-only sampling and these checkpoints.
The gate fails closed if the evidence is missing. Static mappings and reader
smoke tests do not establish an actual death/replay transition.

## Flip-indexed comparison

`RECOMP_PAD_SCRIPT` accepts `f<flip>:<buttons>:<hold flips>` alongside its
existing millisecond syntax. Ordinals are absolute completed NV2A guest flips;
they are distinct from the guest SDK swap serial. The rendering thread
publishes the clock after successful completion. The optional exclusive mode
removes host input from a comparison and logs actual activation/release flips.

`RECOMP_FB_DUMP_GUEST_FLIPS=N,N-M` captures exact completed ordinals to named
`guest-flip-XXXXXXXXXX.bmp` files. The validator requires exact input receipts,
complete BMPs and every requested capture. It rejects malformed/wrapping
schedules before launch. Equal flip ordinals improve alignment but do not fix
wall-clock simulation or RNG: compare camera, animation and HUD state too.

Both `perf155_barrier_flip_state` and final `perf156_barrier_flip_state` fired
all five menu steps at their requested flips and wrote all five requested
captures. Candidate155 exposed a native checkpoint pointer-guard limitation
to low RAM; candidate156 fixes it to include contiguous objects. Candidate155
must not qualify the corrected native replay hooks.

Candidate156 ran 180.15 seconds: 32,802,143 ring reads, zero far/invalid/untrusted
reads, maximum lag `0x3FC`, 121,456 publications, zero freezes/backward jumps/
in-ring CALLs. Its direct observer saw 3,684 post-arm event decisions and zero
read-past decisions. PFB trapping was verified active. The read-only state
observer collected 718 samples without read errors; every code signature matched.
Captured flips 7,500 and 9,600 show intro/briefing surfaces with existing movie
artifacts, not rendered HUD gameplay. This is tooling/short-route evidence.

## Independent remaining gates

The coalesced short pairs and the normal-barrier gameplay/death/replay gate
have no dependency on one another. The full barrier gate passed and GET is
now the default in candidate157; coalescing has separate correctness and rendering gates. The
single-host probe lock remains in place to protect shared GPU/input/save state.
The remaining jobs are prepared independently; performance measurements must
not overlap other game runs.

```powershell
games/brute_force_e3/verify_get_progress.ps1 -Name perf156_get_title -Executable brute_force_e3_perf156.exe -Pairs 5
games/brute_force_e3/verify_upstream_v014.ps1 -Name perf156_barrier_replay_state -Executable brute_force_e3_perf156.exe -Seconds 1500 -Replay -GetProgress -GetMonitor
```

The barrier replay gate completed in 1500.49 seconds with 4,127,051,976
ring reads, zero far/invalid/untrusted reads, max lag `0x3FC`, 16,132,590
publications, and zero freezes/backward jumps/in-ring CALLs. All 36,944
post-arm event decisions had zero read-past violations. The state observer
collected 5,987 samples without read errors. Death, replay request lineage,
reload and resumed gameplay were verified; details are in `GET_DEFAULT_PROMOTION.md`.

A matched rendered-gameplay comparison, the five coalesced pairs, and gameplay
profiling/audio measurement remain pending. Audio analysis should use long QPC/SamplesPlayed windows and
estimate clock drift from fed intervals; do not convert every short-window
sample deficit or dry-queue count into a playback gap.
