#!/usr/bin/env python3
"""Compare examples/webpcodec, written against import mcpplibs.mwrap.libwebp,
with upstream's own cwebp and dwebp built from the same release.

The release tarball is downloaded once into target/upstream and checked
against upstream/documents.json. Its cwebp and dwebp are built as upstream's
makefile.unix builds them (with the system C compiler, threads on, without the
optional PNG/JPEG/TIFF readers); webpcodec is built with mcpp over
compat.libwebp. Then, for each input (upstream's examples/test_ref.ppm, an
opaque picture and one with every kind of alpha, generated here) and each
setting (lossy at several qualities and methods, lossless at several levels,
lossless with -exact):

- the two encoders must write the same bytes;
- the two decoders must write the same PPM or PAM from that file, with fancy
  upsampling and without;

and upstream's examples/test.webp must decode to the same pixels in both.
(Upstream's test_ref.ppm itself predates libwebp's current YUV to RGB
rounding: every libwebp release since decodes test.webp within 1 of it.)

Needs `make` and a C compiler: CI runs it on Linux and macOS.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from inventory import fetch  # noqa: E402

SETTINGS = [
    ['-q', '75', '-m', '4'],
    ['-q', '30', '-m', '0'],
    ['-q', '60', '-m', '2'],
    ['-q', '95', '-m', '6'],
    ['-lossless', '-q', '0', '-m', '0'],
    ['-lossless', '-q', '75', '-m', '4'],
    ['-lossless', '-q', '100', '-m', '6'],
    ['-lossless', '-exact', '-q', '50', '-m', '2'],
]


def upstream_tools(upstream: Path, cc: str) -> tuple[Path, Path]:
    build = upstream.parent / (upstream.name + '-build')
    if not (build / 'examples/cwebp').exists() or not (build / 'examples/dwebp').exists():
        shutil.rmtree(build, ignore_errors=True)
        shutil.copytree(upstream, build)
        command = ['make', '-s', f'-j{os.cpu_count() or 2}', '-f', 'makefile.unix', f'CC={cc}',
                   'EXTRA_FLAGS=-DWEBP_USE_THREAD', 'DWEBP_LIBS=', 'CWEBP_LIBS=', 'examples/cwebp', 'examples/dwebp']
        print('+', ' '.join(command), flush=True)
        subprocess.run(command, cwd=build, check=True)
    return build / 'examples/cwebp', build / 'examples/dwebp'


def webpcodec(example: Path, mcpp: str, toolchain: str | None, profile: str) -> Path:
    command = [mcpp, 'build', '--profile', profile] + (['--toolchain', toolchain] if toolchain else [])
    print('+', ' '.join(command), flush=True)
    subprocess.run(command, cwd=example, check=True)
    found = sorted((p for p in (example / 'target').rglob('webpcodec') if p.parent.name == 'bin' and p.is_file()),
                   key=lambda p: p.stat().st_mtime)
    if not found:
        sys.exit('examples/webpcodec was not built')
    return found[-1]


def inputs(directory: Path, upstream: Path) -> list[Path]:
    directory.mkdir(parents=True, exist_ok=True)
    w, h = 160, 120
    opaque, alpha = bytearray(), bytearray()
    state = 12345
    for y in range(h):
        for x in range(w):
            state = (state * 1664525 + 1013904223) & 0xffffffff
            noise = state >> 28
            disc = (x - 100) ** 2 + (y - 50) ** 2 < 30 ** 2
            rgb = (230 if disc else (x * 2 + noise) & 255, 40 if disc else (y * 2 + noise) & 255,
                   ((x ^ y) & 63) + (128 if x > 64 else 0))
            opaque += bytes(rgb)
            alpha += bytes(rgb) + bytes([0 if y < 16 else (y - 16) * 8 if y < 48 else 255])
    (directory / 'opaque.ppm').write_bytes(f'P6\n{w} {h}\n255\n'.encode() + opaque)
    (directory / 'alpha.pam').write_bytes(
        f'P7\nWIDTH {w}\nHEIGHT {h}\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n'.encode() + alpha)
    return [upstream / 'examples/test_ref.ppm', directory / 'opaque.ppm', directory / 'alpha.pam']


def run(command: list) -> None:
    result = subprocess.run([str(c) for c in command], capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f'{" ".join(map(str, command))} failed:\n{result.stdout}{result.stderr}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mcpp', default='mcpp')
    parser.add_argument('--toolchain')
    parser.add_argument('--profile', default='release')
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    upstream = fetch(root)
    cwebp, dwebp = upstream_tools(upstream, args.cc)
    ours = webpcodec(root / 'examples/webpcodec', args.mcpp, args.toolchain, args.profile)
    work = root / 'target/conformance'
    shutil.rmtree(work, ignore_errors=True)
    failures, checks = [], 0

    def compare(a: Path, b: Path, what: str) -> None:
        nonlocal checks
        checks += 1
        if a.read_bytes() != b.read_bytes():
            failures.append(what)
            print('DIFFERENT', what, flush=True)

    def decoded(webp: Path, extension: str, label: str) -> None:
        for flags in ([], ['-nofancy']):
            theirs, mine = work / f'{label}.dwebp.{extension}', work / f'{label}.ours.{extension}'
            run([dwebp, '-quiet', webp, *flags, f'-{extension}', '-o', theirs])
            run([ours, 'decode', webp, mine, *flags])
            compare(theirs, mine, f'decode {label} {" ".join(flags) or "(fancy)"}')

    for image in inputs(work, upstream):
        extension = 'pam' if image.suffix == '.pam' else 'ppm'
        for setting in SETTINGS:
            label = f'{image.stem} {" ".join(setting)}'
            theirs, mine = work / f'{label}.cwebp.webp', work / f'{label}.ours.webp'
            run([cwebp, '-quiet', *setting, image, '-o', theirs])
            run([ours, 'encode', image, mine, *setting])
            compare(theirs, mine, f'encode {label}')
            print(f'{label}: {theirs.stat().st_size} bytes', flush=True)
            decoded(theirs, extension, label)
    decoded(upstream / 'examples/test.webp', 'ppm', 'test.webp')
    decoded(upstream / 'examples/test.webp', 'pam', 'test.webp')
    if failures:
        sys.exit(f'{len(failures)} of {checks} comparisons differ from upstream cwebp/dwebp')
    print(f'{checks} comparisons: every encoded file and every decoded image is byte-identical to upstream '
          f'cwebp/dwebp 1.5.0 ({args.cc})')


if __name__ == '__main__':
    main()
