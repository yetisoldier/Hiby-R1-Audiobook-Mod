#!/usr/bin/env python3
"""Inspect GLIBC version requirements of a .so or binary."""
import sys
from collections import defaultdict
from elftools.elf.elffile import ELFFile

def main(path):
    with open(path, 'rb') as f:
        e = ELFFile(f)
        dynsym = None
        versym = None
        verneed = None
        for s in e.iter_sections():
            if s.name == '.dynsym':
                dynsym = s
            elif s.name == '.gnu.version':
                versym = s
            elif s.name == '.gnu.version_r':
                verneed = s
        if not dynsym or not versym or not verneed:
            print("missing sections")
            return 1

        # Build version index -> (filename, version_name)
        ver_map = {}
        for ver, auxg in verneed.iter_versions():
            filename = ver.name
            for aux in auxg:
                ver_map[aux.entry.vna_other] = (filename, aux.name)

        # versym data is a sequence of 2-byte indices
        versym_data = versym.data()
        def vidx(i):
            off = i * 2
            return int.from_bytes(versym_data[off:off+2], 'little')

        by_ver = defaultdict(list)
        for i, sym in enumerate(dynsym.iter_symbols()):
            if sym.name == '':
                continue
            ver_idx = vidx(i)
            if ver_idx == 0 or ver_idx == 1:
                continue
            ver = ver_map.get(ver_idx, ('?', '?'))
            by_ver[ver].append(sym.name)

        print(f"=== {path} ===")
        for (fn, vn), syms in sorted(by_ver.items()):
            print(f"\n{fn}: {vn} ({len(syms)} symbols)")
            for s in sorted(syms):
                print(f"  {s}")

if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))