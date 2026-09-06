"""Long-running recurrent self-play with varied encounters and resumable CPU ES."""
import argparse
import fcntl
from collections import OrderedDict
from concurrent.futures import ThreadPoolExecutor
import gzip
import json
import math
import os
from pathlib import Path
import random
import shutil
import subprocess
import tarfile
import platform
import sys
import hashlib
from datetime import datetime, timezone
import signal
import statistics
import threading
import time

from .selfplay import Arena, ROOT, ensure_engine, fingerprint, save, aggregate
from .league import identity
from .recurrent import WEIGHTS, upgrade, shifted, direction

PAUSE = False

def pause(*_):
    global PAUSE
    PAUSE = True


def tuples(value):
    return tuple(tuples(x) for x in value) if isinstance(value, list) else value


def references():
    old = json.loads((ROOT / 'training/results/parameter-v3b/checkpoint.json').read_text())
    neural = json.loads((ROOT / 'training/results/neural-v3/checkpoint.json').read_text())
    p, e = old['policies']['player'], neural['policies']['enemy']
    aggressive = {'params': [128, 40, 50, 50, 100, 12, 35, 2048, 384, 384], 'network': None}
    cautious = {'params': [320, 40, 40, 50, 100, 12, 100, 2048, 384, 384], 'network': None}
    return p, e, aggressive, cautious


class Runner:
    def __init__(self, output, workers):
        self.output = output
        self.binary = output / 'engine'
        self.local = threading.local()
        self.arenas = []
        self.pool = ThreadPoolExecutor(max_workers=workers)
        self.cache = OrderedDict()
        self.model_ids = set()
        self.models = gzip.open(output / 'models.jsonl.gz', 'at', compresslevel=1)
        self.log = gzip.open(output / 'matches.jsonl.gz', 'at', compresslevel=1)
        self.admission = gzip.open(output / 'admission.jsonl.gz', 'at', compresslevel=1)
        self.matches = 0

    def arena(self):
        if not hasattr(self.local, 'arena'):
            self.local.arena = Arena(self.binary)
            self.arenas.append(self.local.arena)
        return self.local.arena

    def register(self, policy):
        key = identity(policy)
        if key not in self.model_ids:
            self.model_ids.add(key)
            self.models.write(json.dumps({'id': key, 'policy': policy}, separators=(',', ':')) + '\n')
            self.models.flush()
        return key

    def worker(self, request):
        p, e, case, ticks = request
        m, seed, scenario = case
        return self.arena().play(p['params'], e['params'], seed, m, ticks=ticks,
                player_net=p['network'], enemy_net=e['network'], scenario=scenario)

    def admit(self, cases):
        p, e, _, _ = references()
        def check(case):
            try:
                self.worker((p, e, case, 1))
                return True
            except RuntimeError as exc:
                problem = exc.args[0]
                if not isinstance(problem, dict) or problem.get('error') != 'No valid combat spawn': raise
                return False
        accepted = []
        for case, valid in zip(cases, self.pool.map(check, cases)):
            self.admission.write(json.dumps({'case': case, 'valid': valid}) + '\n')
            if valid: accepted.append(case)
        self.admission.flush()
        return accepted

    def get(self, p, e, cases, phase):
        pi, ei = self.register(p), self.register(e)
        keys = [(pi, ei, *case) for case in cases]
        missing = [(key, case) for key, case in zip(keys, cases) if key not in self.cache]
        requests = [(p, e, case, 700 if case[2] == 0 else 1575) for _, case in missing]
        for (key, case), result in zip(missing, self.pool.map(self.worker, requests)):
            self.cache[key] = result
            self.log.write(json.dumps({'player': pi, 'enemy': ei, 'phase': phase,
                                       'curriculum': case[2], **result}, separators=(',', ':')) + '\n')
            self.matches += 1
        result = [self.cache[key] for key in keys]
        while len(self.cache) > 50000: self.cache.popitem(last=False)
        self.log.flush()
        return result

    def results(self, role, policy, rivals, cases, phase):
        return [r for rival in rivals for r in (self.get(policy, rival, cases, phase) if role == 'player'
                else self.get(rival, policy, cases, phase))]

    def score(self, role, policy, rivals, cases, phase):
        results = self.results(role, policy, rivals, cases, phase)
        return statistics.mean(r['score'] for r in results) * (1 if role == 'player' else -1)

    def close(self):
        self.pool.shutdown(wait=True)
        for arena in self.arenas: arena.close()
        self.models.close(); self.log.close(); self.admission.close()


