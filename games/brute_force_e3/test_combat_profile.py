import unittest

from analyze_combat_profile import audio_window, apu_window, clock_estimate, readback_accounting_errors, renderer_flip_window, renderer_window, wait_window
from test_performance import report


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
    def test_missing_executor_clock_remains_unknown(self):
        rows=[report(1),report(2)]
        rows[-1]['executor_cpu_ms']=None
        result=renderer_window(rows,[2,4],0)
        self.assertIsNone(result['totals']['executor_cpu_ms'])
        self.assertIsNone(result['per_flip']['executor_cpu_ms'])

    def test_sync_cost_uses_only_complete_flip_intervals(self):
        rows=[report(i) for i in range(1,4)]
        for i,row in enumerate(rows):row['flips']=100+i*10
        result=renderer_flip_window(rows,[105,120])
        self.assertEqual(result['measured_flips'],[110,120])
        self.assertEqual(result['totals']['flips'],10)
        self.assertEqual(renderer_flip_window(rows,[105,115])['status'],'insufficient_complete_intervals')
    def test_byte_volume_is_attributed_and_legacy_is_unknown(self):
        self.assertEqual(readback_accounting_errors([report(1)]),[])
        row=report(1)
        row.update(color_readback_bytes=4096,depth_readback_bytes=1024)
        row["sync"]["release"]["readback_bytes"]=5120
        self.assertEqual(readback_accounting_errors([row]),[])
        row["sync"]["release"]["readback_bytes"]=4096
        self.assertTrue(readback_accounting_errors([row]))
    def test_per_sync_cost_uses_work_count_and_same_flip_denominator(self):
        a,b=report(1),report(2)
        for row in (a,b):
            row["release_histogram"]=[0]*9
        b["sync"]["release"].update(calls=11,ms=30,outcomes=[6,5,0,0,0,0,0,0],work_ms=24)
        a["sync"]["release"]["work_ms"]=4
        result=renderer_window([a,b],[2,4],0)
        self.assertEqual(result["release_ms_per_call"],2)
        self.assertEqual(result["release_ms_per_work_call"],5)
        self.assertIsNone(result["per_flip"]["color_readback_bytes"])

    def test_apu_cpu_and_production_share_one_clock_interval(self):
        rows=[dict(tid=7,qpc_ms=1000,cpu_ms=50,submitted=48000),
              dict(tid=7,qpc_ms=61000,cpu_ms=650,submitted=48000+60*42000)]
        result=apu_window(rows,[1,61],0)
        self.assertEqual(result["cpu_ms"],600)
        self.assertEqual(result["source_frames_per_second"],42000)
        rows[-1]["tid"]=8
        with self.assertRaises(ValueError):apu_window(rows,[1,61],0)
    def test_separate_descheduling_from_cpu_and_event_waits(self):
        result = wait_window([wait(10), wait(20, completed=5, poll_count=3,
                             event_count=2, poll_wall_ms=3000, poll_cpu_ms=500,
                             event_wall_ms=4000, event_cpu_ms=10)], [10, 20], 0)
        self.assertIsNone(result["totals"]["poll_cpu_ms"])
        self.assertEqual(result["totals"]["event_wall_ms"], 4000)
        self.assertAlmostEqual(result["single_thread_wall_fraction"], .7)

    def test_short_cpu_sums_are_discarded_and_cycle_share_is_explicit(self):
        rows=[wait(10, poll_cpu_ms=0, poll_cycles=10, event_cycles=20,
                   thread_cycles=100, thread_cpu_ms=100),
              wait(20, poll_cpu_ms=50000, poll_cycles=2010, event_cycles=1020,
                   thread_cycles=4100, thread_cpu_ms=1300)]
        result=wait_window(rows,[10,20],0)
        self.assertIsNone(result['totals']['poll_cpu_ms'])
        self.assertEqual(result['totals']['poll_cycles'],2000)
        self.assertEqual(result['cpu_cycle_shares']['poll_cycle_share'],.5)
        self.assertEqual(result['cpu_cycle_shares']['event_cycle_share'],.25)

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
