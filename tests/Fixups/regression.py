"""Single-pass regression cases, with independent byte and listing expectations.

Run with the normal binary or a build with -DOUTPUT_BUFFERSIZE=7 to force
cross-window reads/writes even for short instructions. No external packages.
"""
import pathlib
import re
import subprocess
import sys
import tempfile

assembler = pathlib.Path(sys.argv[1]).resolve()
options = sys.argv[2:]
checks = 0


def objects(text):
    result = bytearray()
    for line in text.splitlines():
        if re.match(r'^(?:[0-9A-F]{6}| {6}) ', line):
            for field in range(4):
                token = line[7 + 3 * field:10 + 3 * field]
                if re.fullmatch(r'[0-9A-F]{2} ', token):
                    result.append(int(token[:2], 16))
    return bytes(result)


def check(source, expected=None, error=None, files=None, flags=()):
    global checks
    with tempfile.TemporaryDirectory(prefix='ez80-fixups-') as directory:
        root = pathlib.Path(directory)
        (root / 'test.s').write_text(source)
        for name, content in (files or {}).items():
            (root / name).write_bytes(content)
        command = [str(assembler), 'test.s', '-c', *options, *flags]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True)
        if error:
            assert result.returncode == 1, (source, result.returncode, result.stdout, result.stderr)
            assert error.lower() in result.stdout.lower(), result.stdout
            assert not (root / 'test.bin').exists()
        else:
            assert result.returncode == 0, (source, result.stdout, result.stderr)
            assert (root / 'test.bin').read_bytes() == expected, (source, (root / "test.bin").read_bytes()[:32].hex(), expected[:32].hex(), result.stdout)
            if '-l' in command:
                assert objects((root / 'test.lst').read_text()) == expected, source
            if '-d' in command:
                assert objects(result.stdout) == expected, (source, (root / "test.bin").read_bytes()[:32].hex(), expected[:32].hex(), result.stdout)
                if '-l' not in command:
                    assert not (root / 'test.lst.tmp').exists()
        assert not (root / 'test.lbl').exists()
        checks += 1


# Data widths, expressions involving several unknowns, bracket evaluation and $.
check('org 0\ndb last-first\ndw last\ndl last\ndw32 last+[$-first]\nfirst: db 0\nlast: db 42\n',
      bytes.fromhex('01 0b 00 0b 00 00 07 00 00 00 00 2a'), flags=('-l', '-d'))
# Both operands deferred, signed index displacement, and opcode-embedded values.
check('ld (ix-disp), val\nbit bitno,(iy+disp)\nrst vec\nim mode\n'
      'disp: equ 3\nval: equ 42\nbitno: equ 5\nvec: equ 56\nmode: equ 2\n',
      bytes.fromhex('dd 36 fd 2a fd cb 03 6e ff ed 5e'))
check('org 0\njr target\nnop\ntarget: djnz target\n', bytes.fromhex('18 01 00 10 fe'))
check('org 0\nrelocate $8000\njr target\ndl target\ntarget: db 9\nendrelocate\n',
      bytes.fromhex('18 03 05 80 00 09'))
# Distinct global scopes and independent macro expansion scopes.
check('org 0\none: \njr @end\n@end: nop\ntwo:\njr @end\n@end: nop\n',
      bytes.fromhex('18 00 00 18 00 00'))
check('macro foo\njr @end\ndb 3\n@end: nop\nendmacro\nfoo\nfoo\n',
      bytes.fromhex('18 01 03 00 18 01 03 00'))
check('org 0\njr @f\n@@: db 1\njr @b\njr @n\n@@: db 2\n',
      bytes.fromhex('18 00 01 18 fd 18 00 02'))
check('org 0\ndl target\ninclude "child.s"\ntarget: db 7\n',
      bytes.fromhex('04 00 00 08 07'), files={'child.s': b'db value\nvalue: equ 8\n'})
# Repeated initializers have one fixup regardless of their length. Fields at
# 65535 straddle the default window boundary; final patch revisits the tail.
for size in (65535, 65536, 65537, 131075):
    check(f'blkb {size}, value\nvalue: equ 90\n', b'Z' * size)
check('org 0\ndl target\nblkb 65532, 0\ndl target\nblkb 65534, 0\ntarget: db 4\n',
      bytes.fromhex('00 00 02') + bytes(65532) + bytes.fromhex('00 00 02') + bytes(65534) + b'\x04')
# Pending DS/ALIGN spans affect file offsets, while trailing DS stays omitted.
check('org 0\ndb 1\nds 7\ndl target\nalign 16\ndb 2\ntarget: db 3\nds 100\n',
      b'\x01' + b'\xff' * 7 + bytes.fromhex('11 00 00') + b'\xff' * 5 + b'\x02\x03')
check('org 0\ndl target\nds 3\nincbin "blob"\ntarget: db 1\n',
      bytes.fromhex('09 00 00 ff ff ff') + b'abc\x01', files={'blob': b'abc'})
check('ld a, 100/[denom-1]\ndenom: equ 5\n', bytes.fromhex('3e 19'))
check('ld a, absent\n', error='identifier')
check('jr target\nblkb 128, 0\ntarget: nop\n', error='relative')
check('ld a,(ix+disp)\ndisp: equ 128\n', error='offset exceeded')
check('bit bitno,a\nbitno: equ 8\n', error='bit')
check('im mode\nmode: equ 3\n', error='interrupt')
check('rst vec\nvec: equ 9\n', error='restart')
check('ld a, 1/value\nvalue: equ 0\n', error='division by zero')
check('ds count\ncount: equ 2\n', error='identifier')
check('value: equ later\nlater: equ 3\n', error='identifier')
# Direct-symbol records share one undefined node, which a definition fills.
check('org 0\ndl later\ndl later\ndb later+1\nlater: db 9\n',
      bytes.fromhex('07 00 00 07 00 00 08 09'))
check('db value\nvalue: equ 1\nvalue: equ 2\n', error='defined')
# Fast literals retain suffix precedence, $, signed and full-width values.
check('org 0\ndb 0bh,101b,0x2a\ndw32 -1\ndl $\n',
      bytes.fromhex('0b 05 2a ff ff ff ff 07 00 00'))
# The simple resolver must report the use site, not the most recent file/scope.
check('include "child.s"\nnop\n', error='File "child.s" line 2',
      files={'child.s': b'nop\ndb missing\n'})
check('macro load\ndb missing\nendmacro\nload\n', error='Macro [load]')
check('one:\ndb @missing\ntwo:\n@missing: equ 7\n', error='identifier')
# Identical labels before the first global remain local to their source file.
check('include "a.s"\ninclude "b.s"\n', bytes([3, 4]),
      files={'a.s': b'db @value\n@value: equ 3\n',
             'b.s': b'db @value\n@value: equ 4\n'})
# Cursor writes must retain prefix/displacement ordering and pending padding.
check('org 0\ndb 0\nds 3\nld.lil ix,target\nbit bitno,(iy+disp)\ntarget: nop\n'
      'bitno: equ 5\ndisp: equ 2\n',
      bytes.fromhex('00 ff ff ff 5b dd 21 0e 00 00 fd cb 02 6e 00'))
print(f'{checks} fixup cases passed')
