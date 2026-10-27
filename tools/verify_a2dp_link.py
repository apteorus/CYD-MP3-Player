#!/usr/bin/env python3
"""Fail closed if a player ELF did not link ChonkPod's pinned A2DP SDK module."""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT/'app/src/idf_a2dp'

def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def compiled_digest(p):
    # Arduino prepends one source-location directive when copying sketch files.
    text=p.read_text()
    text=re.sub(r'^#line 1 [^\n]*\n', '', text, count=1)
    return hashlib.sha256(text.encode()).hexdigest()

def require(condition, reason):
    if not condition:
        raise RuntimeError(reason)

def check_sources():
    manifest = json.loads((VENDOR/'manifest.json').read_text())
    require(manifest['idf_commit']=='735507283d5b2f9fb363a1901172dbd9e847945d', 'unexpected IDF revision')
    for name, entry in manifest['files'].items():
        require(digest(VENDOR/name)==entry['sha256'], f'vendored source mismatch: {name}')
    return manifest

def tool_paths(entry):
    compiler = Path(entry['arguments'][0])
    suffix = '.exe' if compiler.suffix.lower()=='.exe' else ''
    prefix = re.sub(r'(gcc|g\+\+)(\.exe)?$', '', compiler.name)
    require(prefix != compiler.name, 'unrecognized compiler path')
    return [compiler.with_name(prefix+t+suffix) for t in ('nm','objdump')]

def run(tool, *args):
    return subprocess.check_output([str(tool), *map(str,args)], text=True, stderr=subprocess.STDOUT)

def verify(build):
    manifest = check_sources()
    entries = json.loads((build/'compile_commands.json').read_text())
    matches = [e for e in entries if e['file'].replace('\\','/').endswith('/src/idf_a2dp/btc_a2dp_source.c')]
    require(len(matches)==1, 'missing/ambiguous SDK component compile command')
    entry=matches[0]; nm,objdump=tool_paths(entry)
    args=entry['arguments']; obj=Path(args[args.index('-o')+1])
    sdk_flags=[Path(x[1:]) for x in args if x.startswith('@') and x.replace('\\','/').endswith('/flags/c_flags')]
    require(len(sdk_flags)==1,'cannot locate pinned SDK archive')
    sdk_bt=sdk_flags[0].parent.parent/'lib/libbt.a'
    sdk_sha=digest(sdk_bt)
    require(sdk_sha=='d500529efd1d391492a4e5798f39f722e3bf0bbc359ff7c819bce56b1ca7141f',
            'unsupported libbt.a: use the official Arduino ESP32 3.3.10 SDK; no core file was modified')
    source=Path(entry['file'])
    require(compiled_digest(source)==manifest['files']['btc_a2dp_source.c']['sha256'], 'compiled source copy is stale')
    glue=source.with_name('packet_allocator.c')
    require(compiled_digest(glue)==compiled_digest(VENDOR/'packet_allocator.c'), 'compiled allocator glue is stale')
    elfs=list(build.glob('*.ino.elf')); require(len(elfs)==1, 'expected one Arduino sketch ELF')
    elf=elfs[0]; mapfile=elf.with_suffix('.map')
    text=mapfile.read_text(errors='replace')
    require('libbt.a(btc_a2dp_source.c.obj)' not in text, 'original libbt A2DP source object was linked')
    # Map ownership plus strong symbols: this component supplies the SDK entrypoints.
    for symbol in ('btc_a2dp_source_startup','btc_a2dp_source_setup_codec','btc_media_aa_read_feeding'):
        pattern=r'\.text\.'+symbol+r'\s+0x[0-9a-f]+\s+0x[0-9a-f]+\s+([^\n]+)'
        owners=re.findall(pattern,text)
        require(any('src/idf_a2dp/btc_a2dp_source.c.o' in o.replace('\\','/') for o in owners), f'wrong/missing map owner: {symbol}')
    symbols=run(nm,'--defined-only',elf)
    for name in ('chonkpod_a2dp_patch_id','chonkpod_a2dp_packet_alloc','chonkpod_a2dp_packet_stats'):
        require(re.search(r'\bT\s+'+name+r'$',symbols,re.M), f'missing patched symbol: {name}')
    object_asm=run(objdump,'-dr',obj)
    reloc=[line.strip() for line in object_asm.splitlines() if re.search(r'R_XTENSA_.*\s+chonkpod_a2dp_packet_alloc(?:\s|$)',line)]
    require(reloc, 'SDK module does not call the project packet allocator')
    asm=run(objdump,'-d',elf)
    calls=[line.strip() for line in asm.splitlines() if re.search(r'\bcall\w*\s+.*<chonkpod_a2dp_packet_alloc>',line)]
    require(len(calls)==1, 'expected one linked SDK packet allocation call site')
    lines=asm.splitlines()
    index=next(i for i,line in enumerate(lines) if line.strip()==calls[0])
    context='\n'.join(lines[max(0,index-14):index+3])
    # Pinned Xtensa compiler: verify actual caller computes 1056 for small MTUs,
    # otherwise MTU+32 capped at 4112, instead of passing a fixed 4112.
    require(re.search(r'movi(?:\.n)?\s+a10,\s*0x420\b',context), 'missing 1056-byte allocation floor in linked caller')
    require(re.search(r'addi\s+a10,\s*a\d+,\s*32\b',context), 'missing dynamic MTU+32 allocation in linked caller')
    require(re.search(r'minu\s+a10,',context), 'missing original upper allocation bound in linked caller')
    helper=run(objdump,'-d','--disassemble=chonkpod_a2dp_packet_alloc',elf)
    require('osi_malloc_func' in helper, 'patched allocator does not delegate to SDK allocator')
    require(b'idf5.5.4-mtualloc-v1' in elf.read_bytes(), 'missing SDK patch identity')
    report={'status':'verified','patch':'idf5.5.4-mtualloc-v1','idf_commit':manifest['idf_commit'],
            'elf_sha256':digest(elf),'module_object_sha256':digest(obj),'sdk_source_sha256':digest(source),
            'installed_sdk_libbt_sha256':sdk_sha,'original_sdk_object_linked':False,'allocation_call_relocations':reloc,'linked_calls':calls,'linked_sizing_context':context}
    (build/'a2dp-link-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    (build/'a2dp-allocator-disassembly.txt').write_text(helper+'\nLinked SDK allocation sizing and call site:\n'+context+'\n')
    print('PASS: project-local IDF A2DP entrypoints and dynamic packet allocator verified in final ELF; original SDK object absent')
    return report

def main():
    parser=argparse.ArgumentParser(__doc__)
    parser.add_argument('--build-dir',type=Path)
    parser.add_argument('--sources-only',action='store_true')
    a=parser.parse_args()
    try:
        if a.sources_only:
            check_sources();print('PASS: pinned A2DP source/header hashes')
        else:
            require(a.build_dir is not None,'--build-dir is required')
            verify(a.build_dir)
    except (RuntimeError,OSError,ValueError,KeyError,subprocess.CalledProcessError) as error:
        print('A2DP verification FAILED: '+str(error),file=sys.stderr)
        return 1
    return 0

if __name__=='__main__':
    sys.exit(main())
