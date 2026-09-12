"""Compare the guarded converter's valid values/error flags with str2num.
Invalid numeric values are deliberately not compared: callers must use the flag.
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
static unsigned seed=8123;
static unsigned rnd(void){seed=seed*1664525u+1013904223u;return seed;}
static void check(const char *s){
 uint8_t n=strlen(s);int32_t a=str2num(s,n);bool invalid=err_str2num;
 int32_t b=str2numOrLabel(s,n);
 if(invalid!=err_str2num || (!invalid && a!=b)){fprintf(stderr,"Mismatch: %s\n",s);exit(1);}
}
int main(void){
 char s[8];unsigned i,j;
 for(i=32;i<127;i++){s[0]=i;s[1]=0;check(s);for(j=32;j<127;j++){s[1]=j;s[2]=0;check(s);}}
 for(i=0;i<20000;i++){unsigned n=1+rnd()%6;for(j=0;j<n;j++)s[j]=32+(rnd()>>16)%95;s[n]=0;check(s);}
 check("$");relocate=true;check("$");
 puts("29,122 numeric conversion contract checks passed");
}
'''
with tempfile.TemporaryDirectory(prefix='num-contract-',dir='/tmp') as folder:
 p=Path(folder);(p/'test.c').write_text(harness)
 subprocess.run(['cc','-DUNIX','-O1','-g','-fsanitize=address,undefined','-I'+str(root/'src'),str(p/'test.c'),str(root/'src/str2num.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),check=True)
