"""Compare literals and failed conversions with the bb7d36c reference.
Reference accumulators use unsigned math to model target wraparound safely.
Run without arguments; requires cc with address/undefined-behavior sanitizers.
"""
from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parents[2]
harness=r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "str2num.h"
bool relocate;
uint24_t address=123,relocateBaseAddress=1000,relocateOutputBaseAddress=100;
extern bool ref_err_str2num;
int32_t ref_str2num(const char *, uint8_t);
static unsigned seed=901;
static unsigned rnd(void){seed=seed*1664525u+1013904223u;return seed;}
static void check(const char *s){
 uint8_t n=strlen(s);int32_t a=ref_str2num(s,n);bool invalid=ref_err_str2num;
 int32_t b=str2num(s,n);
 if(invalid!=err_str2num || a!=b){fprintf(stderr,"Mismatch: %s: %08x/%d vs %08x/%d\n",s,(unsigned)a,invalid,(unsigned)b,err_str2num);exit(1);}
}
int main(void){
 char s[80];unsigned i,j;
 const char *cases[]={"$","#","%","0x","0b","0bh","FACEh","FFFFFFFFh","$FFFFFFFF","0x80000000","4294967295","4294967296","42949672960","100000000h","11111111111111111111111111111111b","100000000000000000000000000000000b","2b","123abc","badName","Finish","99","09","9","00000000h"};
 for(i=0;i<sizeof(cases)/sizeof(cases[0]);i++)check(cases[i]);
 for(i=32;i<127;i++){s[0]=i;s[1]=0;check(s);for(j=32;j<127;j++){s[1]=j;s[2]=0;check(s);}}
 for(i=0;i<30000;i++){unsigned n=1+rnd()%64;for(j=0;j<n;j++)s[j]=32+(rnd()>>16)%95;s[n]=0;check(s);}
 for(i=0;i<10000;i++){unsigned v=rnd();snprintf(s,sizeof(s),"%u",v);check(s);snprintf(s,sizeof(s),"%08xh",v);check(s);}
 relocate=true;check("$");
 puts("Literal/reference comparisons passed: spellings, full-width/wrapping values and invalid conversion results");
}
'''
with tempfile.TemporaryDirectory(prefix='literals-',dir='/tmp') as folder:
 p=Path(folder);(p/'test.c').write_text(harness)
 flags=['cc','-DUNIX','-O1','-g','-fsanitize=address,undefined','-I'+str(root/'src')]
 names=['str2num','str2hex','str2bin','str2dec','isvalidNumber','err_str2num','str2numOrLabel']
 subprocess.run(flags+['-D'+n+'=ref_'+n for n in names]+['-c',str(Path(__file__).with_name('literal-reference.c')),'-o',str(p/'reference.o')],check=True)
 subprocess.run(flags+[str(p/'test.c'),str(root/'src/str2num.c'),str(p/'reference.o'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),check=True)
