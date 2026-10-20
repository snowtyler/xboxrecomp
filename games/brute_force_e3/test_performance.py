"""Regression checks for cumulative diagnostic report accounting."""
import json
import copy
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import compare_performance
import performance


def report(number, scale=1):
    data = {key: number * scale for key in performance.COUNTER_KEYS}
    data.update(version=1, report=number, qpc_ms=number * 2000,
                thread_id=42, executor_cpu_ms=number * 100,
                release_histogram=[number] * 9,
                sync={"release": {"calls": number, "failed": 0, "ms": number * 10,
                                  "outcomes": [0, number, 0, 0, 0, 0, 0, 0]}},
                targets=[{"width": 640, "height": 480, "bpp": 4, "count": number}],
                target_overflow=0)
    return data


class CounterTests(unittest.TestCase):
    def test_readback_bytes_and_sync_cpu_remain_optional_and_monotonic(self):
        before, after = report(1), report(2)
        legacy = performance.counter_delta(before, after)
        self.assertIsNone(legacy["color_readback_bytes"])
        self.assertIsNone(legacy["sync"]["release"]["cpu_ms"])
        for index, row in enumerate((before, after), 1):
            row.update(color_readback_bytes=index*4096, depth_readback_bytes=index*1024)
            row["sync"]["release"].update(work_ms=index*8, color_readbacks=index,
                depth_readbacks=index, readback_bytes=index*5120, cpu_ms=index*6,
                cpu_cycles=index*6000, cpu_errors=0)
        delta=performance.counter_delta(before, after)
        self.assertEqual(delta["color_readback_bytes"],4096)
        self.assertEqual(delta["sync"]["release"]["readback_bytes"],5120)
        self.assertIsNone(delta["sync"]["release"]["cpu_ms"])
        self.assertEqual(delta["sync"]["release"]["cpu_cycles"],6000)
        after["depth_readback_bytes"]=0
        self.assertIsNone(performance.counter_delta(before,after))

    def test_report_and_truncated_json(self):
        parsed = performance.parse_line("[HW COUNTERS] " + json.dumps(report(1)))
        self.assertEqual(parsed["counters"]["thread_id"], 42)
        self.assertIsNone(performance.parse_line('[HW COUNTERS] {"version":'))

    def test_resets_gaps_and_thread_changes_are_rejected(self):
        before = report(1)
        for change in ({"report": 3}, {"thread_id": 43}, {"flips": 0}, {"qpc_ms": 2000}):
            after = report(2)
            after.update(change)
            self.assertIsNone(performance.counter_delta(before, after))

    def test_cpu_unavailable_preserves_other_counters(self):
        after = report(2)
        after["executor_cpu_ms"] = None
        delta = performance.counter_delta(report(1), after)
        result = performance.summarize_counters([{"delta": delta}])
        self.assertIsNone(result["per_frame"]["executor_cpu_ms"])
        self.assertEqual(result["per_frame"]["color"], 1)

    def test_outcomes_and_cpu_use_same_flip_denominator(self):
        delta = performance.counter_delta(report(1), report(2))
        result = performance.summarize_counters([{"delta": delta}])
        self.assertEqual(result["per_frame"]["executor_cpu_ms"], 100)
        self.assertEqual(result["sync_per_frame"]["release"]["color_readbacks"], 1)
        self.assertEqual(result["totals"]["targets"][0]["count"], 1)

    def test_only_complete_unexcluded_intervals_count(self):
        metadata = {"windows": [[2, 6], [8, 9]], "exclusions": [{"start": 4.5, "end": 5}]}
        events = [{"type": "hw_counters", "elapsed_seconds": number * 2,
                   "counters": report(number)} for number in range(1, 4)]
        result = performance.summarize_events(metadata, events)
        self.assertEqual(result["windows"][0]["hw_counters"]["complete_report_intervals"], 1)
        self.assertEqual(result["windows"][0]["hw_counters"]["totals"]["flips"], 1)
        self.assertIsNone(result["windows"][1]["hw_counters"])

    def test_legacy_reports_remain_readable(self):
        self.assertEqual(performance.parse_line("[HW SYNC] 10 syncs, 1.0 ms/sync, draws=50")["type"], "sync")
        self.assertIsNone(performance.summarize_counters([]))


