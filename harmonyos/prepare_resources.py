#!/usr/bin/env python3
"""Stage Game of Life's patterns and SPIR-V shaders into the HarmonyOS rawfile bundle.

Patterns are copied from the source resource folder. Vulkan shaders are independently
prepared and replayed for OHOS by the host compiler using the App shader recipes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import subprocess


def stage(source: Path, output: Path, compiler: Path, checker: Path):
    source = source.resolve()
    output = output.resolve()
    if output.exists():
        raise ValueError('Output exists; remove it or choose a fresh output directory')
    if not (source / 'Patterns' / 'library.json').is_file():
        raise ValueError('No Patterns/library.json in the source')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent, prefix='harmony-bundle-') as temp:
        bundle = Path(temp) / 'Resources'
        bundle.mkdir()
        shutil.copytree(source / 'Patterns', bundle / 'Patterns')
        cache = bundle / 'shaders'
        game = Path(__file__).resolve().parents[1] / 'game'
        subprocess.run([str(compiler.resolve()), str(game), str(cache), 'prepare'], check=True)
        subprocess.run([str(checker.resolve()), str(game), str(cache), 'replay'], check=True)
        shaders = sorted(p for p in cache.iterdir() if p.name.startswith(('slang-', 'pre-', 'threads-slang-')))
        if not shaders:
            raise ValueError('No SPIR-V shaders; prepare the game resources first')
        for shader in shaders:
            data = shader.read_bytes()
            payload = data[64:]
            digest = hashlib.sha256(str(len(payload)).encode() + b':' + payload).hexdigest().encode()
            if data[:64] != digest or (shader.name.startswith('slang-') and payload[:4] != b'\x03\x02\x23\x07'):
                raise ValueError(f'Invalid SPIR-V cache: {shader.name}')
        files = []
        for path in sorted(bundle.rglob('*')):
            if path.is_file():
                data = path.read_bytes()
                files.append({'path': path.relative_to(bundle).as_posix(),
                              'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        (bundle / 'manifest.json').write_text(json.dumps({'version': 1, 'target': 'ohos-vulkan', 'files': files}, indent=2) + '\n')
        bundle.rename(output)
    print(f'Staged {len(shaders)} SPIR-V shaders, {len(files)} files in {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path, help='Prepared game resources (Patterns/, shaders/)')
    parser.add_argument('--output', type=Path,
                        default=Path(__file__).resolve().parent / 'entry/src/main/resources/rawfile/Resources')
    parser.add_argument('--compiler', required=True, type=Path)
    parser.add_argument('--checker', required=True, type=Path)
    args = parser.parse_args()
    stage(args.source, args.output, args.compiler, args.checker)
