"""Simple CALL targets: independent bytes plus general-parser fallbacks."""
import pathlib
import runpy
ns = runpy.run_path(str(pathlib.Path(__file__).with_name('regression.py')))
check = ns['check']
for adl in (0, 1):
    width = 3 if adl else 2
    prefix = f'assume adl={adl}\norg 0\n'
    def call(value, opcode=0xcd):
        return bytes([opcode]) + value.to_bytes(width, 'little')
    for name in ('target', '_target', 'ixlong', 'p_long', 'a'*64):
        check(prefix + f'call {name}\n{name}: nop\n', call(width+1)+b'\0', flags=('-l',))
        check(prefix + f'{name}: nop\ncall {name}\n', b'\0'+call(0))
    check(prefix + 'FACEh: equ 7\ncall FACEh\n', error='Invalid label')
    check(prefix + 'call FACEh\n', call(0xface))
    check(prefix + 'call target\ncall target\ntarget: nop\n', call(2*(width+1))*2+b'\0')
    check(prefix + 'call target+1\ntarget: nop\n', call(width+2)+b'\0')
    check(prefix + 'call nz,target\ntarget: nop\n', call(width+1,0xc4)+b'\0')
    check(prefix + 'scope:\ncall @local\n@local: nop\n', call(width+1)+b'\0')
    check(prefix + 'macro invoke\ncall target\nendmacro\ninvoke\ntarget: nop\n', call(width+1)+b'\0')
    check(prefix + 'if 0\ncall missing_name\nendif\nnop\n', b'\0')
    check(prefix + 'call missing_name\n', error='identifier')
print('38 additional simple-CALL cases passed')
