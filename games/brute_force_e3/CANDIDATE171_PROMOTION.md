# Candidate171 named prototype promotion

October 20, 2026 (America/New_York). Fast-forward merged `codex/integrate-v015`
through `77f449c` into main, after fetching origin and verifying ancestry.
The [integration report](UPSTREAM_V015_INTEGRATION.md) records the seven separate
item commits, applicable ports, tests and explicit deferrals.

Candidate171 passed the 1500.4-second normal-barrier gameplay/death/replay gate,
with default GET progress and monitoring: 3,596,381,192 reads, max lag 0x3FC,
zero far reads and zero read-past decisions. Guest state and reviewed completed
frames prove death, replay and resumed gameplay. The default changes include
callback stack/register restore, POP ESP, lifter regeneration, flag caching and
the bounded ADPCM/per-frame volume fixes. Both rendering diagnostics remain off.
There is no new GPU completion/publication, coalescing or IRQ policy.

The named `build/RelWithDebInfo/brute_force_e3_prototype.exe/.pdb` are now exact
copies of candidate171, not a rebuild. SHA-256:

- EXE: `3b1b3822e3a879ddbc8d7fa6f127bce5bd1b404e2b9659de1ddc8527114a0b37`
- PDB: `f6e8a73dc77b1269e8e21e26a4546f4bf1d15130fbfd64e624db43cf4452961b`

The previous named files and hashes are preserved in ignored
`analysis/prototype-before-v015-20261020/`. Candidate157 and all numbered
candidates remain untouched. Rollback consists of restoring those named files;
GET's explicit `=0` rollback remains available separately.

`analysis/test-launcher171-promoted.json` confirms the normal launcher created
its own visible window, closed it, observed child exit and returned exit 0.
This is a promotion smoke check, not another gameplay qualification run.
The 92 native tests, 608 lifter tests and regenerated dispatch/function census
are documented in the integration report. Performance and rendering limitations
remain; no speedup or visual-fix claim follows from this promotion.
