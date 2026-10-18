# Guest state for death/replay qualification

These are static mappings for the E3 XBE, SHA-256
`bcf4fbfb1adc595a3d04fe1443f22fcc1ec61bee4909f2780783d6ba0a13c4de`.
The candidate156 barrier replay run qualified the runtime sequence described
below; see `GET_DEFAULT_PROMOTION.md` for the captures and totals.
`guest_state.py` reads guest addresses through a supplied callback; it never
executes a guest method or changes memory. `inspect_guest.py` records its result
under `guest.game_state`, alongside the existing GPU/APU snapshot.

## Fields and their consumers

| Observation | Guest location | Evidence and meaning |
| --- | --- | --- |
| World manager | `MEM32(0x00480998)` | Constructor `0x000ED74E` stores `this`; destructor `0x000EC957` clears the root. |
| Campaign object | `MEM32(manager+0xC34)` | Replacement in `0x000DE5E0`; check `MEM32(object+0x14)==0x159BEC67` before applying the following layout. |
| Failure/death flag | `MEM8(campaign+0x24)&2` | Writer `0x0009152A` sets it when `0x00090B10` returns true after its failure timer expires. `0x00111D00` reads it as part of the condition selecting **SQUAD KILLED**. It is a latched flag, not a death count or an individual character's health. |
| Network state | `MEM32(MEM32(0x00485560)+0x1C)!=0` | Getter `0x0020B380`. The Squad Killed predicate excludes this state. |
| Game interface | `MEM32(MEM32(0x00440370)+8)` | Initialized by `0x0006F583`; constructor `0x0008D435` installs vtable `0x003B69C0`. |
| Interface world | `MEM32(interface+0x5C0)` | Consumer `0x0008A350` tears down/reloads it. Pointer replacement alone does not establish Replay Mission. |
| Pending transitions | deque at `interface+0x6C8` | `+4` slot array, `+8` capacity, `+0xC` first, `+0x10` count. Consumer takes newest first. Descriptor low five bits are operation flags, not an enum. Flags `6` are the Replay Mission request produced by `0x000845F0`. |
| SDK swap serial | `MEM32(MEM32(0x003AB378)+0x2018)` | `0x00399DF0` increments it and returns it. This is a guest SDK swap serial; it is distinct from completed host presentation and simulation time. |

The full `0x00111D00` predicate also calls the manager's `+0xBD4` object's
virtual method `+0x34` and requires its result to be below two. The reader
retains that object's method address/bytes but does **not** execute it or
pretend the death bit alone reproduces the full UI predicate. Its boolean
`death_flag` refers to the statically established failure flag only.

`0x00113570` case `0xF3CD882F` chooses table `0x0042A82C` index one when
`0x00111D00` is true. That entry is localization key `0xFC2040F1`, whose English
text is `SQUAD KILLED`. Table `0x0042A7F4` index one is key `0xE2A7E3B3`,
`Replay Mission`. Strings were checked in `common.tgz` member
`common/stringtable/string-table-en.xmb` at key offsets `0xBB6D` and `0xB9D5`.
Their strings start five bytes after those offsets (length prefix then UTF-16).

## Replay checkpoints for persistent observation

The implemented hooks also observe Replay Mission's fully prepared descriptor
at `0x0008468F` (stack `ESP+0x14`, interface `ESI`).
`0x00111D43` observes the failure bit after the guest's other Squad Killed
checks, without calling the predicate itself. `install_guest_state_hooks.py`
restores all checkpoints after regeneration and fails on unexpected bodies.
`observe_guest_state.py` records the raw guarded fields independently.
`validate_guest_route.py` combines the field transitions and checkpoint lineage;
it does not infer replay from a single field or from confirmation preparation.

Preparation alone is insufficient. `0x000845F0` constructs flags `6`,
then invokes vtable `+0x30` (`0x00085D50`). This initiates world teardown and
transition UI work; `0x0010A920` saves the requested descriptor at UI object
`+0x6C`. Runtime captures refine the initial static UI interpretation: restart
confirmation is visible before preparation, while enqueue occurs after the
intro/loading transition. Enqueue is not itself proof of the user clicking
confirmation. The following checkpoints distinguish later work:

1. **UI-enqueued request:** immediately before guest instruction `0x0010B524`,
   `EAX` is the stored descriptor, `ECX` the interface and `EDI` the UI object.
   This path calls interface vtable `+0x20`, enqueueing the request. Require
   `(MEM8(EAX)&0x1F)==6`; UI flag `MEM8(EDI+0x64)&0x80` is clear on this path.
   The `0x0010B51F` alternative takes a different UI path and does not enqueue here.
