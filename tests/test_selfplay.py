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
            arena.play((400, 40, 50, 12, 100, 8, 25, 2048),
                       (160, 70, 140, 70, 16, 0, 60, 0), 23, 3)
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
        player_variant = (300, 40, 50, 50, 100, 8, 25, 2048)
        enemy_variant = (160, 70, 140, 70, 16, 0, 60, 0)
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

    def test_training_writes_auditable_policies_and_held_out_results(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '.cache') as directory:
            subprocess.run(['python3', '-m', 'training.selfplay', '--generations', '1',
                            '--population', '2', '--evaluation-seeds', '2', '--output', directory],
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
