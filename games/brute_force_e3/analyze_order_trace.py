"""Summarize NV2A ordering evidence; never infer a fix from watchdog exit alone."""
import argparse
import collections
import csv
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('name')
p.add_argument('--output', type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parent / 'analysis'
results = []
for path in sorted(root.glob(a.name + '*-order.csv')):
    name = path.name.removesuffix('-order.csv')
    counts = collections.Counter()
    irq_counts = collections.Counter()
    events = []
    event_kinds = collections.Counter()
    waits = {}
    wait_ms = []
    latest = {}
    first = last = None
    for row in csv.DictReader(path.open(newline='')):
        row = {k: (float(v) if k == 'qpc_seconds' else int(v, 0)) for k, v in row.items()}
        first = row['qpc_seconds'] if first is None else first
        last = row['qpc_seconds']
        event = row['event']
        counts[event] += 1
        latest[event] = row
        if event == 1:
            events.append(row)
            event_kinds[row['b']] += 1
            if row['b'] == 2:
                waits[row['tid']] = row
            elif row['b'] == 3 and row['tid'] in waits:
                start = waits.pop(row['tid'])
                wait_ms.append((row['qpc_seconds'] - start['qpc_seconds']) * 1000)
        if event == 5:
            irq_counts[(row['a'], row['b'], bool(row['c']))] += 1
    reports = []
    errors = []
    for line in (root / (name + '.stderr.log')).open(errors='replace'):
        if line.startswith('[HW COUNTERS] '):
            reports.append(json.loads(line[len('[HW COUNTERS] '):]))
        if any(tag in line for tag in ('[HW FENCE]', '[PB ERROR]', '[PB RESUME]', '[GPU INPUT] invalid vertex range')):
            errors.append(line.strip())
    result = dict(name=name, trace_seconds=None if first is None else last-first,
                  event_counts=dict(counts), irq_counts=[dict(vector=k[0], entered=k[1],
                  mask_raised=k[2], count=v) for k, v in sorted(irq_counts.items())],
                  target_event_kinds=dict(event_kinds), target_event_tail=events[-64:],
                  unmatched_waits=list(waits.values()), completed_waits=len(wait_ms),
                  longest_wait_ms=max(wait_ms, default=None),
                  latest_by_event=latest, failures=errors,
                  hw_reports=len(reports), last_hw=reports[-1] if reports else None)
    for suffix, key in [('-order.json', 'observer'), ('.json', 'probe'),
                        ('-guest-inspection.json', 'main_snapshot'),
                        ('-executor-inspection.json', 'executor_snapshot')]:
        extra = root / (name + suffix)
        if extra.exists():
            result[key] = json.loads(extra.read_text())
    result['graphics_stall'] = bool(waits) and bool(reports) and bool(result.get('probe')) and (
        last - reports[-1]['qpc_ms'] / 1000 > 10)
    result['all_reported_sync_failures'] = sum(
        v['failed'] for report in reports for v in report.get('sync', {}).values())
    results.append(result)
text = json.dumps(results, indent=2)
if a.output:
    a.output.write_text(text)
else:
    print(text)
