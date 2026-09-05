#!/usr/bin/env python3
"""Compress a completed experiment's logs and record its local build environment."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path)
parser.add_argument('--omit-report-source', action='store_true', help='Report renderer was added after this run began')
args = parser.parse_args()
output = args.directory
log = output / 'matches.jsonl'
if log.exists():
    with log.open('rb') as source, gzip.open(output / 'matches.jsonl.gz', 'wb', compresslevel=9) as dest:
        shutil.copyfileobj(source, dest)
    log.unlink()
source_paths = [root / 'scripts/build_training.py']
for directory in ['training', 'src', 'vendor/doomgeneric/doomgeneric']:
    source_paths.extend(p for p in (root / directory).iterdir() if p.suffix in {'.c', '.h', '.py'})
if args.omit_report_source:
    source_paths = [p for p in source_paths if p != root / 'training/report.py']
source_paths = sorted(set(source_paths))
digest = hashlib.sha256()
for p in source_paths:
    digest.update(str(p.relative_to(root)).encode())
    digest.update(p.read_bytes())
digest.update((root / 'public/assets/doom1.wad').read_bytes())
source_fingerprint = digest.hexdigest()
record = next((output / name for name in ('checkpoint.json', 'frozen.json') if (output / name).exists()), None)
if record and json.loads(record.read_text())['fingerprint'] != source_fingerprint:
    raise SystemExit('Current sources differ from the run fingerprint; archive the actual run sources instead.')
source_hashes = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in source_paths}
with tarfile.open(output / 'source.tar.gz', 'w:gz') as archive:
    for p in source_paths: archive.add(p, arcname=str(p.relative_to(root)))
    # The makefile supplies the source list to the builder.
    p = root / 'vendor/doomgeneric/doomgeneric/Makefile.emscripten'
    archive.add(p, arcname=str(p.relative_to(root)))
metadata = {'platform': platform.platform(), 'machine': platform.machine(), 'python': sys.version,
            'compiler': subprocess.check_output([shutil.which('clang') or 'cc', '--version'], text=True),
            'source_fingerprint': source_fingerprint, 'source_sha256': source_hashes,
            'iwad_sha256': hashlib.sha256((root / 'public/assets/doom1.wad').read_bytes()).hexdigest(),
            'reproduction': 'Extract source.tar.gz over this repository checkout; the IWAD remains at public/assets/doom1.wad. Use the command arguments in checkpoint.json or frozen.json.'}
(output / 'environment.json').write_text(json.dumps(metadata, indent=2) + '\n')
print(output)
