#!/usr/bin/env python3
"""Audit the modules against libwebp's public headers (the pinned release's).

- The module interfaces and their implementation units are what
  tools/gen_exports.py writes.
- Every type, enumerator and WEBP_EXTERN function of each header group is
  exported by its module, and every static inline function adapted there.
- Every macro of the public headers is mapped in src/macros.cppm or excluded
  below with a reason (the declaration annotations and the include guards).
"""
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_exports import MODULES  # noqa: E402
from inventory import fetch, surface  # noqa: E402

EXCLUDED = {
    'WEBP_EXTERN': 'a linkage annotation of declarations',
    'WEBP_INLINE': "the headers' inlining annotation of their static functions",
    'WEBP_NODISCARD': 'an attribute of declarations (kept as [[nodiscard]] on the adaptations)',
    'SHARPYUV_EXTERN': 'a linkage annotation of declarations',
    'SHARPYUV_INLINE': "the header's inlining annotation of its static function",
}


def is_guard(name: str) -> bool:
    return re.fullmatch(r'WEBP_(?:WEBP|SHARPYUV)_\w+_H_', name) is not None


def mapped_names(text: str) -> set[str]:
    text = text[text.index('export namespace'):]
    names = set(re.findall(r'inline constexpr (?:[\w:&]+\s+)+?(\w+)\s*=', text))
    names |= set(re.findall(r'^constexpr [\w:\*\s]+?\b(\w+)\s*\(', text, flags=re.M))
    return names


def main():
    root = Path(__file__).resolve().parents[1]
    problems = []
    generated = subprocess.run([sys.executable, str(root / 'tools/gen_exports.py'), '--check'], capture_output=True,
                               text=True)
    if generated.returncode != 0:
        problems.append(generated.stderr.strip() or generated.stdout.strip())
    sf = surface(fetch(root))
    macros = set()
    counts = []
    for group, g in sf.items():
        text = (root / MODULES[group]['interface']).read_text(encoding='utf-8')
        exported = set(re.findall(r'export using ::(\w+);', text))
        missing = (set(g.types) | set(g.enumerators) | set(g.functions)) - exported
        if missing:
            problems.append(f'{MODULES[group]["interface"]} lacks: ' + ', '.join(sorted(missing)))
        adapted = text[text.index('export namespace mcpplibs::mwrap::libwebp {'):]
        missing = {i.name for i in g.inline if not re.search(r'\b' + i.name + r'\(', adapted)}
        if missing:
            problems.append(f'{MODULES[group]["interface"]} does not adapt: ' + ', '.join(sorted(missing)))
        macros |= set(g.macros)
        counts.append(f'{group} {len(g.types)}/{len(g.enumerators)}/{len(g.functions)}/{len(g.inline)}')
    owners = {}
    for group, g in sf.items():
        for name in g.types + g.enumerators + g.functions:
            owners.setdefault(name, []).append(group)
    shared = {name: groups for name, groups in owners.items() if len(groups) > 1}
    if shared:
        problems.append('exported by more than one module: ' + ', '.join(sorted(shared)))
    mapped = mapped_names((root / 'src/macros.cppm').read_text(encoding='utf-8'))
    guards = {m for m in macros if is_guard(m)}
    unaccounted = macros - mapped - EXCLUDED.keys() - guards
    if unaccounted:
        problems.append('macros neither mapped nor excluded: ' + ', '.join(sorted(unaccounted)))
    both = EXCLUDED.keys() & mapped
    if both:
        problems.append('listed as excluded yet mapped: ' + ', '.join(sorted(both)))
    stale = EXCLUDED.keys() - macros
    if stale:
        problems.append('excluded but not in the headers: ' + ', '.join(sorted(stale)))
    extra = mapped - macros
    if extra:
        problems.append('mapped but not a macro of the headers: ' + ', '.join(sorted(extra)))
    if problems:
        sys.exit('\n'.join(problems))
    print('exported (types/enumerators/functions/adapted static inline): ' + ', '.join(counts) +
          f'; {len(macros)} macros: {len(macros & mapped)} mapped, {len(EXCLUDED)} annotations and '
          f'{len(guards)} include guards excluded')


if __name__ == '__main__':
    main()
