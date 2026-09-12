"""Run the existing regressions plus colliding forward-symbol/definition cases.
Usage: python3 tests/Fixups/symbols.py /path/to/assembler
"""
import pathlib, re, runpy
ns = runpy.run_path(str(pathlib.Path(__file__).with_name('regression.py')))
check = ns['check']
# Generate a chain using the actual Pearson table, not assumed collisions.
source = (pathlib.Path(__file__).resolve().parents[2] / 'src/hash.c').read_text()
table = list(map(int, re.findall(r'\d+', source.split('pearson[256] = {', 1)[1].split('}', 1)[0])))
def bucket(name):
    h = 0
    for c in name.encode(): h = table[h ^ c]
    return h
names = []
for i in range(100000):
    name = 'symbol_' + str(i)
    if bucket(name) == 0: names.append(name)
    if len(names) == 16: break
assert len(names) == 16
# Each forward record is looked up again, then defined in reverse order.
text = ' assume adl=1\n org 0\n'
for name in names: text += ' dl '+name+','+name+'\n'
for i, name in reversed(list(enumerate(names))): text += name+': equ '+str(i+1)+'\n'
expected = b''.join((i+1).to_bytes(3,'little')*2 for i in range(16))
check(text, expected)
# Definitions with no forward record exercise append-at-tail directly.
text = ''.join(name+': equ '+str(i+1)+'\n' for i,name in enumerate(names))
check(text+' dl '+','.join(names)+'\n', b''.join((i+1).to_bytes(3,'little') for i in range(16)))
check(text+names[0]+': equ 42\n', error='already defined')
print('3 additional symbol cases passed')
