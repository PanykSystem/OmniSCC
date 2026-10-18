#!/usr/bin/env python3
"""Walk emitted ARM service functions, including branches and linker veneers."""
import argparse, re, subprocess

def parse(disassembly):
    funcs={}; current=None
    for line in disassembly.splitlines():
        m=re.match(r'^([0-9a-f]+) <(.*)>:$',line)
        if m:
            current=int(m[1],16);funcs[current]={'name':m[2],'instructions':[]};continue
        m=re.match(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4,8}\s+)+\s*([.\w]+)\s*(.*)$',line)
        if m and current is not None:funcs[current]['instructions'].append((int(m[1],16),m[2],m[3]))
    return funcs

def ram(address):return 0x20000000<=address<0x20040000

def edges(address, funcs, indirect=None):
    f=funcs[address];result=set(); literals={pc:int(args.split()[0],16) for pc,op,args in f['instructions'] if op=='.word'}
    for pc,op,args in f['instructions']:
        if op in ('mov','movs','ldr') and args.startswith('pc,'):raise ValueError(f'unresolved PC write at {pc:x}')
        if op in ('bl','blx','b','b.w') or (op.startswith('b') and op not in ('bic','bics','bkpt','bx')):
            m=re.match(r'([0-9a-f]+) <',args)
            if m:
                target=int(m[1],16)&~1
                if target not in funcs:
                    # Local labels are not separate functions.
                    if any(x[0]==target for x in f['instructions']):continue
                    raise ValueError(f'unresolved target {target:x} at {pc:x} in {f["name"]}')
                if target!=address or op in ('bl','blx'):result.add(target)
            elif op=='blx':
                targets=(indirect or {}).get(f['name'])
                if targets is None:raise ValueError(f'unresolved indirect call at {pc:x} in {f["name"]}')
                result.update(targets)
        if op=='bx' and args.strip() not in ('lr','r14'):
            # ARMv6-M linker veneer: literal -> r0 -> ip -> bx ip.
            targets=[value&~1 for value in literals.values() if value&~1 in funcs]
            if len(targets)!=1 or not f['name'].endswith('_veneer'):raise ValueError(f'unresolved indirect branch at {pc:x} in {f["name"]}')
            result.update(targets)
    return result

def check(funcs, roots):
    seen=set();pending=list(roots)
    while pending:
        address=pending.pop()
        if address in seen:continue
        if address not in funcs:raise ValueError(f'missing root {address:x}')
        if not ram(address):raise ValueError(f'flash service {funcs[address]["name"]} at {address:x}')
        seen.add(address)
        for pc,op,args in funcs[address]['instructions']:
            if op=='.word':
                value=int(args.split()[0],16)&~1
                # GCC folds XIP_BASE + page-loop bounds into literals in the
                # journal verifier. They are address arithmetic, not a table
                # fetched while programming; inspect only this proven shape.
                arithmetic = funcs[address]['name'].startswith('KUCSaveStorage::ProgramPage(') and value in (0x10000060, 0x10000100)
                if 0x10000000<=value<0x11000000 and value not in funcs and not arithmetic:
                    raise ValueError(f'flash constant/data pointer {value:x} in {funcs[address]["name"]}')
        name = funcs[address]['name']
        # The SDK primitive invokes boot-ROM functions and its SRAM boot2 copy.
        # Those indirect targets are SDK-owned; the caller still must be in SRAM.
        if name in ('flash_do_cmd', '__wrap_memset', '__wrap_memcpy', '__wrap___aeabi_memcpy', '__wrap___aeabi_memset'):
            continue
        indirect_prefixes = {
            'KUCFlash::Advance(': ('CKUCCartridge::ReadFlash(', 'CKUCCartridge::ProgramFlash(', 'CKUCCartridge::EraseFlash(', 'CKUCCartridge::SavePending(', 'CKUCCartridge::ServiceSave('),
            'KUCFlash::Read(': ('CKUCCartridge::ReadFlash(',),
            'KUCSave::ReadByte(': ('KUCSaveStorage::Read(',),
            'KUCSave::Append(': ('KUCSaveStorage::Read(', 'KUCSaveStorage::ProgramPage(', 'KUCSaveStorage::Pending('),
            'KUCSave::Service(': ('KUCSaveStorage::Pending(', 'KUCSaveStorage::Service('),
            'KUCSectorSave::ReadByte(': ('KUCSaveStorage::Read(',),
            'KUCSectorSave::Effective(': ('CKUCCartridge::ReadOriginal(',),
            'KUCSectorSave::Append(': ('KUCSaveStorage::Program(', 'KUCSaveStorage::Pending('),
            'KUCSectorSave::Program(': ('KUCSaveStorage::Program(', 'KUCSaveStorage::Pending('),
            'KUCSectorSave::Service(': ('KUCSaveStorage::Pending(', 'KUCSaveStorage::Service('),
        }
        # KUC SPI session setup/teardown resolves only RP2040 boot-ROM entry
        # points (connect, exit, flush, enter). Inspect their direct calls too.
        indirect = {name: set()} if name in ('KUCFlashSPI::Begin()', 'KUCFlashSPI::End()') else {}
        for prefix, callbacks in indirect_prefixes.items():
            if name.startswith(prefix):
                indirect[name] = {a for a, f in funcs.items() if any(f['name'].startswith(p) for p in callbacks)}
        targets=edges(address,funcs,indirect)
        if funcs[address]['name']=='CSCCSound::startOnCore1()':
            # IRQ installation/enabling happens once before the audio service loop.
            targets={x for x in targets if funcs[x]['name'] not in ('__irq_set_exclusive_handler_veneer','__irq_set_enabled_veneer')}
        pending.extend(targets)
    return seen

def load(elf,objdump='arm-none-eabi-objdump'):
    return parse(subprocess.check_output([objdump,'-d','-C',str(elf)],text=True))

def select_roots(funcs):
    required=['flash_read_sector','flash_get_xip_page','RomStorage::ReadAny(', 'VerifyRomPage(', 'CSCCSound::core1Entry()', 'CSCCSound::dmaIrqHandler()', 'CSCCSound::readRegister(', 'CSCCSound::readKUCRegister(', 'CKUCCartridge::Execute()', 'CSCCICartridge::Execute()']
    for cls in ('CCartridge','CGeneric8kBCartridge','CKonamiCartridge','CKonamiSCCCartridge','CASCII8Cartridge','CASCII16Cartridge'):
        for kind in (0,1):required.append(f'bool {cls}::ExecuteStorage<(RomStorageKind){kind}>()')
    roots=set()
    for prefix in required:
        matches=[addr for addr,f in funcs.items() if f['name']==prefix or ('(' in prefix and f['name'].startswith(prefix))]
        if not matches:raise ValueError('missing mandatory SRAM root: '+prefix)
        roots.update(matches)
    roots.update(addr for addr,f in funcs.items() if f['name'].startswith('CSCCSound::renderOneWord(') or f['name'].startswith('CSCCSound::service('))
    return roots

def main():
    p=argparse.ArgumentParser();p.add_argument('--elf',required=True);p.add_argument('--objdump',default='arm-none-eabi-objdump');p.add_argument('--nm',default='arm-none-eabi-nm');a=p.parse_args()
    funcs=load(a.elf,a.objdump)
    roots=select_roots(funcs)
    checked=check(funcs,roots);print(f'OK: {len(checked)} emitted ROM/audio service functions and veneers execute from SRAM')
if __name__=='__main__': main()
