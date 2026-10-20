# Quiet-GPU barrier/coalesced diagnostic pair

October 20, 2026. Diagnostic measurements; no qualification or promotion.

Historical interrupted attempt. The later [quiet-GPU pair](QUIET_GPU_170.md)
completed after the fixed profiling helper was installed. The rejected data
below is not part of its combat results.

**Paused at the user's request before completing a valid pair.** The first
barrier capture (`nsight165_quietbarrier`) is rejected: its pre-trace snapshot
has campaign pointer zero and the title remained in the menu path. The automatic
coalesced arm was cancelled. The replacement run (`nsight165_quietfixbarrier`)
was stopped during mission introduction/briefing; no replacement capture was
armed. Windows administrator consent for the replacement collector was cancelled.
All runs and raw files are retained; there are no new combat results here.

The initial flip-only trigger was insufficient. On resumption, require a reviewed
completed HUD frame plus stable active-campaign state before arming either arm.
Read-only `quiet167-frame.py` successfully captures the presenter's completed
`s_present` copy using PDB symbols, without desktop control or runtime dumps.
Completed-frame checks showed the fixed menu schedule had not finished profile
and campaign selection. Virtual-controller live inputs advanced the replacement
through existing profile DEFAULT, campaign QUICK008, Standard difficulty, the
first mission, Tex selection and Start, then introduction/briefing. That is route
preparation evidence, not a verified HUD/combat gate.

The user closed GPU-heavy applications before this pair. A process inventory
confirmed Penumbra was no longer running. No application was suspended or
terminated by the profiling helpers. Both arms use unchanged candidate163,
GET progress at its qualified default, normal barriers or the existing
coalescing switch respectively, with bulk readback, texture epochs, GET
monitoring, IRQ gating, custom sampler, surface dumps and WAV tap off.

The existing route drives controller inputs by guest flip. The collector arms
after the native flip receipt reaches 29,900 and records a read-only guest-state
snapshot. This retry uses the previously reviewed route as its visual reference;
the failed first attempt had no new frame capture. Flip-indexed input does not guarantee identical
scene state or controller polling receipts throughout two wall-clock windows.

One elevated, hidden CLI helper records a three-second idle physical-GPU
preflight, then the barrier and coalesced combat windows sequentially. Each
combat window uses CPU-only WPR and 30 seconds of non-injecting Nsight WDDM
events plus physical NVIDIA metrics at 1 kHz. D3D11 injection and HAGS settings
remain untouched. Both recordings finish before any SQLite or WPA export starts.
The analysis helper explicitly gates exports on successful completion of both
arms. Raw ETLs include Nsight postprocessing tails; analysis must crop them to
the central Nsight [1, 29] second interval using session UTC epochs.

## Interpretation boundaries

Readback Map establishes availability of the requested staging resource. The
previous global activity measurements do not prove that it drains a single
shared queue containing every application's work. Other GPU contexts can delay
our work through scheduling/resource contention, but packet counts neither
measure GPU-time shares nor show which context delayed a particular copy.

Physical graphics-engine activity measures global busy time, not our process's
GPU utilization or shader saturation. The idle preflight and process inventory
provide evidence about background load, not an exclusive reservation of the
GPU. WDDM coverage and driver-symbol limitations still apply. CPU sample shares
describe on-CPU work, including active polling, rather than blocked duration.
Trace overhead and differing scene progression limit performance conclusions.

Bulk readback remains excluded from subsequent promotion work: the earlier pair
did not establish a saving. It also did not prove memory bandwidth was the
limiting cause. Caching launch flags remains in candidate163; promotion into the
named prototype requires its normal gameplay gate. No such promotion is part
of this measurement pair.

The [target residency draft](TARGET_RESIDENCY_DESIGN.md) starts with retained
content under eager coherence. Deferred readbacks need a separate consumer and
alias-coverage argument. Texture epochs qualification remains the next bounded
experiment, with hash lookup and coalescing held independent.
