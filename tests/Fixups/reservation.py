"""Exercise reservation threshold and fallback with tiny output windows.
Run: python3 tests/Fixups/reservation.py
Requires a host C compiler with address/undefined-behavior sanitizers.
"""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='reserve-', dir='/tmp') as folder:
    for size in (7, 15, 16, 17):
        binary = Path(folder) / ('asm' + str(size))
        subprocess.run(['cc', '-DUNIX', '-DOUTPUT_BUFFERSIZE='+str(size),
                        '-O1', '-g', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', '-Wno-deprecated-declarations',
                        *map(str, (root/'src').glob('*.c')), '-o', str(binary)], check=True)
        subprocess.run(['python3', str(root/'tests/Fixups/regression.py'), str(binary)],
                       env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'), check=True)
        print('Output window', size, 'passed', flush=True)