def development(runner, base, generation, count):
    cases = []
    for i in range(count * 3):
        # Reserve enough cases so rejected layouts never reduce the batch size.
        slot = i % 10
        scenario = 0 if slot < 2 else 1 if slot < (7 if generation <= 8 else 5) else 2
        cases.append((1 + ((i // 10 + i % 10) % 5), base + generation * 10000 + i, scenario))
    admitted = runner.admit(cases)
    if len(admitted) < count: raise RuntimeError('Insufficient valid development layouts')
    return admitted[:count]


def compact_metrics(role, selected, reference):
    sign = 1 if role == 'player' else -1
    return {'matches': len(selected), 'wins': sum(r['result'] == sign for r in selected),
            'reference_wins': sum(r['result'] == sign for r in reference),
            'draws': sum(r['result'] == 0 for r in selected),
            'score_gain': statistics.mean(sign * (a['score'] - b['score']) for a, b in zip(selected, reference))}


def ci(values, rng):
    samples = sorted(sum(rng.choices(values, k=len(values))) / len(values) for _ in range(2000))
    return {'mean': statistics.mean(values), 'ci95': [samples[50], samples[1949]]}


def compare(role, selected, reference):
    sign, rng = (1 if role == 'player' else -1), random.Random(45692)
    return {'score': ci([sign * (a['score'] - b['score']) for a, b in zip(selected, reference)], rng),
            'win_rate': ci([int(a['result'] == sign) - int(b['result'] == sign) for a, b in zip(selected, reference)], rng)}


def compare_panel(selected, reference, styles):
    # Resample encounters, keeping the three player styles paired together.
    count = len(selected) // styles
    score = [sum(reference[s * count + i]['score'] - selected[s * count + i]['score']
                 for s in range(styles)) / styles for i in range(count)]
    wins = [sum(int(selected[s * count + i]['result'] == -1) - int(reference[s * count + i]['result'] == -1)
                for s in range(styles)) / styles for i in range(count)]
    rng = random.Random(45692)
    return {'score': ci(score, rng), 'win_rate': ci(wins, rng), 'clustered_by_encounter': True}


def snapshot(output, binary, code_hash):
    shutil.copy2(binary, output / 'engine')
    paths = [ROOT / 'scripts/build_training.py']
    for directory in ('training', 'src', 'vendor/doomgeneric/doomgeneric'):
        paths += [p for p in (ROOT / directory).iterdir() if p.suffix in ('.c', '.h', '.py')]
    with tarfile.open(output / 'source.tar.gz', 'w:gz') as archive:
        for path in paths:
            archive.add(path, arcname=str(path.relative_to(ROOT)))
        for name in ('vendor/doomgeneric/doomgeneric/Makefile.emscripten', 'training/COOK_PROTOCOL.md',
                     'training/results/parameter-v3b/checkpoint.json', 'training/results/neural-v3/checkpoint.json'):
            archive.add(ROOT / name, arcname=name)
    save(output / 'environment.json', {'fingerprint': code_hash, 'platform': platform.platform(),
         'python': sys.version, 'compiler': subprocess.check_output(['clang', '--version'], text=True),
         'engine_sha256': hashlib.sha256((output / 'engine').read_bytes()).hexdigest()})


def evaluate(runner, state, output, per_map, test_base):
    p, e, aggressive, cautious = references()
    np, ne = state['policies']['player'], state['policies']['enemy']
    save(output / 'frozen.json', {'policies': state['policies'], 'fingerprint': state['fingerprint'],
                                 'test_base': test_base, 'per_map_per_scenario': per_map})
    cells = {'baseline': (p, e), 'trained_player': (np, e), 'trained_enemy': (p, ne),
             'trained_both': (np, ne), 'aggressive_baseline': (aggressive, e),
             'aggressive_trained': (aggressive, ne), 'cautious_baseline': (cautious, e),
             'cautious_trained': (cautious, ne)}
    if 'parent' in state:
        parent = json.loads((output / 'parent-checkpoint.json').read_text())['policies']
        cells.update({'parent_player': (parent['player'], e),
                      'parent_enemy': (p, parent['enemy']),
                      'aggressive_parent': (aggressive, parent['enemy']),
                      'cautious_parent': (cautious, parent['enemy'])})
    summary = {}
    for split, maps in [('primary', (1, 2, 3, 4, 5)), ('unseen', (6, 7, 8))]:
        cases = []
        for m in maps:
            for scenario in (0, 1, 2):
                candidates = [(m, test_base + m * 100000 + scenario * 10000 + i, scenario) for i in range(per_map * 4)]
                admitted = runner.admit(candidates)
                if len(admitted) < per_map: raise RuntimeError('Insufficient final layouts')
                cases += admitted[:per_map]
        raw = {}
        for name, (player, enemy) in cells.items():
            raw[name] = runner.get(player, enemy, cases, 'evaluation-' + split)
            print('Evaluation', split, name, aggregate(raw[name]), flush=True)
        audited = 0
        with Arena(runner.binary) as serial:
            for name, (player, enemy) in cells.items():
                for case, expected in zip(cases, raw[name]):
                    if audited % 127 == 0: serial.play(p['params'], e['params'], 91, 1, ticks=1, player_net=p['network'], enemy_net=e['network'])
                    m, seed, mode = case
                    actual = serial.play(player['params'], enemy['params'], seed, m,
                            ticks=700 if mode == 0 else 1575, player_net=player['network'], enemy_net=enemy['network'], scenario=mode)
                    if actual != expected:
                        save(output / 'replay-failure.json', {'split': split, 'cell': name, 'expected': expected, 'actual': actual})
                        raise RuntimeError('Frozen evaluation failed serial replay')
                    audited += 1
        selected = raw['trained_enemy'] + raw['aggressive_trained'] + raw['cautious_trained']
        reference = raw['baseline'] + raw['aggressive_baseline'] + raw['cautious_baseline']
        summary[split] = {'cases': cases, 'serial_replay_matches': audited, 'serial_replay_passed': True, 'matrix': {k: aggregate(v) for k, v in raw.items()},
                          'player': compare('player', raw['trained_player'], raw['baseline']),
                          'enemy_panel': compare_panel(selected, reference, 3),
                          'enemy_by_style': {name: compare('enemy', raw[a], raw[b]) for name, a, b in [
                              ('reference', 'trained_enemy', 'baseline'),
                              ('aggressive', 'aggressive_trained', 'aggressive_baseline'),
                              ('cautious', 'cautious_trained', 'cautious_baseline')]},
                          'by_scenario': {str(mode): {name: aggregate([r for r, c in zip(rows, cases) if c[2] == mode])
                                                     for name, rows in raw.items()} for mode in (0, 1, 2)}}
        if 'parent' in state:
            parent_rows = raw['parent_enemy'] + raw['aggressive_parent'] + raw['cautious_parent']
            summary[split]['versus_parent'] = {
                'player': compare('player', raw['trained_player'], raw['parent_player']),
                'enemy_panel': compare_panel(selected, parent_rows, 3)}
        save(output / 'evaluation.json', summary)
    summary['enemy_primary_passed'] = all(summary['primary']['enemy_panel'][k]['ci95'][0] > 0 for k in ('score', 'win_rate'))
    save(output / 'evaluation.json', summary)


def publish_progress(output, state, phase, elapsed, matches):
    save(output / 'progress.json', {'status': state['status'], 'phase': phase,
         'generation': state['generation'], 'next_role': state['next_role'],
         'elapsed_seconds': elapsed, 'matches': matches,
         'remaining_training_seconds': max(0, state['config']['minutes'] * 60 - elapsed),
         'updated_utc': datetime.now(timezone.utc).isoformat(), 'history': state['history']})


def run(args):
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    run_lock = output.with_suffix('.lock').open('a+')
    try:
        fcntl.flock(run_lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        raise SystemExit('This experiment is already running')
    code_hash = fingerprint()
    if args.resume:
        state = json.loads((output / 'checkpoint.json').read_text())
        if state['status'] in ('finished', 'evaluating'): raise SystemExit('Final policies are frozen; choose a new experiment')
        if state['fingerprint'] != code_hash: raise SystemExit('Sources changed; restore the archived sources before resuming')
        rng = random.Random(); rng.setstate(tuples(state['rng']))
        # Configuration is frozen on resume, except the execution location and resume flag.
        for key, value in state['config'].items():
            if key not in ('output', 'resume'): setattr(args, key, value)
    elif args.continue_from:
        if any(output.iterdir()): raise SystemExit('Use an empty output directory')
        parent_path = Path(args.continue_from).resolve() / 'checkpoint.json'
        parent_data = parent_path.read_bytes()
        state = json.loads(parent_data)
        if state['status'] != 'finished': raise SystemExit('Continuation requires a finished parent experiment')
        used = state.get('evaluation_bases', []) + [state['config']['evaluation_base']]
        if any(abs(args.evaluation_base - base) < 1000000 for base in used):
            raise SystemExit('Continuation requires a fresh evaluation seed family')
        # Preserve optimizer, RNG, opponent history and generation cursor. Only
        # execution budgets and the held-out seed family change in this new run.
        for key, value in state['config'].items():
            if key not in ('output', 'resume', 'continue_from', 'minutes', 'workers',
                           'generations', 'evaluation_base'):
                setattr(args, key, value)
        rng = random.Random(); rng.setstate(tuples(state['rng']))
        (output / 'parent-checkpoint.json').write_bytes(parent_data)
        state['parent'] = {'path': str(parent_path), 'sha256': hashlib.sha256(parent_data).hexdigest(),
                           'fingerprint': state['fingerprint'], 'matches': state['matches'],
                           'elapsed_seconds': state['elapsed_seconds'], 'generation': state['generation']}
        state.update(fingerprint=code_hash, config=vars(args).copy(), elapsed_seconds=0.,
                     matches=0, history=[], evaluation_bases=used)
    else:
        if any(output.iterdir()): raise SystemExit('Use an empty output directory')
        p, e, _, _ = references()
        rng = random.Random(args.seed)
        state = {'version': 4, 'fingerprint': code_hash, 'config': vars(args).copy(), 'status': 'training',
                 'generation': 1, 'next_role': 'player', 'elapsed_seconds': 0., 'matches': 0, 'history': [],
                 'policies': {'player': upgrade(p, args.seed), 'enemy': upgrade(e, args.seed + 1)},
                 'pools': {'player': [], 'enemy': []},
                 'adam': {r: {'m': [0.] * WEIGHTS, 'v': [0.] * WEIGHTS, 't': 0} for r in ('player', 'enemy')}}
    binary = ensure_engine()
    if not args.resume: snapshot(output, binary, code_hash)
    state['status'] = 'training'
    state['rng'] = rng.getstate()
    save(output / 'checkpoint.json', state)
    (output / 'pid').write_text(str(os.getpid()) + '\n')
    publish_progress(output, state, 'starting', state['elapsed_seconds'], state['matches'])
    runner = Runner(output, args.workers)
    started, prior_elapsed, prior_matches = time.monotonic(), state['elapsed_seconds'], state['matches']
    p, e, aggressive, cautious = references()
    try:
        while state['generation'] <= args.generations and not PAUSE:
            if prior_elapsed + time.monotonic() - started >= args.minutes * 60 or (output / 'STOP').exists(): break
            generation, role = state['generation'], state['next_role']
            other = 'enemy' if role == 'player' else 'player'
            anchors = [e] if role == 'player' else [p, aggressive, cautious]
            rivals = [anchors[0], state['policies'][other]]
            if role == 'enemy': rivals.append(anchors[1 + generation % 2])
            if state['pools'][other]: rivals.append(rng.choice(state['pools'][other]))
            rivals = list({identity(r): r for r in rivals}.values())
            incumbent = state['policies'][role]
            training = development(runner, 100000000, generation, args.screen_cases)
            directions, differences, candidates = [], [], []
            for pair in range(args.pairs):
                noise = direction(rng)
                plus, minus = shifted(incumbent, noise, args.sigma), shifted(incumbent, noise, -args.sigma)
                a = runner.score(role, plus, rivals, training, 'es-selection')
                b = runner.score(role, minus, rivals, training, 'es-selection')
                directions.append(noise); differences.append(a - b)
                candidates.extend([(a, plus), (b, minus)])
                if (pair + 1) % 4 == 0:
                    publish_progress(output, state, 'search', prior_elapsed + time.monotonic() - started, prior_matches + runner.matches)
                    print(f'Generation {generation} {role}: {pair + 1}/{args.pairs} perturbation pairs; {prior_matches + runner.matches:,} matches', flush=True)
            deviation = max(statistics.pstdev([score for score, _ in candidates]), .02)
            gradient = [sum(d * noise[i] for d, noise in zip(differences, directions)) /
                        (2 * args.pairs * args.sigma * deviation) for i in range(WEIGHTS)]
            adam = state['adam'][role]
            adam['t'] += 1
            step = []
            for i, g in enumerate(gradient):
                adam['m'][i] = .9 * adam['m'][i] + .1 * g
                adam['v'][i] = .999 * adam['v'][i] + .001 * g * g
                step.append((adam['m'][i] / (1 - .9 ** adam['t'])) /
                            (math.sqrt(adam['v'][i] / (1 - .999 ** adam['t'])) + 1e-8))
            proposal = shifted(incumbent, step, args.learning_rate)
            finalists = [incumbent, proposal] + [c for _, c in sorted(candidates, key=lambda x: x[0], reverse=True)[:3]]
            selection = development(runner, 120000000, generation, args.selection_cases)
            scored = [(runner.score(role, c, rivals, selection, 'finalist-selection'), c) for c in finalists]
            _, best = max(scored, key=lambda x: x[0])
            validation = development(runner, 500000000, generation, args.validation_cases)
            gain = runner.score(role, best, rivals, validation, 'validation') - runner.score(role, incumbent, rivals, validation, 'validation')
            a = runner.results(role, best, anchors, validation, 'validation-anchor')
            b = runner.results(role, incumbent, anchors, validation, 'validation-anchor')
            metrics = compact_metrics(role, a, b)
            accepted = identity(best) != identity(incumbent) and gain > .005 and metrics['score_gain'] >= 0 and metrics['wins'] >= metrics['reference_wins']
            if accepted:
                state['pools'][role].append(incumbent)
                state['pools'][role] = state['pools'][role][-8:]
                state['policies'][role] = best
            record = {'generation': generation, 'role': role, 'accepted': accepted, 'league_gain': gain,
                      'anchor': metrics, 'candidate': runner.register(best)}
            state['history'].append(record)
            print(json.dumps(record), flush=True)
            state['next_role'] = 'enemy' if role == 'player' else 'player'
            if role == 'enemy': state['generation'] += 1
            state['elapsed_seconds'] = prior_elapsed + time.monotonic() - started
            state['matches'] = prior_matches + runner.matches
            state['rng'] = rng.getstate()
            save(output / 'checkpoint.json', state)
            publish_progress(output, state, 'update complete', state['elapsed_seconds'], state['matches'])
        state['status'] = 'paused' if PAUSE else 'evaluating'
        publish_progress(output, state, state['status'], prior_elapsed + time.monotonic() - started, prior_matches + runner.matches)
        save(output / 'checkpoint.json', state)
        if not PAUSE:
            evaluate(runner, state, output, args.evaluation_cases, args.evaluation_base)
            state['status'] = 'finished'
        state['elapsed_seconds'] = prior_elapsed + time.monotonic() - started
        state['matches'] = prior_matches + runner.matches
        save(output / 'checkpoint.json', state)
        publish_progress(output, state, state['status'], state['elapsed_seconds'], state['matches'])
    except BaseException as exc:
        state['status'] = 'error'
        publish_progress(output, state, str(exc), prior_elapsed + time.monotonic() - started, prior_matches + runner.matches)
        save(output / 'error.json', {'type': type(exc).__name__, 'error': str(exc)})
        raise
    finally:
        runner.close()
        run_lock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    parser.add_argument('--resume', action='store_true')
    parser.add_argument('--continue-from', help='Start a new experiment from a finished run, retaining optimizer and RNG')
    parser.add_argument('--minutes', type=float, default=60)
    parser.add_argument('--workers', type=int, default=4)
    parser.add_argument('--generations', type=int, default=1000)
    parser.add_argument('--pairs', type=int, default=12)
    parser.add_argument('--screen-cases', type=int, default=50)
    parser.add_argument('--selection-cases', type=int, default=150)
    parser.add_argument('--validation-cases', type=int, default=250)
    parser.add_argument('--evaluation-cases', type=int, default=32)
    parser.add_argument('--evaluation-base', type=int, default=920000000)
    parser.add_argument('--sigma', type=float, default=.06)
    parser.add_argument('--learning-rate', type=float, default=.025)
    parser.add_argument('--seed', type=int, default=20260907)
    args = parser.parse_args()
    if args.resume and args.continue_from: parser.error('Choose resume or continue-from')
    if min(args.minutes, args.workers, args.generations, args.pairs, args.screen_cases,
           args.selection_cases, args.validation_cases, args.evaluation_cases, args.sigma, args.learning_rate) <= 0:
        parser.error('Budgets and optimizer settings must be positive')
    if not 1000000 <= args.evaluation_base <= 4000000000: parser.error('Evaluation seed base is outside the supported range')
    signal.signal(signal.SIGTERM, pause); signal.signal(signal.SIGINT, pause)
    run(args)

if __name__ == '__main__': main()
