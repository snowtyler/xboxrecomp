# Qualified GET progress default

October 18, 2026. GET progress now defaults on independently of release
coalescing. The application monitor, coalescing and IRQ gate remain off by
default. Roll back with `RECOMP_NV2A_GET_PROGRESS=0` or `play.ps1
-DisableGetProgress`; empty values use the declared default rather than
enabling a switch through variable presence.

## Qualification chain

Candidate156 contains the reviewed v0.14 integration and qualification hooks
at source commit `0b3be51`. Its 1,500-second normal-barrier gate used explicit
GET progress and monitoring, with ordered tracing, coalescing and IRQ gating
off. Candidate157 then changed the default, added a native policy report and
made flip-script decimal parsing consistent with the validator. The GET walker
and GPU completion/publication policy are unchanged between these candidates.

| Run | Duration | Ring reads | Far reads | Max lag | Direct event decisions / read-past |
| --- | ---: | ---: | ---: | --- | ---: |
| `perf156_barrier_replay_state` | 1500.49 s | 4,127,051,976 | 0 | `0x3FC` | 36,944 / 0 |
| `perf157_default_on` | 160.24 s | 23,480,976 | 0 | `0x3FC` | 2,506 / 0 |
| `perf157_explicit_off` | 160.24 s | 23,723,677 | 14,679,291 | `0xCF8C` | 3,411 / 0 |

All three reached their watchdog normally (exit 3), without forced stop or
timeout. Invalid/untrusted reads, freezes, backward jumps and in-ring CALLs
were zero. The full gate published GET 16,132,590 times. PFB trapping was
verified active. Candidate157's native report confirms progress=1 with the
environment variable absent, and progress=0 with literal `0`, both with default=1.
The off arm made no progress publications and confirms the monitor's sensitivity
in barrier mode. A live barrier read-past positive has not been observed;
the deterministic fixture and the archived coalesced control establish that
observer's positive response. See `CANDIDATE156_QUALIFICATION.md`.

## Death, replay and controls

The full gate collected 5,987 guarded guest-state samples with no read errors.
It observed campaign 22917504's failure flag clear then set, and the complete
Squad Killed predicate returning true. Replay preparation at `0x8468F` was
followed by a matching flags-6 UI enqueue at `0x10B524`, consumption at
`0x8A38E`, and reload return at `0x8A4CF`. Campaign 23016704 then had a clear
failure flag and advancing SDK swap serials. The state validator passed.

Completed-frame review supplies the separate visual/control requirement.
These files are under `analysis/`, prefixed `perf156_barrier_replay_state-`:

| Frame file | Approximate elapsed time | Reviewed evidence |
| --- | ---: | --- |
| `frame-00342.bmp` | 471 s | HUD/basic controls, ammo 48 |
| `frame-00402.bmp` | 551 s | Movement/camera change, ammo 22 |
| `frame-00446.bmp` | 591 s | Movement, turning and combat damage |
| `frame-00556.bmp` | 751 s | SQUAD KILLED and Replay option |
| `frame-00707.bmp` | 941 s | Restart confirmation |
| `frame-00932.bmp` | 1251 s | Resumed HUD, full health and ammo 48 |
| `frame-00992.bmp` | 1321 s | Post-replay movement/firing/damage, ammo 27 |

Preparation occurred around 942.5 seconds, enqueue around 1171.6 seconds and
reload return around 1173.7 seconds. The intervening intro/loading UI explains
why enqueue must not be called the user's confirmation click. Another Squad
Killed capture near 1451 seconds follows the already-proved resumed gameplay.

## Selected local artifacts and checks

SHA-256 identities:

- Source XBE: `bcf4fbfb1adc595a3d04fe1443f22fcc1ec61bee4909f2780783d6ba0a13c4de`.
- Candidate156 EXE: `cf037b7998b9a98526df1aae1f6d3d72f2309c7a06fdb539ed21a18fc2be3e1c`.
- Candidate157/named prototype EXE: `27208192a7c5847f5b1a005fec1c2d4772c986a6e1b9050c35f86ccf64c13825`.
- Candidate157/named prototype PDB: `894676031cec88006394266920fbfb0114ba62c6590b63c1d11f7c0828c1ed5e`.

The named `brute_force_e3_prototype.exe/.pdb` are copies of candidate157.
The previous named artifacts are preserved, with hashes, in
`analysis/prototype-before-get-default-20261018/`. The owned-window launcher
test `test-launcher157-default-final` passed visible-window creation, closure,
child exit and launcher exit 0. This smoke is not a second 25-minute gate.

Final checks: 26 Python tests; five pushbuffer/GET tests including the 91,344
schedule fixture; one native flip/control/config test; four native semaphore
ordering tests; and the launcher environment regression. Raw run metadata,
monitor summaries, guest route evidence and captures remain in ignored
`analysis/`. `get-default-promotion-verification.json` records their hashes and
the manual review. Binaries/assets remain local and are not committed.

## Remaining work

Complete the five coalesced short pairs and compare rendered gameplay with
GET on in both modes, using flip-indexed controls/captures and scene checks.
Movie artifacts cannot qualify rendered coalescing. Candidate154's early-exit
progress arm remains excluded. Combat profiling and WAV/device timing are
independent next tasks; use long timing windows and estimate device drift.
This promotion claims correctness evidence for GET progress, not improved FPS,
audio quality, coalescing qualification or a resolution of candidate147.
