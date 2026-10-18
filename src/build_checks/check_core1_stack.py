#!/usr/bin/env python3
"""Conservative emitted call-chain bound for core1 startup and audio IRQ."""
import argparse,re,subprocess
from pathlib import Path
from check_rom_residency import load,edges

def bound(frames,graph,roots,interrupt=0,limit=4096):
    memo={}
    def visit(name,active):
        if name in active:raise ValueError('recursive stack: '+str(name))
        if name not in frames or frames[name] is None:raise ValueError('unknown stack: '+str(name))
        if name not in memo:memo[name]=frames[name]+max([visit(x,active|{name}) for x in graph.get(name,[])]+[0])
        return memo[name]
    result=max(visit(x,set()) for x in roots)+interrupt
    if result>=limit:raise ValueError(f'stack bound {result} exceeds {limit-1}')
    return result

def frame(instructions):
    # Overcount all pushes/SP decrements, even mutually exclusive prologues.
    total=0
    for pc,op,args in instructions:
        if op=='push':
            registers=args[args.index('{')+1:args.index('}')].split(',');total+=4*len(registers)
        if op in ('sub','subs') and args.startswith('sp,'):
            m=re.search(r'#(\d+)',args)
            if not m:raise ValueError(f'dynamic stack at {pc:x}')
            total+=int(m[1])
        if op in ('mov','movs','add','adds','bic','bics') and args.startswith('sp,'):
            if op.startswith('add') and re.search(r'#\d+',args):continue
            raise ValueError(f'unresolved SP change at {pc:x}')
    return total

def read_reports(paths):
    reported={}
    for path in paths:
        for line in path.read_text().splitlines():
            fields=line.split('\t')
            if '\tdynamic' in line and ',bounded' not in line:
                raise ValueError('unknown dynamic stack: '+line)
            if len(fields)!=3:continue
            # The source location has two numeric suffixes. Splitting at a
            # fixed colon count mistakes Windows' drive colon for a separator,
            # while splitting from the right would break C++ qualified names.
            location=re.fullmatch(r'.*?:\d+:\d+:(.+)',fields[0])
            if not location:raise ValueError('invalid stack usage location: '+fields[0])
            signature=location[1].strip()
            match=re.search(r'([\w:~]+)\(',signature)
            key=match[1] if match else signature.split()[-1]
            reported[key]=max(reported.get(key,0),int(fields[1]))
    return reported

def main():
    p=argparse.ArgumentParser();p.add_argument('--elf',required=True);p.add_argument('--su-root',required=True);a=p.parse_args();funcs=load(a.elf)
    def named(name):
        return next(addr for addr,f in funcs.items() if f['name']==name)
    # The SDK wrapper's entry argument is exactly CSCCSound::core1Entry.
    entry=named('CSCCSound::core1Entry()');irq=named('CSCCSound::dmaIrqHandler()')
    indirect={'core1_wrapper':[entry]}
    # Runtime initializers are a linker-generated table, not an unconstrained callback.
    nm=subprocess.check_output(['arm-none-eabi-nm',str(a.elf)],text=True)
    syms={fields[2]:int(fields[0],16) for line in nm.splitlines() if len(fields:=line.split())==3}
    table=[int(args.split()[0],16) for pc,op,args in funcs[named('runtime_run_per_core_initializers')]['instructions'] if op=='.word']
    if len(table)!=2:raise ValueError('unrecognized per-core initializer table')
    begin,end=table
    if not (0x20000000<=begin<=end<0x20040000) or (end-begin)%4:raise ValueError('invalid initializer table')
    raw=subprocess.check_output(['arm-none-eabi-objdump','-s',f'--start-address={begin}',f'--stop-address={end}',a.elf],text=True)
    data=b''
    for line in raw.splitlines():
        m=re.match(r'\s*[0-9a-f]+\s+((?:[0-9a-f]{8}\s*){1,4})',line)
        if m:data+=bytes.fromhex(m[1])
    init=[int.from_bytes(data[i:i+4],'little')&~1 for i in range(0,end-begin,4)]
    indirect['runtime_run_per_core_initializers']=init
    graph={};frames={};seen=set();pending=[named('core1_wrapper'),irq]
    su=list(Path(a.su_root).rglob('*.su'))
    if not su:raise ValueError('no compiler stack usage reports')
    reported=read_reports(su)
    while pending:
        addr=pending.pop()
        if addr in seen:continue
        if addr not in funcs:raise ValueError(f'unresolved function {addr:x}')
        seen.add(addr);f=funcs[addr];frames[addr]=frame(f['instructions'])
        key=f['name'].split('(')[0]
        if not key.endswith('_veneer'):
            if key not in reported:raise ValueError('missing stack usage report: '+key)
            frames[addr]=max(frames[addr],reported[key])
        graph[addr]=edges(addr,funcs,indirect)
        # SDK panic is a fatal configuration fault, outside successful startup.
        graph[addr]={x for x in graph[addr] if funcs[x]['name'] not in ('panic','__assert_func')}
        pending.extend(graph[addr])
    irqBound=bound(frames,graph,[irq],interrupt=64)
    maximum=bound(frames,graph,[named('core1_wrapper')],interrupt=irqBound)
    print(f'OK: conservative core1 startup/render + DMA exception stack <= {maximum} bytes (<4096); {len(seen)} emitted functions, {len(su)} stack reports')
if __name__=='__main__':main()
