"""Checks that qualification rejects blind monitors and missing evidence."""
import csv
import json
import tempfile
import unittest
from pathlib import Path
from analyze_get_progress import summarize
from install_get_hooks import HOOKS, install

class QualificationTests(unittest.TestCase):
    def fixture(self, root, progress=True, far=0, pfb=True, lost=0):
        prefix = root / "probe"
        def put(suffix, value):
            prefix.with_name(prefix.name + suffix).write_text(json.dumps(value))
        put("-process.json", {"configuration": {"RECOMP_NV2A_GET_PROGRESS": "1" if progress else None}})
        put("-order.json", dict(records=6, discarded=lost, overwritten=0))
        put(".json", dict(exit_code=3, stopped=False, timed_out=False))
        put("-guest-inspection.json", {"gpu": {"pfb_trap_active": pfb,"pfb_trap_matches_guest":pfb}})
        prefix.with_name(prefix.name + ".stderr.log").write_text(
            f"[PB GET] reads=100 far={far} invalid=0 untrusted=0 max_lag=3FC publications=2 freezes=0 backward=0 ring_calls=0\n")
        records = [(13, 0xF011, 4, 0, 0), (7, 0x80158B24, 0x1D70, 0xF011, 0),
                   (16, 0x80158B34, 0x1D90, 0, 0),
                   (14, 0xF011, 0x80158B20, 0x80158B00, 0 if progress else 0x58B8),
                   (17, 0xF011, 0, 0, 0), (18, 0xF011, 1, 100, 80)]
        with prefix.with_name(prefix.name + "-order.csv").open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(["sequence", "qpc", "qpc_seconds", "tid", "event", "a", "b", "c", "d"])
            for i, row in enumerate(records, 1):
                writer.writerow([i, i, i / 100, 7, row[0], *[hex(x) for x in row[1:]]])
        return prefix

    def test_fixed_invariant_and_poll_duration(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent) as directory:
            result = summarize(self.fixture(Path(directory)))
            self.assertTrue(result["automated_checks_passed"])
            self.assertEqual(result["poll_spin_total_us"], 80)

    def test_control_must_expose_stale_get(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent) as directory:
            root = Path(directory)
            self.assertFalse(summarize(self.fixture(root, progress=False))["automated_checks_passed"])
            result = summarize(self.fixture(root, progress=False, far=1))
            self.assertTrue(result["automated_checks_passed"])
            self.assertEqual(result["waits_after_placeholder_fetch"], 1)

    def test_fixed_violations_and_missing_evidence_fail(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent) as directory:
            root = Path(directory)
            for options in (dict(far=1), dict(pfb=False), dict(lost=1)):
                self.assertFalse(summarize(self.fixture(root, **options))["automated_checks_passed"])

    def test_generated_hooks_are_complete_idempotent_and_fail_closed(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent) as directory:
            path = Path(directory) / "generated.c"
            path.write_text("\n".join(HOOKS))
            install(path); first = path.read_text(); install(path)
            self.assertEqual(first, path.read_text())
            for label, hook in HOOKS.items():
                self.assertIn(label + "\n" + hook, first)
            path.write_text("invalid generated output")
            with self.assertRaises(ValueError):
                install(path)

if __name__ == "__main__":
    unittest.main()
