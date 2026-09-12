from pathlib import Path
import subprocess,os,tempfile
root=Path(__file__).resolve().parents[2];src=root/'src'
workspace=tempfile.TemporaryDirectory(prefix='fixup-blocks-',dir='/tmp');test=Path(workspace.name)
source=test/'forward.asm'
source.write_text(' assume adl=1\n org 0\n'+' jp target\n'*1859+'target: ret\n')
expected=(bytes([0xc3])+(1859*4).to_bytes(3,'little'))*1859+bytes([0xc9])
(test/'probe.c').write_text(r'''
#include <stdio.h>
#include <stdlib.h>
#include "defines.h"
#include "globals.h"
#include "utils.h"
static unsigned requests, allocated, released, exact;
static void report(void) { fprintf(stderr,"BLOCKS requests=%u exact=%u allocated=%u freed=%u\n",requests,exact,allocated,released); }
static void init(void) { if(!requests && !exact) atexit(report); }
void *blockTestMalloc(size_t n) {
    void *p; const char *mode=getenv("BLOCK_FAIL"); init(); requests++;
    if(mode && mode[0] && (mode[0]=='p' || allocated >= 1)) return NULL;
    p=malloc(n); if(p) allocated++; return p;
}
void *blockTestAllocate(size_t n, uint24_t *counter) {
    void *p; const char *mode=getenv("BLOCK_FAIL"); init(); exact++;
    if(mode && mode[0]=='a' && allocated >= 1) { error(message[ERROR_MEMORY],0); return NULL; }
    p=allocateMemory(n,counter); if(p) allocated++; return p;
}
void blockTestFree(void *p) { if(p) released++; free(p); }
''')
flags=['cc','-DUNIX','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wno-deprecated-declarations','-I'+str(src)]
subprocess.run(flags+['-Dmalloc=blockTestMalloc','-Dfree=blockTestFree','-DallocateMemory=blockTestAllocate','-c',str(src/'fixup.c'),'-o',str(test/'fixup.o')],check=True)
subprocess.run(flags+[str(p) for p in src.glob('*.c') if p.name!='fixup.c']+[str(test/'probe.c'),str(test/'fixup.o'),'-o',str(test/'assembler')],check=True)
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
subprocess.run(['python3',str(root/'tests/Fixups/regression.py'),str(test/'assembler')],env=env,check=True)
for mode in ['', 'preferred', 'after_first']:
 with tempfile.TemporaryDirectory(dir="/tmp") as d:
  output=Path(d)/'test.bin'
  p=subprocess.run([str(test/'assembler'),'-m',str(source),str(output),'-c'],env=dict(env,BLOCK_FAIL=mode),capture_output=True,text=True)
  assert 'runtime error:' not in p.stderr and 'AddressSanitizer' not in p.stderr,p.stderr
  if mode=='after_first':
   assert p.returncode!=0 and 'Error allocating memory' in p.stdout and not output.exists(),p.stdout+p.stderr
  else:
   assert p.returncode==0,p.stdout+p.stderr
   assert output.read_bytes()==expected
  import re
  m=re.search(r'allocated=(\d+) freed=(\d+)',p.stderr);assert m and m[1]==m[2],p.stderr
  if not mode: assert int(m[1]) < 1859//4, p.stderr
  print((mode or 'normal')+': '+p.stderr.strip())
# Tiny capacity makes every record oversized, exercising dedicated chunks.
subprocess.run(flags+['-DFIXUP_BLOCK_SIZE=32','-DOUTPUT_BUFFERSIZE=7']+[str(p) for p in src.glob('*.c')]+['-o',str(test/'tiny')],check=True)
subprocess.run(['python3',str(root/'tests/Fixups/regression.py'),str(test/'tiny')],env=env,check=True)
print('Allocation failure, fallback, alignment, oversized records and tiny output window checks passed.')
