"""Inspect shipped PE size and reject build-machine/debug metadata (no dependencies)."""
import argparse
import json
import os
from pathlib import Path
import re
import struct


def inspect(path, forbidden=()):
    data = Path(path).read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('not a PE executable')
    count, = struct.unpack_from('<H', data, pe + 6)
    optional = pe + 24
    optional_size, = struct.unpack_from('<H', data, pe + 20)
    directory = optional + (112 if struct.unpack_from('<H', data, optional)[0] == 0x20b else 96)
    sections = []
    for i in range(count):
        entry = optional + optional_size + i * 40
        virtual, rva, size, offset = struct.unpack_from('<IIII', data, entry + 8)
        sections.append(dict(name=data[entry:entry + 8].rstrip(b'\0').decode(),
                             size=size, virtual_size=virtual, rva=rva, offset=offset))

    def file_offset(rva):
        for section in sections:
            if section['rva'] <= rva < section['rva'] + max(section['size'], section['virtual_size']):
                return section['offset'] + rva - section['rva']
        raise ValueError('unmapped RVA')

    issues = []
    symbols, = struct.unpack_from('<I', data, pe + 16)
    if symbols:
        issues.append(f'COFF symbol table contains {symbols} symbols')
    debug_rva, debug_size = struct.unpack_from('<II', data, directory + 6 * 8)
    debug_types = []
    if debug_rva:
        start = file_offset(debug_rva)
        for entry in range(start, start + debug_size, 28):
            kind, = struct.unpack_from('<I', data, entry + 12)
            debug_types.append(kind)
            # POGO section layout (13), REPRO (16), and extended DLL security
            # flags (20) contain no symbols/source paths. Keep loader metadata.
            if kind not in (13, 16, 20):
                issues.append(f'PE debug metadata remains (type {kind})')
    # Check raw ASCII and UTF-16 bytes, including strings outside printable runs.
    hits = set()
    # UTF-16 literals may start at either byte alignment in PE resources/data.
    for encoding, offset in (('utf-8', 0), ('utf-16le', 0), ('utf-16le', 1)):
        decoded = data[offset:].decode(encoding, errors='replace')
        for match in re.finditer(r'[A-Za-z]:[\\/]Users[\\/][^\x00\r\n<>"\s]+', decoded, re.I):
            hits.add(match.group()[:240])
        for prefix in forbidden:
            if prefix and prefix.casefold() in decoded.casefold():
                hits.add(prefix)
        if re.search(r'(?i)\.pdb\x00', decoded):
            issues.append(f'PDB reference remains ({encoding}, offset {offset})')
    issues.extend(f'build-machine path remains: {hit}' for hit in sorted(hits))
    imports = []
    import_rva, _ = struct.unpack_from('<II', data, directory + 8)
    if import_rva:
        entry = file_offset(import_rva)
        while any(data[entry:entry + 20]):
            name_rva, = struct.unpack_from('<I', data, entry + 12)
            start = file_offset(name_rva)
            imports.append(data[start:data.index(b'\0', start)].decode())
            entry += 20
    end = max(section['offset'] + section['size'] for section in sections)
    return dict(path=str(Path(path).resolve()), size=len(data), sections=sections,
                coff_symbols=symbols, debug_directory_bytes=debug_size, debug_types=debug_types,
                overlay_bytes=len(data) - end, imports=imports, issues=issues)


def linked_code(map_path, sections):
    """Estimate text ownership from symbol intervals, including alignment padding."""
    from collections import Counter
    entries = {}
    for line in Path(map_path).read_text(errors='replace').splitlines():
        match = re.match(r'\s+0001:([0-9a-fA-F]+)\s+(\S+)\s+[0-9a-fA-F]+\s+f\s+(\S+)', line)
        if match:
            entries.setdefault(int(match[1], 16), (match[2], match[3]))

    def owner(name, obj):
        match = re.match(r'_ZN(\d+)', name)
        if match:
            part = name[match.end():match.end() + int(match[1])].replace('_$LT$', '').lstrip('$')
            crate = re.match(r'([A-Za-z_][A-Za-z_0-9]*)', part)
            if crate:
                return 'shared generics' if crate[1] in ('RF', 'mut', 'u20') else crate[1]
        if 'libaudcore_host' in obj:
            dsp = any(x in obj for x in ('Processors.obj', 'Core.obj', 'JiangProcessor.obj', 'State.obj'))
            return 'native effects DSP' if dsp else 'native effects UI/bridge'
        if obj.startswith('lib'):
            return re.sub(r'-[0-9a-f]+.*', '', obj[3:])
        return 'other native/runtime'

    end = next(section['virtual_size'] for section in sections if section['name'] == '.text')
    ordered = sorted(offset for offset in entries if offset < end)
    groups = Counter()
    largest = []
    for i, offset in enumerate(ordered):
        size = (ordered[i + 1] if i + 1 < len(ordered) else end) - offset
        name, obj = entries[offset]
        group = owner(name, obj)
        groups[group] += size
        largest.append(dict(size=size, group=group, symbol=name))
    return dict(method='Symbol start intervals including padding; LTO and shared generics prevent exact feature attribution.',
                groups=dict(groups.most_common()),
                largest_symbols=sorted(largest, key=lambda item: item['size'], reverse=True)[:20])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe')
    parser.add_argument('--json', dest='output')
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--map', dest='map_path')
    parser.add_argument('--forbid-prefix', action='append', default=[])
    args = parser.parse_args()
    root = str(Path(__file__).resolve().parent.parent)
    forbidden = [root, str(Path(root).parent / 'audcore'), str(Path.home()), os.environ.get('CARGO_TARGET_DIR', '')] + args.forbid_prefix
    report = inspect(args.exe, forbidden)
    if args.map_path:
        report['linked_code'] = linked_code(args.map_path, report['sections'])
    text = json.dumps(report, ensure_ascii=True, indent=2) + '\n'
    if args.output:
        Path(args.output).write_text(text, encoding='utf-8')
    print(text, end='')
    return int(args.check and bool(report['issues']))


if __name__ == '__main__':
    raise SystemExit(main())
