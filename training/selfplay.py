"""Alternating population search against a league of real DOOM opponents."""
import argparse
import hashlib
import html
import json
import os
from pathlib import Path
import random
import select
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
# Each row is name, minimum, maximum, starting value. Order is the arena protocol.
PLAYER = [('range', 96, 400, 192), ('strafe', 0, 40, 12), ('advance', 12, 50, 25),
          ('retreat', 12, 50, 25), ('aim_gain', 10, 100, 50), ('fire_angle', 1, 12, 4),
          ('switch_time', 18, 140, 70), ('turn_rate', 384, 2048, 1024)]
ENEMY = [('range', 160, 400, 288), ('cover_wait', 12, 70, 20),
         ('cover_retry', 35, 140, 70), ('peek_time', 18, 70, 35),
         ('dodge_reaction', 6, 16, 6), ('flank', 0, 96, 64),
         ('attack_delay', 24, 60, 24), ('lead', 0, 80, 66)]
BASE_PLAYER = tuple(row[3] for row in PLAYER)
BASE_ENEMY = tuple(row[3] for row in ENEMY)


def fingerprint():
    digest = hashlib.sha256()
    paths = [ROOT / 'scripts/build_training.py', ROOT / 'training/selfplay.py']
    for directory in ['training', 'src', 'vendor/doomgeneric/doomgeneric']:
        paths.extend(p for p in (ROOT / directory).iterdir() if p.suffix in {'.c', '.h'})
    for path in sorted(set(paths)):
        digest.update(str(path.relative_to(ROOT)).encode())
        digest.update(path.read_bytes())
    digest.update((ROOT / 'public/assets/doom1.wad').read_bytes())
    return digest.hexdigest()


def ensure_engine():
    binary = ROOT / '.cache/selfplay-engine'
    inputs = [ROOT / 'scripts/build_training.py']
    for directory in ['training', 'src', 'vendor/doomgeneric/doomgeneric']:
        inputs.extend(p for p in (ROOT / directory).iterdir() if p.suffix in {'.c', '.h'})
    if not binary.exists() or any(p.stat().st_mtime > binary.stat().st_mtime for p in inputs):
        subprocess.run(['python3', str(ROOT / 'scripts/build_training.py')], check=True)
    return binary


class Arena:
    def __init__(self, binary=None):
        binary = binary or ensure_engine()
        self.log = open(ROOT / '.cache/selfplay-engine.log', 'ab')
        self.process = subprocess.Popen([str(binary), '-iwad',
            str(ROOT / 'public/assets/doom1.wad'), '-warp', '1', '1', '-skill', '3', '-nosound'],
            cwd=ROOT / '.cache', stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log)
        self.buffer = b''

    def play(self, player, enemy, seed, map_id, ticks=700):
        for values, space in [(player, PLAYER), (enemy, ENEMY)]:
            if len(values) != len(space) or any(not isinstance(v, int) or not lo <= v <= hi
                for v, (_, lo, hi, _) in zip(values, space)):
                raise ValueError('Policy values are outside the allowed tactical bounds')
        command = ' '.join(map(str, [seed, map_id, ticks, *player, *enemy])) + '\n'
        self.process.stdin.write(command.encode())
        self.process.stdin.flush()
        deadline = time.monotonic() + 30
        while True:
            while b'\n' in self.buffer:
                line, self.buffer = self.buffer.split(b'\n', 1)
                if line.startswith(b'RESULT '):
                    result = json.loads(line[7:])
                    if 'error' in result:
                        raise RuntimeError(result)
                    if result['seed'] != seed or result['map'] != map_id:
                        raise RuntimeError('Arena response did not match the requested encounter')
                    return result
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.process.stdout], [], [], remaining)[0]:
                raise TimeoutError('Headless match exceeded 30 seconds of wall time')
            chunk = os.read(self.process.stdout.fileno(), 65536)
            if not chunk:
                raise RuntimeError('Arena exited unexpectedly; inspect .cache/selfplay-engine.log')
            self.buffer += chunk

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.process.stdout.close()
        self.log.close()

    def __enter__(self): return self
    def __exit__(self, *_): self.close()


class Evaluation:
    def __init__(self, arena, path, ticks):
        self.arena, self.ticks = arena, ticks
        self.cache = {}
        self.log = open(path, 'w')

    def match(self, p, e, scenario, phase):
        map_id, seed = scenario
        key = (tuple(p), tuple(e), map_id, seed)
        if key not in self.cache:
            result = self.arena.play(p, e, seed, map_id, self.ticks)
            self.cache[key] = result
            self.log.write(json.dumps({'player': p, 'enemy': e, 'phase': phase, **result}) + '\n')
            self.log.flush()
        return self.cache[key]

    def fitness(self, role, policy, opponents, scenarios, phase):
        results = [self.match(policy, opp, case, phase) if role == 'player'
                   else self.match(opp, policy, case, phase)
                   for opp in opponents for case in scenarios]
        return statistics.mean(r['score'] for r in results) * (1 if role == 'player' else -1)


