#!/usr/bin/env python3
"""Launch a durable, low-priority local training run; stdout goes to a log file."""
import argparse
from pathlib import Path
import subprocess
import sys
import time

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', default='.cache/recurrent-v4')
parser.add_argument('--minutes', type=float, default=60)
parser.add_argument('--workers', type=int, default=4)
parser.add_argument('--resume', action='store_true')
parser.add_argument('--continue-from')
parser.add_argument('--evaluation-base', type=int, default=920000000)
args = parser.parse_args()
output = (root / args.output).resolve()
# Keep the launcher log next to, rather than inside, the initially empty run.
output.parent.mkdir(parents=True, exist_ok=True)
log = output.with_suffix('.log')
command = ['nice', '-n', '10', sys.executable, '-u', '-m', 'training.cook',
           '--output', str(output), '--minutes', str(args.minutes), '--workers', str(args.workers)]
if args.resume: command.append('--resume')
if args.continue_from: command += ['--continue-from', args.continue_from]
command += ['--evaluation-base', str(args.evaluation_base)]
with log.open('ab') as stream:
    process = subprocess.Popen(command, cwd=root, stdin=subprocess.DEVNULL,
                               stdout=stream, stderr=stream, start_new_session=True)
time.sleep(1)
if process.poll() is not None:
    raise SystemExit(f'Trainer exited; inspect {log}')
print(f'Training PID: {process.pid}\nLog: {log}\nCheckpoints: {output}')
