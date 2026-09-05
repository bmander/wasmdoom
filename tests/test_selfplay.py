"""Integration checks for the training arena and end-to-end experiment."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from training.selfplay import Arena, BASE_PLAYER, BASE_ENEMY, ROOT, ensure_engine


class SelfPlayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binary = ensure_engine()

    def test_match_reset_is_independent_of_previous_opponents_and_maps(self):
        with Arena(self.binary) as arena:
            first = arena.play(BASE_PLAYER, BASE_ENEMY, 12, 1)
            arena.play((400, 40, 50, 12, 100, 8, 25, 2048, *BASE_PLAYER[8:]),
                       (160, 70, 140, 70, 16, 0, 60, 0, 12, 0, *BASE_ENEMY[10:]), 23, 3)
            self.assertEqual(first, arena.play(BASE_PLAYER, BASE_ENEMY, 12, 1))
            self.assertEqual(first['result'], 1)
            self.assertEqual(first['enemy_health'], 0)

    def test_death_and_timeout_are_distinct_outcomes(self):
        with Arena(self.binary) as arena:
            loss = arena.play(BASE_PLAYER, BASE_ENEMY, 23, 3)
            draw = arena.play(BASE_PLAYER, BASE_ENEMY, 23, 3, ticks=1)
            self.assertEqual(loss['result'], -1)
            self.assertEqual(loss['player_health'], 0)
            self.assertEqual(draw['result'], 0)
            self.assertEqual(draw['ticks'], 1)

    def test_both_policies_change_combat_without_changing_enemy_loadouts(self):
        player_variant = (300, 40, 50, 50, 100, 8, 25, 2048, *BASE_PLAYER[8:])
        enemy_variant = (160, 70, 140, 70, 16, 0, 60, 0, 12, 0, *BASE_ENEMY[10:])
        with Arena(self.binary) as arena:
            base, player, enemy = [], [], []
            for seed in [12, 23, 34]:
                base.append(arena.play(BASE_PLAYER, BASE_ENEMY, seed, 1))
                player.append(arena.play(player_variant, BASE_ENEMY, seed, 1))
                enemy.append(arena.play(BASE_PLAYER, enemy_variant, seed, 1))
            self.assertNotEqual([r['score'] for r in base], [r['score'] for r in player])
            self.assertNotEqual([r['score'] for r in base], [r['score'] for r in enemy])
            self.assertEqual([r['enemy_initial_health'] for r in base],
                             [r['enemy_initial_health'] for r in enemy])
            with self.assertRaises(ValueError):
                arena.play((192, 120, *BASE_PLAYER[2:]), BASE_ENEMY, 12, 1)

    def test_crowded_layout_retries_before_combat(self):
        with Arena(self.binary) as arena:
            result = arena.play(BASE_PLAYER, BASE_ENEMY, 3540169, 3)
            self.assertEqual(result['enemies'], 3)
            arena.play(BASE_PLAYER, BASE_ENEMY, 12, 1)
            self.assertEqual(result, arena.play(BASE_PLAYER, BASE_ENEMY, 3540169, 3))

    def test_navigation_budget_cannot_leak_from_a_short_previous_episode(self):
        fixture = json.loads((ROOT / 'tests/fixtures/neural_episode_reset.json').read_text())
        p, e = fixture['player'], fixture['enemy']
        with Arena(self.binary) as arena:
            def replay():
                return arena.play(p['params'], e['params'], fixture['seed'], fixture['map'],
                                  player_net=p['network'], enemy_net=e['network'])
            first = replay()
            for _ in range(3):
                arena.play(BASE_PLAYER, BASE_ENEMY, 91, 1, ticks=1)
                self.assertEqual(first, replay())
            self.assertEqual(first['ticks'], 502)
            self.assertEqual(first['player_health'], 58)

    def test_evaluation_admission_logs_impossible_layouts_without_scoring_them(self):
        from training.evaluate import admit
        encounters, rejected = admit(self.binary, 9000000, 96, (4,))
        self.assertEqual(len(encounters), 96)
        self.assertEqual(len(set(encounters)), 96)
        self.assertNotIn((4, 9040095), encounters)
        self.assertIn({'map': 4, 'seed': 9040095, 'reason': 'No valid combat spawn'}, rejected)
        self.assertTrue(all(m == 4 for m, _ in encounters))

    def test_neural_connections_change_combat_and_reset_between_episodes(self):
        # A genuine state-dependent path: health -> hidden unit -> range/strafe.
        network = [0.] * 149
        network[1] = 2.
        network[105] = -2.
        network[114] = 2.
        neutral = [0.3] * 104 + [0.] * 45
        with Arena(self.binary) as arena:
            for seed in (12, 23, 34):
                base = arena.play(BASE_PLAYER, BASE_ENEMY, seed, 1)
                self.assertEqual(base, arena.play(BASE_PLAYER, BASE_ENEMY, seed, 1,
                                                 player_net=neutral, enemy_net=neutral))
            reference, changed = [], []
            for seed in (12, 23, 34):
                reference.append(arena.play(BASE_PLAYER, BASE_ENEMY, seed, 1))
                changed.append(arena.play(BASE_PLAYER, BASE_ENEMY, seed, 1,
                                          player_net=network, enemy_net=network))
                self.assertEqual(reference[-1], arena.play(BASE_PLAYER, BASE_ENEMY, seed, 1))
            self.assertNotEqual(reference, changed)
            self.assertEqual([r['enemy_initial_health'] for r in reference],
                             [r['enemy_initial_health'] for r in changed])
            for invalid in ([0.] * 148, [float('nan')] * 149, [4.01] * 149):
                with self.assertRaises(ValueError):
                    arena.play(BASE_PLAYER, BASE_ENEMY, 12, 1, player_net=invalid)

    def test_neural_league_and_frozen_evaluation_are_auditable(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '.cache') as directory:
            path = Path(directory)
            for kind in ('parameters', 'neural'):
                command = ['python3', '-m', 'training.league', '--output', str(path / kind),
                           '--generations', '1', '--population', '2', '--screen-cases', '1',
                           '--train-cases', '1', '--validation-cases', '1', '--workers', '2',
                           '--development-base', '70000000']
                if kind == 'neural':
                    command += ['--neural', '--initial', str(path / 'parameters/checkpoint.json')]
                subprocess.run(command, cwd=ROOT, check=True, capture_output=True, timeout=60)
            checkpoint = json.loads((path / 'neural/checkpoint.json').read_text())
            for policy in checkpoint['policies'].values():
                self.assertEqual(len(policy['network']), 149)
            subprocess.run(['python3', '-m', 'training.evaluate',
                            '--parameters', str(path / 'parameters/checkpoint.json'),
                            '--neural', str(path / 'neural/checkpoint.json'),
                            '--output', str(path / 'evaluation'), '--base', '80000000',
                            '--primary-cases', '1', '--unseen-cases', '1', '--workers', '2'],
                           cwd=ROOT, check=True, capture_output=True, timeout=60)
            summary = json.loads((path / 'evaluation/summary.json').read_text())
            self.assertEqual(len(summary['evaluation']['primary']['matrix']), 9)
            self.assertEqual(len(summary['evaluation']['unseen_maps']['matrix']), 9)
            self.assertTrue((path / 'evaluation/frozen.json').exists())
            records = [json.loads(line) for line in (path / 'evaluation/matches.jsonl').read_text().splitlines()]
            self.assertTrue(all(r['seed'] >= 80000000 for r in records))
            models = json.loads((path / 'evaluation/models.json').read_text())
            # Replaying a logged result in another engine process must agree.
            record = records[-1]
            with Arena(self.binary) as arena:
                p, e = models[record['player']], models[record['enemy']]
                replay = arena.play(p['params'], e['params'], record['seed'], record['map'],
                                    player_net=p['network'], enemy_net=e['network'])
            self.assertTrue(all(record[k] == v for k, v in replay.items()))

    def test_training_writes_auditable_policies_and_held_out_results(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '.cache') as directory:
            subprocess.run(['python3', '-m', 'training.selfplay', '--generations', '1',
                            '--population', '2', '--train-seeds', '2', '--validation-seeds', '2',
                            '--evaluation-seeds', '2', '--output', directory],
                           cwd=ROOT, check=True, capture_output=True, text=True, timeout=60)
            path = Path(directory)
            result = json.loads((path / 'summary.json').read_text())
            matches = [json.loads(line) for line in (path / 'matches.jsonl').read_text().splitlines()]
            training_cases = {(r['map'], r['seed']) for r in matches if r['phase'] != 'evaluation'}
            test_cases = {(r['map'], r['seed']) for r in matches if r['phase'] == 'evaluation'}
            self.assertFalse(training_cases & test_cases)
            self.assertNotIn(3, {map_id for map_id, _ in training_cases})
            self.assertIn(3, {map_id for map_id, _ in test_cases})
            self.assertEqual(len(result['history']), 2)
            self.assertEqual(set(result['evaluation']['improvement']), {'player', 'enemy'})
            self.assertTrue((path / 'checkpoint.json').exists())
            self.assertIn('Held-out matchups', (path / 'report.html').read_text())


if __name__ == '__main__':
    unittest.main()
