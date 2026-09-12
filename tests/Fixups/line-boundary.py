"""Source-line capacity, including LF and zero, in both buffering modes."""
import pathlib
import runpy
ns = runpy.run_path(str(pathlib.Path(__file__).with_name('regression.py')))
check = ns['check']
count = 0
for mode in ((), ('-m',)):
    for listing in ((), ('-l',)):
        flags = mode + listing
        for ending in ('\n', '\r\n', ''):
            # CR is counted among the bytes preceding LF by the existing reader.
            content = 255 if ending == '\r\n' else 256
            line = 'db 7 ;' + 'x' * (content - 6) + ending
            check(line, b'\x07', flags=flags)
            check('include "child.s"\n', b'\x07', files={'child.s': line.encode()}, flags=flags)
            count += 2
        for ending in ('\n', '\r\n'):
            content = 255 if ending == '\r\n' else 256
            line = 'db 7 ;' + 'x' * (content - 6) + ending
            check('macro foo\n' + line + 'endmacro\nfoo\nfoo\n', b'\x07\x07', flags=flags)
            count += 1
        # Cross a refill boundary before reading a maximum-length line.
        check(';\n'*2040 + 'db 7 ;' + 'x'*250 + '\n', b'\x07', flags=flags)
        count += 1
        for ending in ('\n', '\r\n', ''):
            content = 256 if ending == '\r\n' else 257
            check(';' + 'x'*(content-1) + ending, error='line too long', flags=flags)
            count += 1
print(f'{count} additional line-boundary cases passed')
