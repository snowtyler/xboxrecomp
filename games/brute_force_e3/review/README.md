# Claude review handoff

Start with [opus55/PROMPT.md](opus55/PROMPT.md) and
[opus55/SNAPSHOT.md](opus55/SNAPSHOT.md). The performance goal is paused and the
candidate has not replaced the verified executable.

`brute-force-opus55-review.zip` is a standalone source/evidence snapshot for
uploading to a reviewer. In a cloud checkout, extract it to a separate scratch
directory to read the ignored analysis logs, reports and screenshots. Repository
paths are preserved inside the ZIP; the prompt and snapshot are under
`games/brute_force_e3/review/opus55/`. Its MANIFEST.json records file sizes and
SHA256 hashes. The exporter verifies archive integrity and every listed hash.

The archive contains relevant runtime/game-harness source, tests, current and
historical evidence, a limited initial-source diff, and selected captures.
Original game assets, generated game-code chunks, executables/PDBs, saves and
build outputs are intentionally omitted. Building/playing still requires the
user's local assets and regeneration described in the game README.

The8.1% result is an instrumented fixed-view comparison. Normal graphics replay
verification was interrupted by the user, and query-free performance comparison
remains unfinished. Do not treat the handoff commit as a verified release.
