# First combat profile on the GET default

October 18, 2026. Release synchronization remains the main measured performance
lead. In the representative 500–530 s gameplay window it took 38.38 ms per
completed flip, about 58% of the 66.09 ms mean frame interval. Poll-path CPU
cost did not dominate combat: there were no completed poll waits in this window.
This supports continuing coalescing investigation, without qualifying its
rendering or predicting an FPS gain.

## Run and qualification boundary

`perf158_combat_profile` ran 640.29 s, normal barriers and normal audio, GET
default on, monitor on, IRQ gate/coalescing/ordered trace/GPU timing queries
off. CPU renderer timing, cumulative counters, state observation, WAV/device
logging and the CPU sampler were enabled. It exited at watchdog 3 without a
forced stop or timeout. Candidate158 EXE SHA-256:
`fff2c7fe19a96de3e10b9f35bbd48aadbc976580894c560569aa66e6ebd1a76b`.
The named prototype remains candidate157; this diagnostic build is not promoted.

GET observed 858,804,709 reads, max lag `0x3FC`, zero far/invalid/untrusted reads,
3,350,250 publications, and zero freezes/backward jumps/in-ring CALLs. All
10,239 post-arm event decisions had zero read-past violations. PFB trapping was
verified active. These are diagnostic-run invariants, not a coalescing gate.

The 470 s capture window confirms rendered HUD gameplay. Periodic captures
`frame-00365.bmp` show HUD/ammo 48, and `frame-00395.bmp` shows changed position,
ammo 24, incoming fire and damage. The long turn at 560 s is followed by close
rock views without HUD, including completed captures near 590 and 630 s.
Consequently the later three timing windows are retained in raw JSON but
excluded from the representative combat conclusion. The future profile route
shortens this turn and the later forward movement; that revised route still
needs visual validation. Do not use the close-rock views for a matched
coalescing comparison.

## Wait and executor attribution

