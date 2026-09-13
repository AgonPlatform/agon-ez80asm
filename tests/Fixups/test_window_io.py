"""Build a host stdio probe; verify the in-memory path and I/O-error cleanup.
Run from any directory: python3 tests/Fixups/test_window_io.py
"""
import os
import re
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
cc = os.environ.get('CC', 'cc')
with tempfile.TemporaryDirectory(prefix='ez80-window-') as directory:
    root = Path(directory)
    flags = ['-O1', '-DUNIX', '-DOUTPUT_BUFFERSIZE=65536UL', '-Wno-deprecated-declarations', '-I' + str(repo / 'src')]
    subprocess.run([cc, *flags, '-include',
                    str(repo / 'tests/Fixups/io_probe_redirect.h'), '-c',
                    str(repo / 'src/io.c'), '-o', str(root / 'io.o')], check=True)
    sources = [str(p) for p in (repo / 'src').glob('*.c') if p.name != 'io.c']
    subprocess.run([cc, *flags, '-Dmain=assemblerMain', *sources,
                    str(repo / 'tests/Fixups/io_probe.c'), str(root / 'io.o'),
                    '-o', str(root / 'probe')], check=True)
    environment = dict(os.environ)
    environment.pop('EZ80_IO_FAIL', None)
    for size in (65535, 65536, 65537, 131075):
        (root / 'test.s').write_text(f'blkb {size}, value\nvalue: equ 42\n')
        result = subprocess.run([str(root / 'probe'), 'test.s', '-c', '-m'],
                                cwd=root, env=environment, text=True, capture_output=True)
        assert result.returncode == 0, result.stdout + result.stderr
        assert (root / 'test.bin').read_bytes() == b'*' * size
        counts = re.search(r'PROBE reads=(\d+) writes=(\d+) failed=(\d+)', result.stdout)
        assert counts, result.stdout + result.stderr
        assert counts[3] == '0', result.stdout
        if size > 65536:
            assert int(counts[1]) > 0, ('read hook was bypassed', result.stdout)
        if size <= 65536:
            assert 'PROBE reads=0 writes=1' in result.stdout, result.stdout
    for failure in ('read', 'write', 'seek', 'close'):
        environment = dict(os.environ, EZ80_IO_FAIL=failure)
        result = subprocess.run([str(root / 'probe'), 'test.s', '-c', '-m'],
                                cwd=root, env=environment, text=True, capture_output=True)
        assert 'failed=1' in result.stdout, (failure, 'failure hook was not reached', result.stdout, result.stderr)
        assert result.returncode == 1, (failure, result.returncode, result.stdout, result.stderr)
        assert not (root / 'test.bin').exists(), failure
    print('Window I/O: resident outputs need no reads and one write; all four I/O failure cases clean up')