class HardwareObservationTests(unittest.TestCase):
    def test_concurrent_context_rejects_changed_external_load_or_clocks(self):
        gpu = dict(sample_count=4, adapter='fixture-adapter', max_gap_seconds=5, counter_errors=0,
                   competing_3d_engine_peak_sum_percent=55,
                   process_3d_engine_percent={'42': dict(owned=True, mean=30), '99': dict(owned=False, mean=50)})
        metrics = {f'device/{key}': dict(mean=value, min=value, max=value)
                   for key, value in [('graphics_mhz', 2000), ('memory_mhz', 10000), ('power_w', 100)]}
        window = dict(requested_start_seconds=560, gpu_observation=gpu,
                      nvidia_observation=dict(sample_count=4, query_errors=0, metrics=metrics))
        baseline = dict(name='before', aggregate_ms_per_frame=53, executor_cpu_ms_per_frame=50,
                        vertex_work_per_frame=dict(program_batches=100, program_vertices=1000), windows=[window])
        for change, expected in [('stable', True), ('load', False), ('clocks', False)]:
            candidate = copy.deepcopy(baseline)
            candidate.update(name='after', aggregate_ms_per_frame=41, executor_cpu_ms_per_frame=39)
            if change == 'load':
                candidate['windows'][0]['gpu_observation']['process_3d_engine_percent']['99']['mean'] = 70
            if change == 'clocks':
                candidate['windows'][0]['nvidia_observation']['metrics']['device/graphics_mhz'] = dict(mean=1000, min=1000, max=1000)
            captured = []
            arguments = ['compare', '--name', 'fixture'] + ['--baseline', 'before', '--candidate', 'after'] * 3
            with mock.patch('sys.argv', arguments), mock.patch.object(compare_performance, 'observation', side_effect=[baseline]*3+[candidate]*3), mock.patch.object(Path, 'write_text', lambda path, content, **kw: captured.append(content)), mock.patch('builtins.print'):
                compare_performance.main()
            result = json.loads(captured[0])['repeated_runs']
            self.assertTrue(result['performance_threshold_passed'])
            self.assertTrue(result['hardware_observation_complete'])
            self.assertFalse(result['low_competing_gpu_load'])
            self.assertEqual(result['concurrent_context_comparable'], expected, change)

    def test_competing_process_queues_are_not_double_counted(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parent) as directory:
            project = Path(directory)
            analysis = project / 'analysis'
            analysis.mkdir()
            window = dict(requested_start_seconds=10, requested_end_seconds=20,
                          measured_seconds=10, completed_flips=100,
                          vertex_work=dict(weighted_flips=100, program_batches=200,
                                           program_vertices=300), hw_counters=None)
            report_data = dict(metadata=dict(elapsed_zero_utc='2026-10-14T00:00:00+00:00',
                                            pid=42, executable_sha256='fixture',
                                            route_validation='fixture'), windows=[window])
            (analysis / 'run-performance.json').write_text(json.dumps(report_data))
            (analysis / 'run.json').write_text('{}')
            engines = [dict(instance=f'pid_{pid}_luid_0x0_0x1_phys_0_eng_{queue}_engtype_3d', percent=value)
                       for pid, queue, value in [(42, 0, 30), (99, 0, 50), (99, 1, 40), (100, 0, 2)]]
            samples = [dict(utc=f'2026-10-14T00:00:{second:02d}Z', engines=engines, counter_errors=[])
                       for second in [10, 15, 20]]
            path = analysis / 'run-gpu-load.jsonl'
            path.write_text('\n'.join(json.dumps(sample) for sample in samples))
            observed = compare_performance.observation(project, 'run')['windows'][0]['gpu_observation']
            self.assertEqual(observed['sample_count'], 3)
            self.assertEqual(observed['max_gap_seconds'], 5)
            self.assertEqual(observed['competing_3d_engine_peak_sum_percent'], 52)
            self.assertEqual(observed['process_3d_engine_percent']['99']['peak'], 50)
            self.assertTrue(observed['process_3d_engine_percent']['42']['owned'])
            path.unlink()
            missing = compare_performance.observation(project, 'run')['windows'][0]['gpu_observation']
            self.assertIsNone(missing['adapter'])
            self.assertIsNone(missing['competing_3d_engine_peak_sum_percent'])
            self.assertEqual(missing['sample_count'], 0)


if __name__ == "__main__":
    unittest.main()
