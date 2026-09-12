"""Common-mnemonic fast-path cases; pass a host assembler binary."""
import itertools
import pathlib
import runpy
ns = runpy.run_path(str(pathlib.Path(__file__).with_name('regression.py')))
check = ns['check']
count = 0
for adl in (0, 1):
    width = 3 if adl else 2
    for word in ('ld', 'call', 'jp'):
        for letters in itertools.product(*[(c, c.upper()) for c in word]):
            name = ''.join(letters)
            if word == 'ld':
                instruction = name + ' a,42'
                expected = bytes.fromhex('3e 2a 00')
            else:
                instruction = name + ' target'
                expected = bytes([0xcd if word == 'call' else 0xc3]) + (1 + width).to_bytes(width, 'little') + b'\0'
            check(f'assume adl={adl}\norg 0\n{instruction}\ntarget: nop\n', expected)
            count += 1
for name in ('l', 'c', 'j', 'ca', 'cal', 'ldx', 'callx', 'jpx'):
    check(name + '\n', error='Invalid mnemonic')
    check(f'macro {name}\nnop\nendmacro\n{name}\n', b'\0')
    count += 2
for name in ('ld', 'CALL', 'jp'):
    check(f'macro {name}\nnop\nendmacro\n', error='Macro already defined')
    count += 1
check('.ld a,1\n', error='Invalid mnemonic')
print(f'{count + 1} additional common-mnemonic cases passed')
