"""Recompute proportional K projections from the retained matched CI samples."""
import json
import math
from pathlib import Path
import statistics

root = Path(__file__).resolve().parent
results = []
for item in json.loads((root / 'source-manifest.json').read_text()):
    for run in item['qualification_runs']:
        directory = root / 'receipts' / run
        if not (directory / 'timing.json').exists():
            continue
        timing = json.loads((directory / 'timing.json').read_text())
        ratios = [r['medians']['candidate'] / r['medians']['champion']
                  for r in timing if r['threads'] == 12]
        assert len(ratios) == 3
        memory_ratios = []
        for path in sorted(directory.glob('memory-seed*.json')):
            rows = json.loads(path.read_text())
            peaks = {name: [r['peak'] for r in rows
                            if r['threads'] == 12 and r['name'] == name]
                     for name in ('champion', 'candidate')}
            assert all(len(p) == 15 for p in peaks.values())
            memory_ratios.append(statistics.median(peaks['candidate']) /
                                 statistics.median(peaks['champion']))
        assert len(memory_ratios) == 3
        t = math.prod(ratios) ** (1 / 3)
        m = math.prod(memory_ratios) ** (1 / 3)
        results.append({'branch': item['branch'], 'run': run,
                        'source_blob': item['source_blob'], 'S': item['S'],
                        'D': item['D'], 'T_ratio': t, 'M_ratio': m,
                        'projected_T': 0.016388 * t,
                        'projected_M': 21344256 * m,
                        'proxy_K': 49.960098 * t ** -0.5 * m ** -0.2 *
                                   (584 / item['S']) ** 0.15 * item['D'] ** 0.15})
print(json.dumps(results, indent=2))
