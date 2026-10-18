import unittest

from analyze_combat_profile import audio_window, clock_estimate, wait_window


def wait(t, tid=1, **changes):
    row = dict(tid=tid, qpc_ms=t*1000, completed=0, poll_count=0,
               event_count=0, poll_wall_ms=0, event_wall_ms=0,
               poll_cpu_ms=0, event_cpu_ms=0, cpu_errors=0)
    row.update(changes)
    return row


def audio(t, rate=48000, underruns=0, queued=2):
    return dict(qpc=t*10000000, hz=10000000, played=round(t*rate),
                submitted=round(t*rate)+1024, queued=queued, staged=0,
                underruns=underruns)


class CombatProfileTests(unittest.TestCase):
    def test_separate_descheduling_from_cpu_and_event_waits(self):
        result = wait_window([wait(10), wait(20, completed=5, poll_count=3,
                             event_count=2, poll_wall_ms=3000, poll_cpu_ms=500,
                             event_wall_ms=4000, event_cpu_ms=10)], [10, 20], 0)
        self.assertEqual(result["totals"]["poll_cpu_ms"], 500)
        self.assertEqual(result["totals"]["event_wall_ms"], 4000)
        self.assertAlmostEqual(result["single_thread_wall_fraction"], .7)

    def test_sampling_exclusion_and_thread_denominator(self):
        rows = [wait(10), wait(10, tid=2), wait(20, completed=1),
                wait(20, tid=2, completed=1)]
        self.assertIsNone(wait_window(rows, [10, 20], 0)["single_thread_wall_fraction"])
        result = wait_window(rows, [10, 20], 0, [{"start": 15, "end": 16}])
        self.assertEqual(result["totals"]["completed"], 0)

    def test_counter_reset_rejected(self):
        with self.assertRaises(ValueError):
            wait_window([wait(10, completed=5), wait(20, completed=1)], [10, 20], 0)

    def test_starvation_rate_is_not_clock_drift(self):
        rows = [audio(t, rate=44000, underruns=t, queued=0) for t in range(61)]
        self.assertIsNone(clock_estimate(rows)["estimated_drift_ppm"])
        result = audio_window(rows, [0, 60], 0)
        self.assertAlmostEqual(result["device_unplayed_wall_ms"], 5000)
        self.assertEqual(result["drift_sensitivity_1000ppm_ms"], 60)

    def test_fed_clock_estimate_retains_drift(self):
        rows = [audio(t, rate=48004.8) for t in range(61)]
        clock = clock_estimate(rows)
        self.assertAlmostEqual(clock["estimated_drift_ppm"], 100)
        self.assertAlmostEqual(audio_window(rows, [0, 60], 0,
                               clock["estimated_samples_per_second"])["device_unplayed_wall_ms"], 0)


if __name__ == "__main__":
    unittest.main()