def mutate(policy, space, rng, scale=0.16):
    # Some children make one deliberate change; others explore combinations.
    chosen = set(range(len(space))) if rng.random() < .5 else {rng.randrange(len(space))}
    return tuple(max(lo, min(hi, round(v + rng.gauss(0, (hi - lo) * scale)))) if i in chosen else v
        for i, (v, (_, lo, hi, _)) in enumerate(zip(policy, space)))


def opponents(pool, rng):
    return list(dict.fromkeys([tuple(pool[0]), tuple(pool[-1]), tuple(rng.choice(pool))]))


def paired_interval(differences, seed):
    rng = random.Random(seed)
    samples = sorted(statistics.mean(rng.choices(differences, k=len(differences))) for _ in range(2000))
    return {'mean': statistics.mean(differences), 'ci95': [samples[50], samples[1949]]}


def aggregate(results):
    return {'matches': len(results), 'player_wins': sum(r['result'] == 1 for r in results),
            'enemy_wins': sum(r['result'] == -1 for r in results),
            'draws': sum(r['result'] == 0 for r in results),
            'player_score': statistics.mean(r['score'] for r in results)}


def save(path, data):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(data, indent=2) + '\n')
    temporary.replace(path)


def report(output, result):
    rows = ''
    for label, data in result['evaluation']['matrix'].items():
        rows += f'<tr><td>{html.escape(label)}</td><td>{data["player_wins"]}</td><td>{data["enemy_wins"]}</td><td>{data["draws"]}</td><td>{data["player_score"]:.3f}</td></tr>'
    gains = ''
    for role, data in result['evaluation']['improvement'].items():
        lo, hi = data['ci95']
        conclusion = 'Positive on these encounters' if lo > 0 else 'Negative on these encounters' if hi < 0 else 'Inconclusive at 95%'
        gains += f'<article><h2>{role.title()} improvement</h2><strong>{data["mean"]:+.3f}</strong><p>Paired score change against the original opponent.<br>95% bootstrap interval: [{lo:+.3f}, {hi:+.3f}]</p><p>{conclusion}</p></article>'
    history = ''.join(f'<tr><td>{r["generation"]}</td><td>{r["role"]}</td><td>{r["validation_gain"]:+.3f}</td><td>{"Promoted" if r["accepted"] else "Kept incumbent"}</td></tr>' for r in result['history'])
    content = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>DOOM self-play experiment</title>
