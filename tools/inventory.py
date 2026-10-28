#!/usr/bin/env python3
"""The public surface of libwebp's headers, from the pinned release.

Shared by tools/gen_exports.py (which writes the modules) and
tools/check_exports.py (which audits them against it). The headers come from
the upstream tarball, downloaded once into target/upstream and checked against
upstream/documents.json: the same bytes compat.libwebp builds.

The public headers are the ones upstream installs, grouped by the library
that implements them and so by the module that exports them:

- core (libwebp, always built): webp/types.h, webp/decode.h, webp/encode.h and
  webp/mux_types.h (WebPData and the animation enums; header-only, shared by
  demux and mux, so exported once, here);
- sharpyuv (in the same archive): sharpyuv/sharpyuv.h, sharpyuv/sharpyuv_csp.h;
- demux (the `demux` feature): webp/demux.h;
- mux (the `mux` feature): webp/mux.h.

webp/format_constants.h is not installed by upstream: it is the library's own.
"""
from dataclasses import dataclass, field
import hashlib
import json
from pathlib import Path
import re
import sys
import tarfile
import urllib.request

GROUPS = {
    'core': ['src/webp/types.h', 'src/webp/decode.h', 'src/webp/encode.h', 'src/webp/mux_types.h'],
    'sharpyuv': ['sharpyuv/sharpyuv.h', 'sharpyuv/sharpyuv_csp.h'],
    'demux': ['src/webp/demux.h'],
    'mux': ['src/webp/mux.h'],
}

# What a consumer includes for each group (compat.libwebp's include roots are
# the archive root and its src/).
INCLUDES = {
    'core': ['webp/types.h', 'webp/decode.h', 'webp/encode.h', 'webp/mux_types.h'],
    'sharpyuv': ['sharpyuv/sharpyuv.h', 'sharpyuv/sharpyuv_csp.h'],
    'demux': ['webp/demux.h'],
    'mux': ['webp/mux.h'],
}

# types.h declares the fixed-width integers itself under _MSC_VER: those are
# the C library's, not libwebp's.
STANDARD = {'int8_t', 'uint8_t', 'int16_t', 'uint16_t', 'int32_t', 'uint32_t', 'int64_t', 'uint64_t'}


def fetch(root: Path) -> Path:
    pins = json.loads((root / 'upstream/documents.json').read_text(encoding='utf-8'))
    cache = root / 'target/upstream'
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / 'libwebp-release.tar.gz'
    if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest() != pins['upstream_sha256']:
        print('+ fetch', pins['upstream'], flush=True)
        with urllib.request.urlopen(pins['upstream'], timeout=120) as response:
            archive.write_bytes(response.read())
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != pins['upstream_sha256']:
        sys.exit(f'upstream archive checksum {digest} != {pins["upstream_sha256"]}')
    with tarfile.open(archive) as tar:
        top = tar.getmembers()[0].name.split('/')[0]
        if not (cache / top).exists():
            tar.extractall(cache, filter='data')
    return cache / top


@dataclass
class Inline:
    """A `static WEBP_INLINE` function: internal linkage, adapted."""
    name: str
    nodiscard: bool
    result: str
    params: list[tuple[str, str]]   # (declaration, name)

    def declaration(self) -> str:
        params = ', '.join(p for p, _ in self.params) or 'void'
        return f'{"[[nodiscard]] " if self.nodiscard else ""}{self.result} {self.name}({params})'

    def definition(self) -> str:
        params = ', '.join(p for p, _ in self.params)
        args = ', '.join(n for _, n in self.params)
        return f'{self.result} {self.name}({params}) {{ return ::{self.name}({args}); }}'


@dataclass
class Group:
    types: list[str] = field(default_factory=list)
    enumerators: list[str] = field(default_factory=list)
    functions: list[str] = field(default_factory=list)
    inline: list[Inline] = field(default_factory=list)
    macros: list[str] = field(default_factory=list)


def strip(text: str) -> str:
    # One pass, so a `/*` inside a line comment (encode.h has some) does not
    # open a block comment.
    return re.sub(r'//[^\n]*|/\*.*?\*/', lambda m: '' if m.group().startswith('//') else ' ', text, flags=re.S)


def declarations(text: str) -> str:
    """The header without comments and preprocessor lines (and their continuations)."""
    text = strip(text).replace('\\\n', ' ')
    return re.sub(r'^\s*#[^\n]*', '', text, flags=re.M)


def parameter(text: str) -> tuple[str, str]:
    text = ' '.join(text.split())
    m = re.fullmatch(r'(.*?[\s\*])(\w+)', text)
    assert m, text
    return text, m.group(2)


def group(upstream: Path, files: list[str]) -> Group:
    raw = {name: (upstream / name).read_text(encoding='utf-8') for name in files}
    decls = '\n'.join(declarations(text) for text in raw.values())
    g = Group()
    names = set()
    names |= set(re.findall(r'typedef\s+(?:struct|union)\s+(\w+)\s+\1\s*;', decls))
    names |= set(re.findall(r'typedef\s+(?:struct|enum)\s*\{[^{}]*\}\s*(\w+)\s*;', decls))
    names |= set(re.findall(r'typedef\s+[\w\s\*]+?\(\s*\*\s*(\w+)\s*\)\s*\(', decls))
    for m in re.finditer(r'\benum\s+(?:WEBP_NODISCARD\s+)?(\w*)\s*\{(.*?)\}\s*(\w*)\s*;', decls, flags=re.S):
        names |= {n for n in (m.group(1), m.group(3)) if n}
        g.enumerators += re.findall(r'\b([A-Za-z_]\w*)\b\s*(?:=[^,]*)?(?:,|$)', m.group(2).strip())
    names |= set(re.findall(r'typedef\s+(?!struct|enum|union)[\w\s\*]+?\b(\w+)\s*;', decls))
    g.types = sorted(names - STANDARD)
    # Functions: WEBP_EXTERN / SHARPYUV_EXTERN declarations; the static inline
    # ones are adapted.
    for m in re.finditer(r'(?:WEBP|SHARPYUV)_EXTERN\s+[\w\s\*]+?\b(\w+)\s*\(', decls):
        g.functions.append(m.group(1))
    for m in re.finditer(r'(WEBP_NODISCARD\s+)?static\s+(?:WEBP|SHARPYUV)_INLINE\s+([\w\s\*]+?)\b(\w+)\s*\(([^)]*)\)\s*\{',
                         decls):
        params = [] if m.group(4).strip() in ('', 'void') else [parameter(p) for p in m.group(4).split(',')]
        g.inline.append(Inline(m.group(3), bool(m.group(1)), ' '.join(m.group(2).split()), params))
    g.functions = sorted(set(g.functions))
    g.macros = sorted({m for text in raw.values() for m in re.findall(r'^\s*#\s*define\s+(\w+)', text, flags=re.M)})
    return g


def surface(upstream: Path) -> dict[str, Group]:
    return {name: group(upstream, files) for name, files in GROUPS.items()}


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1]
    for name, g in surface(fetch(root)).items():
        print(f'== {name}: {len(g.types)} types, {len(g.enumerators)} enumerators, {len(g.functions)} functions, '
              f'{len(g.inline)} static inline, {len(g.macros)} macros')
        print('types:', ' '.join(g.types))
        print('enumerators:', ' '.join(g.enumerators))
        print('functions:', ' '.join(g.functions))
        print('inline:', *(i.declaration() for i in g.inline), sep='\n  ')
        print('macros:', ' '.join(g.macros))
