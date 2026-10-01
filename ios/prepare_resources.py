#!/usr/bin/env python3
"""Prepare the app's GameResources folder: the pattern library and the shader cache.

A prepare build of gol_check (GOL_PREPARE=ON, links Slang and SPIRV-Cross)
renders the game once and writes every shader it compiles. A replay build
(GOL_PREPARE=OFF, like the app) then runs against an empty shader folder; each
shader it reports missing is copied from the prepared cache until the game
renders. Both the Slang SPIR-V and the Metal entries it reads are kept, so the
output also serves as the source of a HarmonyOS bundle.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--prepare', type=Path, required=True, help='Prepare build of gol_check')
parser.add_argument('--checker', type=Path, required=True, help='Replay build of gol_check')
parser.add_argument('--patterns', type=Path, default=ROOT / 'LongMarch/demo/gol/patterns')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if args.output.exists():
    parser.error('output already exists; choose a new output directory or remove the generated directory first')
args.output.parent.mkdir(parents=True, exist_ok=True)


def run(checker: Path, resources: Path, output: Path, *extra: str) -> subprocess.CompletedProcess:
    return subprocess.run([str(checker.resolve()), str(resources), str(output), *extra],
                          capture_output=True, text=True)


with tempfile.TemporaryDirectory(prefix='game-bundle-', dir=args.output.parent) as temp:
    prepared = Path(temp) / 'prepared'
    (prepared / 'shaders').mkdir(parents=True)
    result = run(args.prepare, prepared, Path(temp) / 'prepare-check', 'prepare')
    if result.returncode != 0:
        raise RuntimeError(f'Shader preparation failed:\n{result.stdout}{result.stderr}')
    staging = Path(temp) / 'Resources'
    (staging / 'shaders').mkdir(parents=True)
    shutil.copytree(args.patterns, staging / 'Patterns')
    while True:
        result = run(args.checker, staging, Path(temp) / 'check')
        missing = re.search(r'Missing bundled shader ((?:slang|msl)-[0-9a-f]{64})', result.stdout + result.stderr)
        if missing is None:
            if result.returncode != 0:
                raise RuntimeError(f'Game of Life failed:\n{result.stdout}{result.stderr}')
            break
        source = prepared / 'shaders' / missing.group(1)
        if not source.exists():
            raise RuntimeError(f'The prepare build did not produce {missing.group(1)}')
        shutil.copy2(source, staging / 'shaders')
    staging.rename(args.output)
print(f'Prepared Game of Life with {len(list((args.output / "shaders").iterdir()))} shaders in {args.output}')
