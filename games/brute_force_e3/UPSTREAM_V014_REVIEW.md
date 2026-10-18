# Upstream v0.14.0 review for Brute Force E3

October 18 follow-up: selected ports and their validation are recorded in
`UPSTREAM_V014_INTEGRATION.md`. The review below describes the pre-port baseline.

Reviewed October 17, 2026. Upstream was fetched, including release tags.
Local runtime baseline: `1788cb4`. Release: `v0.14.0`, `eb0aa1d`
("Other Machines"). Common ancestor: `193e299`.

There are 122 upstream-only commits and 10 local-only commits. Both sides
changed 19 paths. A read-only three-way merge simulation found conflict
markers in 13 files. No merge, regeneration, runtime change, build promotion,
or game qualification was performed in the working checkout.

## Changes worth bringing over

### 1. Floating-point lifting: strongest direct source match

Upstream's floating-point series, including `c50c597`, `bb2b5c7`, `8862b6c`,
and `999a8c6`, fixes guest rounding, flag snapshots and x87 edge cases.
The existing generated Brute Force code contains:

- Eight `cvtss2si` instructions in `sub_000AD120` and `sub_000AD690`, four
  each, emitted as `(int32_t)xmm0.f[0]`. These routines combine four scaled
  components into a packed 32-bit value. The cast truncates; CVTSS2SI rounds
  according to MXCSR, nearest-even at reset. This can change packed values.
- One `fprem` in `sub_0031C570`, followed by FNSTSW/SAHF/JP back to the
  remainder instruction. Current output calls `fmod` without updating the
  x87 condition codes and tests `g_fp_cmp` for the SAHF parity branch.
  Upstream models remainder status and snapshots flags from AH. This is a
  concrete correctness candidate; no runtime occurrence or hang was proved.
- 88 scalar `rcpss` and 214 `rsqrtss` emissions. Upstream uses reciprocal
  estimate helpers instead of exact division/square root. This improves
  agreement with the guest instructions; it is not a demonstrated FPS gain.

Counts describe generated source, including potentially unreachable code,
not execution frequency. No `fcmov`, MXCSR load/store, CVTSD2SI or
COMISD/UCOMISD occurrence was found in that scan. Therefore those release
headlines are less directly relevant than the eight conversions and remainder.

The `lock xadd` flag fix (`e108d0e`) is useful shared infrastructure, but all
five Brute Force sites are followed by INC or DEC before any subsequent branch.
The direct XADD-to-JNE refcount failure reported for JSRF was not found here.

Port lifter changes together with their runtime helpers and `g_mxcsr` TLS
definition, then regenerate into a separate output directory. Compare
dispatch/ABI/coalescence changes before replacing the current generated code.
Fetching or merging Python alone cannot repair existing generated C.

### 2. Port I/O and video field status: direct match

`46aee91` adds lifting for IN/OUT and runtime port helpers. The release models
the video field pin at port `0x80C0` using the vblank count, with a fallback
before ticks begin.

Brute Force has two explicit field-pin reads, `0x003A4C7C` and `0x003A4F3D`,
both currently `RECOMP_UNIMPL("in al, dx", ...)`. The surrounding code sets DX
to `0x80C0`, extracts/inverts bit 5, and stores the result in device+`0x1D0`.
This is a better-defined field-status result and could help video timing.
It does not establish a fix for Bink artifacts or movie stalls.

Bring over the runtime API, lifter and declarations together, preserving our
timer/vblank behavior. Regeneration is required for these sites too.

### 3. Audio pacing, pitch and diagnostics

The most useful audio series is:

| Commit | Change | Brute Force relevance |
|---|---|---|
| `a16163f` | Device-queue pacing, period staging, stalled-device fallback | Our monitor already preserves/submits 256 DSP samples, but still paces by host time. Queue pacing and buffer ownership are additional improvements. |
| `148431d` | Pitch-based linear resampling | Replaces the source-per-output stub; candidate for pitch/rate accuracy and stream timing. |
| `c6ee718` | Automatic DSP doorbell discovery | Could replace launcher's fixed `0x83390810` with scratch-table-derived discovery; must verify the resolved address on this title. |
| `e0d2525` | Latch FETINTSTS once per new front-end trap | Better-defined acknowledge/rearm behavior for DirectSound interrupts. |
| `cde57b3` | Physical address resolution through allocation/page maps | Relevant to audio backing memory, but coupled to allocator changes; do not port in isolation casually. |
| `5e40efb` | Sequencer sample counter and SSL interrupt | Potential compatibility improvement; Brute Force already initializes audio, so this is not an established current blocker. |
| `a87919d`, `2d9c96c`, `ed1432d` | WAV tap, starvation/voice diagnostics, stale stream-lap tracing | Better evidence for underruns and repeated stream data. |

