# Candidate153 qualification

Candidate153 uses the reviewed v0.14 integration at `3c29dd9`, with optional
raw XAudio2 device telemetry and per-initialization audio counter reset.
The GET walker, GPU completion policy and launcher defaults are unchanged.

## Required order

1. Repeat five short-route control/progress pairs on this executable.
2. Establish monitor sensitivity with a normal-barrier GET-off control.
   Then qualify normal-barrier GET-on gameplay, death and replay.
3. Enable GET progress by default only after those gates pass. Keep the
   monitor available independently. Coalescing and IRQ gating stay opt-in.

Every GET-on qualification run must have `RECOMP_NV2A_GET_MONITOR=1`, maximum
lag at most `0x3FC`, zero far reads, invalid/untrusted reads, freezes,
backward jumps and in-ring CALLs. Require actual publications, active PFB
trapping, no parser/handler/fence failures and visual proof of route progress.
The GET-off barrier control must show far reads. It need not stall: its
purpose is to demonstrate the hazard and the monitor's sensitivity.

Commands, run separately with fresh names:

```powershell
games/brute_force_e3/verify_get_progress.ps1 -Name perf153_get_title -Executable brute_force_e3_perf153.exe -Pairs 5
games/brute_force_e3/verify_get_progress.ps1 -Name perf153_get_barrier_first -Executable brute_force_e3_perf153.exe -Pairs 1 -Barrier
games/brute_force_e3/verify_upstream_v014.ps1 -Name perf153_barrier_replay -Executable brute_force_e3_perf153.exe -Seconds 1500 -Replay -GetProgress -GetMonitor
```

The full trace analyzer remains required on short routes. Long routes use
`--monitor-only` to avoid treating an overflowing ordered trace as evidence.
That mode explicitly does not establish SDK-wait trace coverage. Its pass
only establishes the monitored invariants; death/replay still require
review of completed-frame captures and guest state. Scripted button presses
or a watchdog exit do not establish that death or replay occurred.

## Gameplay artifacts

Movie artifacts on candidate152 occurred with barriers, but movie frames
come through the CPU frame path. They do not answer whether coalescing adds
artifacts to rendered gameplay.

Capture the same gameplay route with GET progress and its monitor enabled
in both arms. Use `verify_upstream_v014.ps1 -GetProgress -GetMonitor`, adding
`-ReleaseCoalesce` for the second arm. Compare completed frames with matching
scene, camera, animation and HUD state; matching wall-clock capture windows
alone are insufficient because the arms can progress at different speeds.
Keep unmatched captures inconclusive and retain the raw sequences.

If rendered artifacts occur only with coalescing, audit input acquisition,
starting with `nv2a_hw_acquire_input` and GPU-written input outside the current
colour/depth targets. If they are identical in both arms, track them as a
rendering bug while still requiring coalescing's own correctness gates.

## Audio measurement

The deterministic XAudio2 fixture now checks a 100 ms producer stall after
queue exhaustion: one dry spell and exactly 4,800 missing playback frames.
It also checks an instantaneous empty/refill observation: one dry spell but
zero missing playback frames. Thus an underrun count alone cannot establish
an audible gap or its duration. These are model tests, not physical-device
audio validation.

`RECOMP_AUDIO_DEVICE_TRACE=1` logs once per second when the producer polls:
QPC timestamp and frequency, XAudio2 `SamplesPlayed`, buffers queued, source
frames successfully submitted to XAudio2, staged frames and cumulative dry
spells. The source count excludes rejected submissions and the partially
filled staging buffer. At 48 kHz, compare `delta(QPC)/frequency` against
`delta(SamplesPlayed)/48000`; the difference includes playback loss, device
clock drift, intentional pauses and device accounting granularity. Use the
existing host starvation/trap accounting to distinguish these cases.

```powershell
games/brute_force_e3/verify_upstream_v014.ps1 -Name perf153_audio_barrier -Executable brute_force_e3_perf153.exe -Seconds 600 -GetProgress -GetMonitor -AudioEvidence
```

`-AudioEvidence` enables raw device telemetry, `RECOMP_AUDIO_HOST` and a WAV
tap. Mute only changes master volume and can be added for a second arm. WAV
samples are submissions, so a clean WAV cannot exclude device-inserted
silence. Watchdog termination can leave the WAV unfinalized; inspect its
header and actual data before analyzing it. GET qualification and graphics
comparison should also have separate runs without this audio instrumentation.

## Results

Candidate153 built successfully with optimization and GET trace hooks.
Eight integration fixtures and six Python GET analyzer/hook tests passed.
The launcher environment regression also passed after the probes completed.

Executable SHA256:
`1fdb02d85f37fb82dee51f0eb3a050451ae7066940e7620803bd12a7b8978162`.
Candidate152 remains unchanged.

The first normal-barrier short-route pair is complete. Both arms used normal
audio, the GET monitor and full ordered SDK-wait tracing, with coalescing,
IRQ gating and GPU profiling off. PFB trapping was verified active in both.

| Arm | Duration | Ring reads | Far reads | Max lag | Publications |
| --- | ---: | ---: | ---: | ---: | ---: |
| GET off | 180.28 s | 25,772,956 | 15,455,778 | `0xCF74` | 0 |
| GET on | 180.24 s | 25,296,577 | 0 | `0x3FC` | 93,152 |

Both had zero invalid/untrusted reads, freezes, backward jumps, in-ring CALLs
and parser/fence failures. Neither stalled; the control establishes stale GET
and monitor sensitivity, not a barrier-mode lost-wakeup reproduction. The
progress arm had zero waits after reserved-slot fetches and completed 32,651
SDK waits. Both exited through their normal watchdog, without forced timeout
or cancellation.

Completed-frame captures around 162 seconds were reviewed:
`analysis/perf153_get_barrier_first_control_1-frame-00385.bmp` and
`analysis/perf153_get_barrier_first_progress_1-frame-00384.bmp`.
Both show advancing mission briefing text with existing movie pixel artifacts.
This is short-route evidence only; neither capture is rendered HUD gameplay.

Machine-readable run summaries, exact child configurations and source/binary
hashes are in `analysis/perf153-qualification-verification.json`; raw evidence
uses the `perf153_get_barrier_first_` prefix. The workflow state records the
manual capture review separately from its automated GET checks.

Still pending: the five coalesced short-route pairs on candidate153, full
monitored barrier gameplay/death/replay, matched rendered-gameplay captures,
and a gameplay WAV/device-timing measurement. GET progress remains opt-in.
