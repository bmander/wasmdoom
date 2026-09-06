"""Behavioral checks for recurrent warm starts, richer scenarios and durable runs."""
import gzip
import json
from pathlib import Path
import random
import signal
import subprocess
import tempfile
import time
import unittest

from training.selfplay import Arena, ROOT, ensure_engine
from training.cook import references
from training.recurrent import upgrade, shifted, direction, WEIGHTS


class RecurrentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binary = ensure_engine()

    def test_warm_start_keeps_the_parent_behavior_in_every_curriculum(self):
        p, e, _, _ = references()
        np, ne = upgrade(p, 1), upgrade(e, 2)
        self.assertEqual(len(ne['network']), WEIGHTS)
        with Arena(self.binary) as arena:
            for mode in (0, 1, 2):
                for map_id in (1, 3, 5):
                    seed = 71000000 + map_id
                    base = arena.play(p['params'], e['params'], seed, map_id, player_net=p['network'], enemy_net=e['network'], scenario=mode)
                    new = arena.play(np['params'], ne['params'], seed, map_id, player_net=np['network'], enemy_net=ne['network'], scenario=mode)
                    self.assertEqual(base, new)

    def test_scenarios_vary_rooms_loadouts_and_occlusion_without_policy_dependent_spawns(self):
        p, e, _, _ = references()
        np = shifted(upgrade(p, 1), direction(random.Random(3)), .1)
        ne = shifted(upgrade(e, 2), direction(random.Random(4)), .1)
        rows = []
        with Arena(self.binary) as arena:
            for mode in (1, 2):
                for i in range(8):
                    seed, map_id = 72000000 + i, 1 + i % 5
                    first = arena.play(p['params'], e['params'], seed, map_id, ticks=350, player_net=p['network'], enemy_net=e['network'], scenario=mode)
                    changed = arena.play(np['params'], ne['params'], seed, map_id, ticks=350, player_net=np['network'], enemy_net=ne['network'], scenario=mode)
                    replay = arena.play(p['params'], e['params'], seed, map_id, ticks=350, player_net=p['network'], enemy_net=e['network'], scenario=mode)
                    self.assertEqual(first, replay)
                    for key in ('enemy_initial_health', 'enemies', 'weapon', 'initial_visible', 'spawn_x', 'spawn_y'):
                        self.assertEqual(first[key], changed[key])
                    rows.append(first)
            expected = arena.play(np['params'], ne['params'], 72000001, 2, player_net=np['network'], enemy_net=ne['network'], scenario=2)
            arena.play(p['params'], e['params'], 91, 1, ticks=1)
            self.assertEqual(expected, arena.play(np['params'], ne['params'], 72000001, 2, player_net=np['network'], enemy_net=ne['network'], scenario=2))
        self.assertEqual({r['weapon'] for r in rows}, {1, 2, 3})
        self.assertTrue(any(r['initial_visible'] < r['enemies'] for r in rows))
        self.assertTrue(any(r['enemies'] == 7 for r in rows))
        self.assertGreater(len({(r['spawn_x'], r['spawn_y']) for r in rows}), 5)

    def test_long_run_can_pause_resume_and_finish_with_frozen_evaluation(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '.cache') as directory:
            path = Path(directory) / 'run'
            command = ['python3', '-m', 'training.cook', '--output', str(path), '--minutes', '5',
                       '--evaluation-base', '850000000', '--generations', '1', '--pairs', '2', '--screen-cases', '5',
                       '--selection-cases', '5', '--validation-cases', '5', '--evaluation-cases', '1', '--workers', '2']
            log = Path(directory) / 'log'
            with log.open('w') as stream:
                process = subprocess.Popen(command, cwd=ROOT, stdout=stream, stderr=stream)
                deadline = time.monotonic() + 30
                while not (path / 'pid').exists() and time.monotonic() < deadline and process.poll() is None:
                    time.sleep(.02)
                self.assertTrue((path / 'pid').exists(), log.read_text())
                duplicate = subprocess.run(command + ['--resume'], cwd=ROOT, capture_output=True, text=True, timeout=30)
                self.assertNotEqual(duplicate.returncode, 0)
                self.assertIn('already running', duplicate.stderr)
                process.send_signal(signal.SIGTERM)
                self.assertEqual(process.wait(timeout=120), 0, log.read_text())
            paused = json.loads((path / 'checkpoint.json').read_text())
            self.assertEqual(paused['status'], 'paused')
            self.assertFalse((path / 'frozen.json').exists())
            subprocess.run(command + ['--resume'], cwd=ROOT, capture_output=True, check=True, timeout=180)
            final = json.loads((path / 'checkpoint.json').read_text())
            self.assertEqual(final['status'], 'finished')
            self.assertEqual(len(final['history']), 2)
            self.assertTrue((path / 'source.tar.gz').exists())
            self.assertTrue((path / 'engine').exists())
            evaluation = json.loads((path / 'evaluation.json').read_text())
            self.assertEqual(len(evaluation['primary']['matrix']), 8)
            self.assertEqual(len(evaluation['unseen']['matrix']), 8)
            self.assertTrue(evaluation['primary']['enemy_panel']['clustered_by_encounter'])
            rows = list(map(json.loads, gzip.open(path / 'matches.jsonl.gz', 'rt')))
            development = {(r['map'], r['seed']) for r in rows if not r['phase'].startswith('evaluation')}
            heldout = {(r['map'], r['seed']) for r in rows if r['phase'].startswith('evaluation')}
            self.assertFalse(development & heldout)
            self.assertTrue(all(m <= 5 for m, _ in development))
            self.assertTrue(any(m > 5 for m, _ in heldout))
            refusal = subprocess.run(command + ['--resume'], cwd=ROOT, capture_output=True, text=True)
            self.assertNotEqual(refusal.returncode, 0)
            self.assertIn('frozen', refusal.stderr)
            # Finished runs stay frozen; continuation starts in a new directory
            # with identical learned state and fresh held-out encounters.
            child = Path(directory) / 'continued'
            fork = ['python3', '-m', 'training.cook', '--output', str(child),
                    '--continue-from', str(path), '--minutes', '0.0000001', '--workers', '2']
            reused = subprocess.run(fork + ['--evaluation-base', '850000000'], cwd=ROOT,
                                    capture_output=True, text=True, timeout=30)
            self.assertNotEqual(reused.returncode, 0)
            self.assertIn('fresh evaluation', reused.stderr)
            subprocess.run(fork + ['--evaluation-base', '860000000'], cwd=ROOT,
                           capture_output=True, check=True, timeout=180)
            continued = json.loads((child / 'checkpoint.json').read_text())
            for key in ('policies', 'pools', 'adam', 'rng', 'generation', 'next_role'):
                self.assertEqual(continued[key], final[key], key)
            self.assertEqual(continued['history'], [])
            self.assertEqual(continued['status'], 'finished')
            self.assertEqual(json.loads((path / 'checkpoint.json').read_text()), final)
            child_eval = json.loads((child / 'evaluation.json').read_text())
            for split in ('primary', 'unseen'):
                self.assertEqual(len(child_eval[split]['matrix']), 12)
                self.assertTrue(child_eval[split]['serial_replay_passed'])
                self.assertEqual(child_eval[split]['versus_parent']['enemy_panel']['score']['mean'], 0)


if __name__ == '__main__': unittest.main()
