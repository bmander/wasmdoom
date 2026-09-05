#!/usr/bin/env python3
"""Replay a multi-worker evaluation serially, including disruptive predecessors."""
import argparse
import gzip
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from training.selfplay import Arena, BASE_PLAYER, BASE_ENEMY, fingerprint, save

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path)
args = parser.parse_args()
output = args.directory
models = json.loads((output / 'models.json').read_text())
log = output / 'matches.jsonl.gz'
open_log = gzip.open if log.exists() else open
if not log.exists(): log = output / 'matches.jsonl'
started = time.monotonic()
failures, count, predecessors = [], 0, 0
with Arena() as arena, open_log(log, 'rt') as records:
    for line in records:
        record = json.loads(line)
        if count % 127 == 0:
            arena.play(BASE_PLAYER, BASE_ENEMY, 91, 1, ticks=1)
            predecessors += 1
        p, e = models[record['player']], models[record['enemy']]
        replay = arena.play(p['params'], e['params'], record['seed'], record['map'],
                            player_net=p['network'], enemy_net=e['network'])
        if any(record[k] != v for k, v in replay.items()):
            failures.append({'record': record, 'replay': replay})
        count += 1
        if count % 4096 == 0: print(f'Replayed {count:,}; mismatches: {len(failures)}', flush=True)
result = {'matches': count, 'short_predecessors': predecessors, 'mismatches': failures,
          'fingerprint': fingerprint(), 'elapsed_seconds': time.monotonic() - started,
          'passed': not failures and count > 0}
save(output / 'replay-audit.json', result)
print(f'Replayed {count:,}; mismatches: {len(failures)}', flush=True)
if not result['passed']: raise SystemExit(1)
