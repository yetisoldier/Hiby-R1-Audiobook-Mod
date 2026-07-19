#!/usr/bin/env python3
"""Dump exported (defined) and imported dynamic symbols of an ELF .so/.bin.
Usage: dump_dynsyms.py <elf> [--imports] [--defined]
Uses pyelftools; architecture-agnostic (works on MIPS ELFs)."""
import sys
from elftools.elf.elffile import ELFFile

def main():
    path = sys.argv[1]
    show_imp = "--imports" in sys.argv
    show_def = "--defined" in sys.argv or not show_imp  # default defined
    with open(path, "rb") as f:
        elf = ELFFile(f)
        machine = elf.header.e_machine
        print(f"# {path}  machine={machine}  type={elf.header.e_type}")
        seen = set()
        for sec in elf.iter_sections():
            if sec.name not in (".dynsym",):
                continue
            for sym in sec.iter_symbols():
                name = sym.name
                if not name:
                    continue
                # binding: STB_LOCAL/GLOBAL/WEAK; shndx
                bind = sym.entry.st_info.bind
                shndx = sym.entry.st_shndx
                defined = not (shndx == "SHN_UNDEF" or shndx == 0)
                if defined and show_def:
                    if name in seen:
                        continue
                    seen.add(name)
                    print(f"DEF {bind:7} {name}")
                if (not defined) and show_imp:
                    if name in seen:
                        continue
                    seen.add(name)
                    print(f"IMP {bind:7} {name}")

if __name__ == "__main__":
    main()