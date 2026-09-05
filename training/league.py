"""CPU population training of tactical controllers and small residual neural policies.

Selection/validation only. The separate evaluate command consumes frozen models.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import random
import statistics
import threading
import time

from .selfplay import Arena, PLAYER, ENEMY, BASE_PLAYER, BASE_ENEMY, ensure_engine, fingerprint, save, mutate


def model(params, network=None):
    return {'params': list(params), 'network': network}


def identity(policy):
    return hashlib.sha256(json.dumps(policy, sort_keys=True).encode()).hexdigest()[:20]


def cases(base, count, maps=(1, 2, 3)):
    return [(m, base + m * 10000 + i) for i in range(count) for m in maps]


class Matches:
    def __init__(self, output, workers=4):
        self.binary = ensure_engine()
        self.local = threading.local()
        self.arenas = []
        self.executor = ThreadPoolExecutor(max_workers=workers)
        self.cache, self.models = {}, {}
        self.output = Path(output)
        self.log = (self.output / 'matches.jsonl').open('w')

    def worker(self, item):
        key, p, e, case = item
        if not hasattr(self.local, 'arena'):
            self.local.arena = Arena(self.binary)
            self.arenas.append(self.local.arena)
        m, seed = case
        return key, self.local.arena.play(p['params'], e['params'], seed, m,
                        player_net=p['network'], enemy_net=e['network'])

    def get(self, player, enemy, encounters, phase):
        pi, ei = identity(player), identity(enemy)
        self.models[pi], self.models[ei] = player, enemy
        keys = [(pi, ei, m, seed) for m, seed in encounters]
        pending = [(key, player, enemy, case) for key, case in zip(keys, encounters) if key not in self.cache]
        for key, result in self.executor.map(self.worker, pending):
            self.cache[key] = result
            self.log.write(json.dumps({'player': pi, 'enemy': ei, 'phase': phase, **result}) + '\n')
        self.log.flush()
        return [self.cache[key] for key in keys]

    def results(self, role, policy, rivals, encounters, phase):
        return [r for rival in rivals for r in (self.get(policy, rival, encounters, phase)
                if role == 'player' else self.get(rival, policy, encounters, phase))]

    def fitness(self, role, policy, rivals, encounters, phase):
        values = self.results(role, policy, rivals, encounters, phase)
        return statistics.mean(r['score'] for r in values) * (1 if role == 'player' else -1)

    def close(self):
        self.executor.shutdown(wait=True)
        for arena in self.arenas: arena.close()
        self.log.close()
        save(self.output / 'models.json', self.models)


def neural_start(policy, rng):
    return model(policy['params'], [round(rng.gauss(0, .4), 7) for _ in range(104)] + [0.] * 45)


def child(policy, role, neural, rng, generation):
    if not neural:
        return model(mutate(policy['params'], PLAYER if role == 'player' else ENEMY, rng,
                            rng.choice([.08, .18, .35, .7])))
    weights = policy['network'][:]
    scale = rng.choice([.05, .12, .3, .7])
    kind = rng.randrange(4)
    # Bias-only children compete with state-dependent children. Most mutations
    # change connections; no backprop or libraries are needed for this small MLP.
    indices = (list(range(149)) if kind == 0 else list(range(104, 149)) if kind == 1
               else [104 + 9 * rng.randrange(5)] if kind == 2
               else rng.sample(range(149), 16))
    for i in indices:
        weights[i] = round(max(-4, min(4, weights[i] + rng.gauss(0, scale))), 7)
    return model(policy['params'], weights)


def run(args):
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()): raise SystemExit('Use an empty output directory')
    started = time.monotonic()
    code_hash = fingerprint()
    rng = random.Random(args.seed)
    pools = {'player': [model(BASE_PLAYER)], 'enemy': [model(BASE_ENEMY)]}
    history = []
    if args.initial:
        previous = json.loads(Path(args.initial).read_text())
        for role in pools:
            pools[role].append(previous['policies'][role])
    if args.neural:
        for role in pools:
            pools[role].append(neural_start(pools[role][-1], rng))
    evaluator = Matches(output, args.workers)
    try:
        for generation in range(1, args.generations + 1):
            for role in ['player', 'enemy']:
                other = 'enemy' if role == 'player' else 'player'
                rival_pool = pools[other]
                # Original plus latest adversary. Add a past adversary after
                # several promotions to resist forgetting older strategies.
                rivals = [rival_pool[0]]
                if len(rival_pool) > 1: rivals.append(rival_pool[-1])
                if len(rival_pool) > 4: rivals.append(rng.choice(rival_pool[1:-1]))
                rivals = list({identity(r): r for r in rivals}.values())
                incumbent = pools[role][-1]
                candidates = [incumbent]
                for n in range(args.population - 1):
                    eligible = [p for p in pools[role] if not args.neural or p['network'] is not None]
                    parent = incumbent if n % 4 else rng.choice(eligible)
                    candidates.append(child(parent, role, args.neural, rng, generation))
                candidates = list({identity(c): c for c in candidates}.values())
                training = cases(args.development_base + generation * 2000, args.train_cases)
                for count, keep in [(args.screen_cases, 6), (args.train_cases, 1)]:
                    scores = [(evaluator.fitness(role, c, rivals, training[:count * 3], 'selection'), c) for c in candidates]
                    scores.sort(key=lambda row: row[0], reverse=True)
                    candidates = [c for _, c in scores[:keep]]
                best = candidates[0]
                validation = cases(args.development_base + 500000 + generation * 2000, args.validation_cases)
                gain = (evaluator.fitness(role, best, rivals, validation, 'validation')
                        - evaluator.fitness(role, incumbent, rivals, validation, 'validation'))
                anchor_gain = (evaluator.fitness(role, best, [rival_pool[0]], validation, 'validation')
                        - evaluator.fitness(role, incumbent, [rival_pool[0]], validation, 'validation'))
                accepted = identity(best) != identity(incumbent) and gain > .005 and anchor_gain >= 0
                if accepted: pools[role].append(best)
                record = {'generation': generation, 'role': role, 'gain': gain, 'anchor_gain': anchor_gain,
                          'accepted': accepted, 'candidate': best, 'incumbent': identity(incumbent)}
                history.append(record)
                print(f'{"Neural" if args.neural else "Parameter"} generation {generation:02} {role:6}: '
                      f'league {gain:+.3f}, original opponent {anchor_gain:+.3f}; '
                      f'{"promoted" if accepted else "retained"}; {len(evaluator.cache):,} matches', flush=True)
                save(output / 'checkpoint.json', {'version': 3, 'fingerprint': code_hash, 'config': vars(args),
                     'policies': {r: pools[r][-1] for r in pools}, 'pools': pools, 'history': history,
                     'matches': len(evaluator.cache), 'elapsed_seconds': time.monotonic() - started})
        # Larger diagnostic validation, still development data, before freezing.
        validation = cases(args.development_base + 800000, args.validation_cases * 2)
        reference = evaluator.get(pools['player'][0], pools['enemy'][0], validation, 'development-audit')
        audit = {}
        for role in pools:
            selected = evaluator.results(role, pools[role][-1], [pools['enemy' if role == 'player' else 'player'][0]], validation, 'development-audit')
            win = 1 if role == 'player' else -1
            audit[role] = {'baseline_wins': sum(r['result'] == win for r in reference),
                          'selected_wins': sum(r['result'] == win for r in selected), 'matches': len(reference),
                          'score_gain': statistics.mean((a['score'] - b['score']) * win for a, b in zip(selected, reference))}
        save(output / 'development.json', audit)
        checkpoint = json.loads((output / 'checkpoint.json').read_text())
        checkpoint['matches'] = len(evaluator.cache)
        checkpoint['elapsed_seconds'] = time.monotonic() - started
        checkpoint['development_audit'] = audit
        save(output / 'checkpoint.json', checkpoint)
        print(json.dumps(audit, indent=2), flush=True)
    finally:
        evaluator.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    parser.add_argument('--initial', help='Earlier checkpoint; original reference controllers remain fixed')
    parser.add_argument('--neural', action='store_true')
    parser.add_argument('--generations', type=int, default=8)
    parser.add_argument('--population', type=int, default=48)
    parser.add_argument('--screen-cases', type=int, default=24, help='Per map')
    parser.add_argument('--train-cases', type=int, default=96, help='Per map')
    parser.add_argument('--validation-cases', type=int, default=192, help='Per map')
    parser.add_argument('--development-base', type=int, default=3000000)
    parser.add_argument('--seed', type=int, default=20260906)
    parser.add_argument('--workers', type=int, default=4)
    args = parser.parse_args()
    if min(args.generations, args.population, args.screen_cases, args.train_cases, args.validation_cases, args.workers) < 1:
        parser.error('Counts must be positive')
    if args.screen_cases > args.train_cases: parser.error('Screen cases cannot exceed training cases')
    run(args)

if __name__ == '__main__': main()
