#!/usr/bin/env python3
"""Stage Game of Life's patterns and SPIR-V shaders into the HarmonyOS rawfile bundle.

The source is a prepared iOS game resource folder (Patterns/ and shaders/). The
iOS preparation records Slang's Vulkan 1.2 SPIR-V (slang-* cache entries) before
converting it to MSL; both backends issue the same Slang requests, so only the
slang-* entries are copied. The app verifies every file against manifest.json.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile


def stage(source: Path, output: Path):
    source = source.resolve()
    output = output.resolve()
    if output.exists():
        raise ValueError('Output exists; remove it or choose a fresh output directory')
    if not (source / 'Patterns' / 'library.json').is_file():
        raise ValueError('No Patterns/library.json in the source')
    shaders = sorted((source / 'shaders').glob('slang-*'))
    if not shaders:
        raise ValueError('No SPIR-V shaders; prepare the game resources first')
    for shader in shaders:
        data = shader.read_bytes()
        payload = data[64:]
        digest = hashlib.sha256(str(len(payload)).encode() + b':' + payload).hexdigest().encode()
        if data[:64] != digest or payload[:4] != b'\x03\x02\x23\x07':
            raise ValueError(f'Invalid SPIR-V cache: {shader.name}')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent, prefix='harmony-bundle-') as temp:
        bundle = Path(temp) / 'Resources'
        bundle.mkdir()
        shutil.copytree(source / 'Patterns', bundle / 'Patterns')
        (bundle / 'shaders').mkdir()
        for shader in shaders:
            shutil.copy2(shader, bundle / 'shaders' / shader.name)
        files = []
        for path in sorted(bundle.rglob('*')):
            if path.is_file():
                data = path.read_bytes()
                files.append({'path': path.relative_to(bundle).as_posix(),
                              'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        (bundle / 'manifest.json').write_text(json.dumps({'version': 1, 'files': files}, indent=2) + '\n')
        bundle.rename(output)
    print(f'Staged {len(shaders)} SPIR-V shaders, {len(files)} files in {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path, help='Prepared game resources (Patterns/, shaders/)')
    parser.add_argument('--output', type=Path,
                        default=Path(__file__).resolve().parent / 'entry/src/main/resources/rawfile/Resources')
    args = parser.parse_args()
    stage(args.source, args.output)
