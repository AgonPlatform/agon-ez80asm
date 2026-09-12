"""Exercise register and condition-code opcode fields; pass a host assembler."""
import pathlib,runpy
check=runpy.run_path(str(pathlib.Path(__file__).with_name('regression.py')))['check']
registers=[('b',0),('c',1),('d',2),('e',3),('h',4),('l',5),('a',7)]
s='';expected=bytearray()
for dst,d in registers:
    for src,r in registers:
        s+='ld '+dst+','+src+'\n';expected.append(0x40+d*8+r)
    s+='ld '+dst+',$23\n';expected.extend([0x06+d*8,0x23])
check(s,bytes(expected))
s='assume adl=1\n';expected=bytearray()
for i,r in enumerate(['bc','de','hl','sp']):
    s+='ld '+r+',$1234\n';expected.extend([0x01+i*16,0x34,0x12,0])
for i,r in enumerate(['bc','de','hl','af']):
    s+='push '+r+'\n';expected.append(0xc5+i*16)
check(s,bytes(expected))
s='assume adl=1\n';expected=bytearray()
for i,cc in enumerate(['nz','z','nc','c','po','pe','p','m']):
    s+='jp '+cc+',$123456\ncall '+cc+',$123456\nret '+cc+'\n'
    expected.extend([0xc2+i*8,0x56,0x34,0x12,0xc4+i*8,0x56,0x34,0x12,0xc0+i*8])
check(s,bytes(expected))
print('3 additional opcode-field cases passed (88 instructions)')