Preserve our master-volume mute behavior and nonzero source PCM when merging.
The audio monitor and XAudio2 paths overlap our existing fixes. Test normal
audio, mute, movie playback, gameplay and replay after integration.

### 4. Graphics and kernel improvements

`eed7f4e` makes positive-zero vertex-program output W render with W=1. This
can restore menus/overlays whose shader deliberately leaves W at zero. The
release tree's effective implementation is in `transform_vertex`; our current
function returns the interpreter output unchanged. Port consistently through
both CPU and hardware vertex paths, with zero/negative-zero/clipping coverage.
There is no evidence that this explains our current coalescing pixel artifacts.

`19ef9f0` adds fixed-function transforms/lighting, anti-aliased surfaces and a
render-backend interface. Useful future coverage, but a broad renderer change:
our custom backend already supports functionality the new generic interface
does not carry, including combiner and multiple texture-stage state.

`f775a5a` resolves guest dispatcher objects in multiple-object waits. This is
useful kernel coverage, but our established graphics lost wakeup uses a single
object; it is not a replacement for the GET fix. Thread/TLS, FATX/save and
callback-discovery improvements also deserve later integration. Preserve our
handle lifetime, save-path, LAHF carry, dispatch-size and coalescence regressions.

Most POSIX/SDL/arm64 additions do not directly improve the Windows Brute Force
performance path. Mouse/keyboard virtual-pad support is optional usability work.

## Do not replace our pushbuffer/GET path with upstream's

The final release keeps its DMA walker opt-in behind `RECOMP_PB_WALK=dma`.
Its docs explicitly describe a missing software-method interrupt that can
leave a title asleep. Our runtime already implements original ISR/DPC delivery.

Inspection of the final release tree also found differences from our audited
GET contract:

- GET is published at command boundaries using plain volatile stores.
- `pb_consume` reads an entire parameter payload without publishing GET inside
  it; a maximum payload can exceed our 0x400-byte progress bound.
- During CALL, GET is published at the subroutine address. Its own fixture
  expects this; Brute Force requires holding at the ring return address because
  the SDK fallback register is not maintained.
- The segment fallback does not carry our GET monitor, split-payload handling,
  SDK inline-record handling, failure freeze or completion/publication policy.

Retain our `1788cb4` walker and full-barrier publisher. Any adoption of the new
DMA architecture needs an explicit port of these contracts and the existing
91,344-schedule fixture, not a conflict resolution that chooses upstream wholesale.

One additional review finding: the new VSH constant trace reads
`s_const[s_const_load]` when dumping a completed constant at index >=64 without
checking index <192. The write is range-checked but that diagnostic read is not.
Guard the trace before adopting it. This finding is by source inspection, not
a reproduced game crash.

## Validation performed against the release

An unmodified `git archive` snapshot is stored under ignored
`analysis/upstream-v014-review/`. The working runtime and candidate binaries
were left intact.

- 17 unittest tests passed: MXCSR/conversion lifting and compiled helper,
  port-I/O lifting, alias-branch recovery, and the floating-point differential
  test. The latter executed 979 snippets against the x86 CPU using MSVC.
- Three standalone XADD flag checks passed.
- Built the upstream runtime and `apu_monitor_test` with MSVC Release x64.
  All three CTest modes passed: diagnostics on, unset, and explicitly off.
- The broader pytest suites were not run: pytest is absent from the installed
  Python environments. No package installation was needed for the checks above.
- No Brute Force build or gameplay test used upstream code; no performance or
  pixel-correctness improvement is claimed.

Suggested integration order: floating-point lifting/runtime and port I/O in
one separately generated candidate; audio in a second candidate; zero-W and
other renderer coverage afterward. Keep GET/coalescing qualification separate
so a change in guest timing is not mistaken for a graphics synchronization fix.

Release: https://github.com/sp00nznet/xboxrecomp/releases/tag/v0.14.0
