#!/usr/bin/env python3
"""Check final scratch/stack/heap allocations (optional ELF argument)."""
import subprocess, sys
from pathlib import Path

def placement(elf):
    symbols = {}
    for line in subprocess.check_output(['arm-none-eabi-nm', '-S', '-C', str(elf)], text=True).splitlines():
        fields = line.split()
        if len(fields) >= 3:
            if len(fields) >= 4 and len(fields[1]) == 8:
                address, size, name = int(fields[0],16), int(fields[1],16), ' '.join(fields[3:])
            else:
                address, size, name = int(fields[0],16), 0, ' '.join(fields[2:])
            symbols[name] = (address,size)
    start,size = symbols['CLoader::exchange_buffer']
    bottom = symbols['__StackOneBottom'][0]
    top = symbols['__StackOneTop'][0]
    assert start == 0x20038000 and size == 8192, (hex(start),size)
    assert start + size <= bottom and top-bottom == 8192
    assert symbols['__StackTop'][0]-symbols['__StackBottom'][0] == 16384
    assert symbols['__HeapLimit'][0]-symbols['__end__'][0] == 2048
    cache, cache_size = symbols['car_cache']
    assert cache_size == 131072
    assert symbols['__kuc_runtime_start__'][0] == cache + 106496
    assert symbols['__kuc_runtime_end__'][0] <= cache + 122880
    source = symbols['__kuc_runtime_source__'][0]
    assert 0x10000000 <= source < 0x100ff000
    print('OK: KUC overlay preserves the live upper menu and full ordinary workspace')
    print('OK: disjoint loader exchange/core1 stack; core0 and heap preserved')

if __name__ == '__main__':
    placement(Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).resolve().parents[2]/'generated/build/OmniSCC.elf')