2. **Consumed request:** immediately before `0x0008A38E`, `EBP` is the selected
   descriptor and `ESI` the interface. Require descriptor flags `6`. This is
   persistent-hook evidence that the request actually reached the consumer,
   which a periodically sampled deque cannot guarantee.
3. **Reload routine returned:** immediately before `0x0008A4CF`, the call to
   `0x00087820` at `0x0008A4CA` has returned. `EBP` still holds the descriptor
   and `ESI` the interface. Flags `6` identify the relevant path. `0x00087820`
   destroys any remaining manager and invokes `0x000830B0` to load the mission.
   Record the new world/campaign roots and flags. Returning from this routine
   establishes reload entry; resumed HUD/control still needs its own evidence.

For a replay gate, retain an earlier clear-to-set death-flag transition on a
recognized campaign object, then accepted and consumed flags-6 requests, then
the reload checkpoint and a later recognized world with its failure flag
clear. Pair these guest observations with completed-frame/control evidence.
`replay_entered` in the raw snapshot deliberately remains `null`: pointer
replacement, an empty deque or a single snapshot cannot prove this sequence.

## REA evidence and limits

REA rejected the XBE container (`file_format_rejected`). An ignored static PE
analysis carrier was therefore created at `analysis/guest-state-carrier.exe`:
image base `0x10000`, one `.guest` section starting `0x12000`, every original
XBE section's raw bytes copied to its unchanged guest VA, virtual tails/gaps
zero-filled. Carrier SHA-256:
`f4d69206a81a8ef4a9099da55707c7c660626e3e55b76bcd739e0da81cf5b34d`.
The wrapper is not an executable Xbox loader and was never executed.
`analysis/guest-state-carrier.json` records source/carrier identities.

REA used the configured headless IDA provider. Complete records are retained in
`analysis/guest-state-rea-evidence.json` (60 records at export). Key Evidence IDs:

| Procedure | REA Evidence ID |
| --- | --- |
| Failure flag writer `0x00091510` | `ev_1dd2b661ce0edb1db3c1ec61cd586b69697ed884bfbcdb258d5072f284f193a5` |
| Failure timer `0x00090B10` | `ev_b636ee96a568bb95a0e8cea291e43e39ac7176a1baa2cd3d8ef8986eed49bd54` |
| Squad Killed predicate `0x00111D00` | `ev_cb81e56ca6e0916c113d6a82654f6a08cb1b999ea3ab2e1ead14de2d25bb27c3` |
| UI localization selection `0x00113570` | `ev_85fb0f2123fe9a279a83a5b7d333f7b1b248795f6729eb202589da72c6353a25` |
| Replay action dispatch `0x00087300` | `ev_616240d39f13876afc1ee440c0026bc826999afe655093f313ddafb3012fdfd6` |
| Replay Mission preparation `0x000845F0` | `ev_2398f3776a8e4c8eb245319ad920e5d22cf3f50baf906187637a992eb0918eab` |
| Pending descriptor `0x0010A920` | `ev_4fbfc57530c628f93c2ef3623bd20297724d64bcd46bc0522366957a8e931ba4` |
| UI enqueue `0x0010B480` | `ev_a100611a28dedf8c663b486cfe15620b2f1e7ed449be5fab92fd3599c53187cd` |
| Transition consumer `0x0008A350` | `ev_22c4b2c07138ffb1e746e060cc6a7754b57bdda2794b772688f94eafa3a3f4da` |
| Mission reload `0x00087820` | `ev_e2e883d8f660d89bb04d70090c8b5003709f0da29e3d715554cc5f2456ad2b61` |
| SDK swap serial `0x00399DF0` | `ev_6ab8b43a72d5b4ff80d73b25fee1949c516ac8d2da4f0e87758288ab6493c00d` |
| World constructor `0x000ED3B0` | `ev_c8b78f88dc19c7de935e73c83b10c20b2ba677e41b0ff59931838d2f6d59271d` |

The IDA adapter does not expose complete typed references or function body
ranges. Decompiled `this` parameters are sometimes omitted or misinferred;
checkpoint registers and adjusted-this offsets above were checked against
the original disassembly and generated C. `0x00113980` decompilation failed;
its retained partial assembly (`ev_5f0a80ea026108464f7617e5c44d32dc9065a32a8078c4744c5379fd733998fd`)
and existing disassembly establish the action call, not complete pseudocode.
Static analysis did not observe a death or replay execution.

The reader bounds every request, validates the campaign class and interface
vtable, and rereads roots/deque bounds. These checks reject obvious transitions
or corrupted pointers but do not make a multi-object snapshot atomic.
