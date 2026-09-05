"""Evaluate frozen parameter and neural policies on the preregistered fresh cases."""
import argparse
import json
from pathlib import Path
import random
import statistics
import time
from .league import Matches, cases, model
from .selfplay import Arena, BASE_PLAYER, BASE_ENEMY, aggregate, fingerprint, save


def interval(values, rng):
    samples = sorted(sum(rng.choices(values, k=len(values))) / len(values) for _ in range(4000))
    return {'mean': statistics.mean(values), 'ci975': [samples[50], samples[3949]]}


def compare(challenger, reference, role):
    sign = 1 if role == 'player' else -1
    rng = random.Random(92834)
    return {'score': interval([(a['score'] - b['score']) * sign for a, b in zip(challenger, reference)], rng),
            'win_rate': interval([int(a['result'] == sign) - int(b['result'] == sign)
                                  for a, b in zip(challenger, reference)], rng)}


def constant(policy):
    weights = policy['network'][:]
    for h in range(8):
        for i in range(1, 13): weights[h * 13 + i] = 0.
    return model(policy['params'], weights)


def admit(binary, base, count, maps):
    """Reject only impossible layouts, never combat outcomes or model behavior."""
    accepted, rejected = [], []
    with Arena(binary) as arena:
        for map_id in maps:
            found = 0
            for index in range(count + 4096):
                seed = base + map_id * 10000 + index
                try:
                    arena.play(BASE_PLAYER, BASE_ENEMY, seed, map_id, ticks=1)
                except RuntimeError as exc:
                    problem = exc.args[0]
                    if not isinstance(problem, dict) or problem.get('error') != 'No valid combat spawn':
                        raise
                    rejected.append({'map': map_id, 'seed': seed, 'reason': problem['error']})
                    continue
                accepted.append((map_id, seed))
                found += 1
                if found == count: break
            if found < count: raise RuntimeError('Cannot admit enough valid encounters')
    # Preserve the original interleaved case order where every layout is valid.
    accepted.sort(key=lambda c: (c[1] - base - c[0] * 10000, c[0]))
    return accepted, rejected


def run(args):
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()): raise SystemExit('Use an empty output directory')
    param = json.loads(Path(args.parameters).read_text())
    neural = json.loads(Path(args.neural).read_text())
    frozen = {'parameters': param, 'neural': neural, 'config': vars(args), 'fingerprint': fingerprint()}
    save(output / 'frozen.json', frozen)  # Must precede any test matches.
    p, e = model(BASE_PLAYER), model(BASE_ENEMY)
    pp, pe = param['policies']['player'], param['policies']['enemy']
    np, ne = neural['policies']['player'], neural['policies']['enemy']
    matchups = {'original / original': (p, e), 'parameter / original': (pp, e),
                'original / parameter': (p, pe), 'neural / original': (np, e),
                'original / neural': (p, ne), 'parameter / parameter': (pp, pe),
                'neural / neural': (np, ne), 'constant neural / original': (constant(np), e),
                'original / constant neural': (p, constant(ne))}
    started = time.monotonic()
    evaluator = Matches(output, args.workers)
    summary = {'config': vars(args), 'fingerprint': frozen['fingerprint'], 'evaluation': {}}
    try:
        for split, count, maps in [('primary', args.primary_cases, (1, 2, 3)),
                                   ('unseen_maps', args.unseen_cases, (4, 5))]:
            encounters, rejected = admit(evaluator.binary, args.base, count, maps)
            summary.setdefault('admission', {})[split] = {'accepted': encounters, 'rejected': rejected}
            save(output / 'admission.json', summary['admission'])
            results = {}
            for name, (player, enemy) in matchups.items():
                results[name] = evaluator.get(player, enemy, encounters, 'test-' + split)
                print(split, name, aggregate(results[name]), flush=True)
            comparisons = {}
            for role in ('player', 'enemy'):
                label = (lambda kind: kind + ' / original') if role == 'player' else (lambda kind: 'original / ' + kind)
                comparisons[role] = {
                    'parameter_vs_original': compare(results[label('parameter')], results['original / original'], role),
                    'neural_vs_original': compare(results[label('neural')], results['original / original'], role),
                    'neural_vs_parameter': compare(results[label('neural')], results[label('parameter')], role),
                    'neural_vs_constant': compare(results[label('neural')], results[label('constant neural')], role)}
            summary['evaluation'][split] = {'matrix': {k: aggregate(v) for k, v in results.items()}, 'improvement': comparisons}
            save(output / 'summary.json', summary)
        summary['success'] = all(summary['evaluation']['primary']['improvement'][r]['neural_vs_original'][metric]['ci975'][0] > 0
                                 for r in ('player', 'enemy') for metric in ('score', 'win_rate'))
        summary['matches'] = len(evaluator.cache)
        summary['elapsed_seconds'] = time.monotonic() - started
        save(output / 'summary.json', summary)
        print('Primary success criterion:', summary['success'], flush=True)
    finally:
        evaluator.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parameters', required=True)
    parser.add_argument('--neural', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--base', type=int, default=9000000)
    parser.add_argument('--primary-cases', type=int, default=512)
    parser.add_argument('--unseen-cases', type=int, default=256)
    parser.add_argument('--workers', type=int, default=4)
    args = parser.parse_args()
    if min(args.primary_cases, args.unseen_cases, args.workers) < 1: parser.error('Counts must be positive')
    run(args)

if __name__ == '__main__': main()