<style>body{font:17px/1.6 system-ui;max-width:1040px;margin:45px auto;padding:0 24px;background:#141815;color:#e6e8dc}h1,h2,strong{color:#dec77d}h1{font-size:38px}h2{font-size:22px}strong{font-size:34px}a{color:#dec77d}.cards{display:flex;gap:24px;flex-wrap:wrap}article{background:#222a24;padding:24px;flex:1;min-width:250px}table{border-collapse:collapse;width:100%;font-size:15px}td,th{text-align:left;border-bottom:1px solid #465147;padding:10px}code{overflow-wrap:anywhere;font-size:13px}</style>
<h1>DOOM self-play experiment</h1><p>Two compact tactical policies, trained against a league of earlier opponents in the real DOOM engine. This is parameter search over existing behaviors, not a neural network learning DOOM from pixels.</p>'''
    content += f'<p>{result["matches"]} simulated matches · {result["elapsed_seconds"]:.1f} seconds of wall time · seed {result["config"]["seed"]}</p><div class="cards">{gains}</div>'
    content += '<h2>Held-out matchups</h2><p>All cells use the same separate evaluation seeds. Training uses E1M1/E1M2 combat drills; E1M3 is unseen during selection. Wins require killing all opponents; death is a loss; the time limit produces a draw. Health and damage statistics remain original DOOM values.</p><table><tr><th>Player / enemy checkpoint</th><th>Player wins</th><th>Enemy wins</th><th>Draws</th><th>Player score</th></tr>' + rows + '</table>'
    content += '<h2>Promotion history</h2><p>Each candidate must beat its incumbent on separate validation encounters against the same opponents. These changing matchups are not directly comparable across generations.</p><table><tr><th>Generation</th><th>Side</th><th>Validation score gain</th><th>Decision</th></tr>' + history + '</table>'
    content += '<h2>Interpretation</h2><p>The bots receive structured line-of-sight observations and geometry queries. Short combat drills do not establish full-level competence or improvement against humans. Confidence intervals summarize these sampled encounters, not every DOOM map. Trained policies are saved for further experiments and are not automatically enabled in the browser.</p>'
    content += '<p><a href="summary.json">Full results and checkpoints</a> · <a href="matches.jsonl">Raw match results</a></p>'
    content += f'<p>Engine, trainer, and IWAD fingerprint: <code>{result["fingerprint"]}</code></p></html>'
    (output / 'report.html').write_text(content)


def run(args):
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    if any((output / name).exists() for name in ['checkpoint.json', 'matches.jsonl', 'summary.json']):
        raise SystemExit('Output already contains a run; choose a new --output directory.')
    binary = ensure_engine()
    started = time.monotonic()
    player_pool, enemy_pool = [BASE_PLAYER], [BASE_ENEMY]
    history = []
    config = vars(args)
    code_hash = fingerprint()
    with Arena(binary) as arena:
        evaluator = Evaluation(arena, output / 'matches.jsonl', args.seconds * 35)
        try:
            # Determinism audit: repeating a match after an unrelated episode
            # must reproduce the outcome exactly, not depend on league order.
            audit = arena.play(BASE_PLAYER, BASE_ENEMY, 91, 1, 350)
            arena.play(BASE_PLAYER, BASE_ENEMY, 92, 2, 350)
            if audit != arena.play(BASE_PLAYER, BASE_ENEMY, 91, 1, 350):
                raise RuntimeError('Arena failed the repeatability audit')
            for generation in range(1, args.generations + 1):
                for role, pool, rivals, space in [('player', player_pool, enemy_pool, PLAYER),
                                                 ('enemy', enemy_pool, player_pool, ENEMY)]:
                    rng = random.Random(args.seed + generation * 1009 + (role == 'enemy'))
                    rival_set = opponents(rivals, rng)
                    cases = [(1 + i % 2, 1000 + generation * 16 + i) for i in range(4)]
                    validation = [(1 + i % 2, 100000 + generation * 16 + i) for i in range(4)]
                    incumbent = tuple(pool[-1])
                    candidates = [incumbent]
                    candidates += [mutate(incumbent, space, rng) for _ in range(args.population - 1)]
                    fitness = [evaluator.fitness(role, c, rival_set, cases, 'train') for c in candidates]
                    best = candidates[max(range(len(candidates)), key=lambda i: fitness[i])]
                    gain = (evaluator.fitness(role, best, rival_set, validation, 'validation')
                            - evaluator.fitness(role, incumbent, rival_set, validation, 'validation'))
                    accepted = best != incumbent and gain > .005
                    if accepted: pool.append(best)
                    record = {'generation': generation, 'role': role, 'validation_gain': gain,
                              'accepted': accepted, 'policy': list(pool[-1])}
                    history.append(record)
                    print(f'Generation {generation:02} {role:6}: validation gain {gain:+.3f}; '
                          f'{"promoted" if accepted else "retained"}; {len(evaluator.cache)} matches', flush=True)
                save(output / 'checkpoint.json', {'version': 1, 'fingerprint': code_hash, 'config': config,
                    'generation': generation, 'player_pool': player_pool, 'enemy_pool': enemy_pool, 'history': history})
            # Held-out data is evaluated only after all selection is finished.
            cases = [(map_id, 1000000 + map_id * 10000 + i) for map_id in [1, 2, 3]
                     for i in range(args.evaluation_seeds)]
            ps = {'initial': BASE_PLAYER, 'middle': player_pool[len(player_pool) // 2], 'final': player_pool[-1]}
            es = {'initial': BASE_ENEMY, 'middle': enemy_pool[len(enemy_pool) // 2], 'final': enemy_pool[-1]}
            raw, matrix = {}, {}
            for pn, p in ps.items():
                for en, e in es.items():
                    key = pn + ' / ' + en
                    raw[key] = [evaluator.match(p, e, case, 'evaluation') for case in cases]
                    matrix[key] = aggregate(raw[key])
                    print(f'Evaluation {key}: {matrix[key]}', flush=True)
            base = raw['initial / initial']
            improvements = {}
            unseen = {}
            for role, key, sign in [('player', 'final / initial', 1), ('enemy', 'initial / final', -1)]:
                improvements[role] = paired_interval([sign * (r['score'] - b['score'])
                    for r, b in zip(raw[key], base)], args.seed)
                unseen[role] = paired_interval([sign * (r['score'] - b['score'])
                    for r, b in zip(raw[key], base) if r['map'] == 3], args.seed)
            result = {'version': 1, 'fingerprint': code_hash, 'config': config,
                'matches': len(evaluator.cache), 'audit_matches': 3,
                'elapsed_seconds': time.monotonic() - started,
                'player_parameters': [r[0] for r in PLAYER], 'enemy_parameters': [r[0] for r in ENEMY],
                'player_pool': player_pool, 'enemy_pool': enemy_pool, 'history': history,
                'evaluation': {'cases': cases, 'matrix': matrix, 'improvement': improvements,
                               'unseen_map_improvement': unseen}}
            save(output / 'summary.json', result)
            report(output, result)
            print(json.dumps({'improvement': improvements, 'unseen_map_improvement': unseen}, indent=2))
            print(f'Report: {output / "report.html"}', flush=True)
        finally:
            evaluator.log.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generations', type=int, default=8)
    parser.add_argument('--population', type=int, default=10)
    parser.add_argument('--seconds', type=int, default=20)
    parser.add_argument('--evaluation-seeds', type=int, default=16, help='Held-out seeds per map')
    parser.add_argument('--seed', type=int, default=20260905)
    parser.add_argument('--output', default='.cache/selfplay-run')
    args = parser.parse_args()
    if args.generations < 1 or args.population < 2 or not 1 <= args.seconds <= 100 or args.evaluation_seeds < 2:
        parser.error('Use positive generations, population >= 2, 1–100 seconds, and >= 2 evaluation seeds.')
    run(args)