Correction from the candidate159 sanity check: the short-phase CPU values
originally summed from `GetThreadTimes` are withdrawn. Its coarse accounting
can overcount repeated sub-millisecond phases; one sync window even exceeded
its wall time. Wall times/counts and the long-window executor/main-thread CPU
measurements remain valid. Candidate160 uses scheduled cycle counts for short
phases, without converting cycles to elapsed time. See
[Microsoft's cycle-counter guidance](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime).

The new opt-in `RECOMP_BRUTE_FORCE_WAIT_PROFILE=1` records per-thread cumulative
QPC wall time and `GetThreadTimes` CPU time at poll/event entry and wait return.
There is no hook inside a busy-loop iteration and no ordered-trace traffic.
An event retry retains its initial start. Completed waits only are counted;
window boundaries may shift a wait's attribution. CPU clocks have scheduler
accounting granularity. Profile-disabled paths do not query clocks.

For 500–530 s, whole QPC report intervals measured:

| Observation | Result |
| --- | ---: |
| Executor interval | 28.550 s / 432 completed flips |
| Completed flip rate / mean interval | 15.13 FPS / 66.09 ms |
| Release sync | 16.580 s / 38.38 ms per flip |
| All sync reasons | 19.740 s / 45.69 ms per flip |
| Executor CPU | 26.563 s / 61.49 ms per flip |
| Guest wait observation interval | 26.370 s |
| Event stage | 388 waits / 24.142 s wall; short-phase CPU withdrawn |
| Poll stage | 0 waits / 0 wall |

Guest wait and executor synchronization overlap on different threads. Never add
their wall times. Event wall time occupies about 91.6% of the guest observation
interval. The native renderer interval uses QPC counter boundaries; the existing
host-observed report analyzer independently reports 14.91 FPS over its slightly
different complete intervals. These are means, not frame-time percentiles.

The separate 540–560 s CPU sample covered 20.007 s during firing. Main guest
thread 23988 consumed 1.922 s CPU, with 2,115 instruction observations
at `NtWaitForSingleObject`; its saved stack candidates include the SDK wait and
kernel wait bridge. Executor thread 41244 consumed 19.156 s CPU. Its instruction
observations include driver waits, readback mapping, memory comparisons, target
work and vertex fetching. Suspended instruction samples include blocked threads
and are not CPU-weighted. CPU interval deltas, rather than lifetime totals, are
now saved per thread. The timing windows exclude this sampler and requested
bulk captures. Periodic surface dumps and other diagnostic overhead remain;
these are not clean production performance measurements.

Across the entire run, the final completed-wait report has 12.881 s poll wall
and 272.816 s event wall. Stored short-phase CPU sums are not interpreted.
Retain the poll/event wall split in future profiles, especially coalesced runs
where publication timing can change it.

## Replay gap

The candidate156 replay preparation-to-enqueue interval is 229.077974 s. The
original files reopened during this interval have these encoded durations:

| File | Seconds |
| --- | ---: |
| `Campaign_Intro.bik` | 48.348348 |
| `Tex_Intro_Final.bik` | 47.914581 |
| `MB01.bik` | 132.732733 |
| Total | 228.995662 |

The excess is only 82 ms. This strongly supports ordinary intro playback, not
a 230 s loading stall. Original file hashes and ffprobe output are archived in
`analysis/replay-intro-duration-evidence.json`. No original-hardware or
unmodified-emulator runtime comparison was available; the known DirectSound
movie stall is not thereby resolved.

## WAV and device pacing

The backend fixture passed a known 100 ms producer stall: one dry spell and
exactly 4,800 missing frames. It also demonstrates an instantaneous refill
where the counter increments with zero missing playback time. Counts alone
are insufficient.

Gameplay offered no ten-second continuously fed span, so its clock drift is
unidentifiable from that trace alone. After the game exited, a separate 65 s
probe fed silent PCM continuously through the real backend/default device,
with the same 512-frame/four-buffer configuration. It recorded zero underruns.
Over 63.722 s its measured rate was 47,998.844 frames/s, an apparent -24 ppm
offset. Endpoint processing quantization remains: a conservative two-buffer
endpoint uncertainty is about 335 ppm, so this is a scale estimate, not a
precise physical clock calibration.

In the gameplay 480–540 s long window, the device played about 42,267 frames/s;
submission rate was about 42,268. Using the fed-probe rate, about 7.0 s of
58.6 s wall time had no corresponding source-frame progress. Even a 1,000 ppm
rate error would account for only 59 ms here. This supports actual device
starvation in this diagnostic run, rather than treating every empty-queue
observation as harmless or every deficit as clock drift. It does not identify
whether scheduling, pacing or production is responsible, and diagnostic I/O
overhead remains a confounder.

The WAV has 27,914,240 stereo 16-bit frames at 48 kHz (581.547 s of submitted
audio). One-second source excerpts mapped near host 500, 535 and 550 s contain
active PCM, with peaks 4,981 / 13,168 / 8,767. A submission WAV omits silence
inserted by an empty device queue; it cannot alone prove audible continuity.
No listening-quality pass is claimed.

## Reproduction and next work

```powershell
games/brute_force_e3/verify_upstream_v014.ps1 -Name <fresh-name> -Executable brute_force_e3_perf158.exe -Seconds 640 -CombatProfile -UseDefaultGetProgress -AudioEvidence -GuestStateEvidence
```

After gameplay, run the explicitly invoked `tests/xaudio2_clock` hardware probe
separately. It is excluded from CTest. Analyze with `analyze_combat_profile.py`
and optional `--clock-log`. Raw prefixes, hashes and reviewed captures are
recorded in `analysis/perf158-combat-verification.json`.

Checks passed: 31 Python tests, two MSVC tests of the actual wait hooks with
independent wall/CPU clocks, the XAudio2 backend fixture, launcher environment
regression and script syntax checks. The qualified default is unchanged.

Next are the five coalesced short pairs (direct counter on in both arms), then
a validated flip-indexed rendered-gameplay comparison. Keep the audio pacing
investigation separate. All renderer correctness gates still apply; no IRQ
gate or asynchronous semaphore publication change is included here.
