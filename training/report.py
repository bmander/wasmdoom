"""Render a standalone, honest report from a frozen evaluation's summary."""
import argparse
import html
import json
from pathlib import Path


def estimate(value, percent=False):
    scale, unit = (100, ' pp') if percent else (1, '')
    lo, hi = value['ci975']
    return f"{value['mean'] * scale:+.3f}{unit} [{lo * scale:+.3f}, {hi * scale:+.3f}]"


def render(directory):
    result = json.loads((directory / 'summary.json').read_text())
    content = '''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width"><title>DOOM neural policy experiment</title>
<style>body{font:17px/1.6 system-ui;max-width:1100px;margin:40px auto;padding:0 24px;background:#141815;color:#e6e8dc}h1,h2,strong{color:#dec77d}h1{font-size:38px}h2{margin-top:2em;font-size:25px}a{color:#dec77d}table{border-collapse:collapse;width:100%;font-size:15px}td,th{text-align:left;border-bottom:1px solid #465147;padding:10px}code{overflow-wrap:anywhere;font-size:12px}.scroll{overflow-x:auto}.note{background:#222a24;padding:20px;border-left:4px solid #dec77d}</style>
<h1>DOOM: learning better adversaries</h1>
<p>Parameter controllers followed by two 149-weight neural networks, trained through CPU mutation and selection in the real DOOM engine. Networks choose tactics from structured combat observations; navigation, aim execution, and cover mechanics are provided by the existing controllers.</p>'''
    conclusion = ('Both neural policies passed the predeclared primary criterion: higher win rate and score against the same original opponents, with positive lower bounds in paired 97.5% bootstrap intervals.'
                  if result['success'] else 'The neural pair did not pass every predeclared primary criterion. Inspect both sides and uncertainty below; a positive point estimate alone is insufficient.')
    content += f'<p class="note">{conclusion}</p>'
    for split, label in [('primary', 'Fresh encounters: E1M1–E1M3'), ('unseen_maps', 'Unseen geometry: E1M4–E1M5')]:
        data = result['evaluation'][split]
        content += f'<h2>{label}</h2><div class="scroll"><table><tr><th>Player / enemy</th><th>Matches</th><th>Player wins</th><th>Enemy wins</th><th>Draws</th><th>Player score</th></tr>'
        for name, cell in data['matrix'].items():
            content += f'<tr><td>{html.escape(name)}</td><td>{cell["matches"]}</td><td>{cell["player_wins"]} ({100*cell["player_wins"]/cell["matches"]:.1f}%)</td><td>{cell["enemy_wins"]} ({100*cell["enemy_wins"]/cell["matches"]:.1f}%)</td><td>{cell["draws"]}</td><td>{cell["player_score"]:.3f}</td></tr>'
        content += '</table></div><p>Differences below favor the named side when positive. Brackets give paired 97.5% bootstrap intervals; pp means percentage points.</p><div class="scroll"><table><tr><th>Side</th><th>Comparison</th><th>Win-rate difference</th><th>Score difference</th></tr>'
        for role, comparisons in data['improvement'].items():
            for comparison, metrics in comparisons.items():
                content += f'<tr><td>{role.title()}</td><td>{html.escape(comparison.replace("_", " "))}</td><td>{estimate(metrics["win_rate"], True)}</td><td>{estimate(metrics["score"])}</td></tr>'
        content += '</table></div>'
    content += '''<h2>What this establishes</h2><p>Gains over the original opponent measure improvement from the full training pipeline. Neural-versus-parameter results isolate the additional training stage, but do not control for its extra compute. The constant-network ablation removes input connections while preserving learned biases and output weights; neural-versus-constant results test whether state-dependent decisions help this frozen model.</p>
<p>Each drill starts with a pistol player and two or three normal monsters around a map start. A player win kills all enemies; an enemy win kills the player; a 20-second timeout is a draw. Scores add a small damage/health term to the +1/−1/0 outcome. Health, damage, movement limits, and minimum attack delay stay unchanged. Final cases never select policies. These results cover short combat drills, not full levels or human opponents.</p>
<p>Browser behavior is unchanged. Checkpoints remain offline artifacts for future watch mode.</p>
<h2>Audit files</h2><p><a href="summary.json">All metrics</a> · <a href="frozen.json">Frozen policies and training history</a> · <a href="models.json">Model dictionary</a> · <a href="matches.jsonl.gz">Compressed raw matches</a></p>'''
    content += f'<p>{result["matches"]:,} distinct evaluation matches · {result["elapsed_seconds"]:.1f} seconds · seed family {result["config"]["base"]:,}</p><p>Source/IWAD fingerprint: <code>{result["fingerprint"]}</code></p></html>'
    (directory / 'report.html').write_text(content)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    render(parser.parse_args().directory)
