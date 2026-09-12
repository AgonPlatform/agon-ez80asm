"""Numeric-looking labels and valid literal spellings; pass a host assembler."""
import pathlib,runpy
check=runpy.run_path(str(pathlib.Path(__file__).with_name('regression.py')))['check']
check('db ordinary,2b,123abc,Finish\nordinary: equ 1\n2b: equ 2\n123abc: equ 3\nFinish: equ 4\n',bytes([1,2,3,4]))
check('dw FACEh\ndb 101b,0bh,0x2a,$2a,#2a,%101,42\n',bytes.fromhex('ce fa 05 0b 2a 2a 2a 05 2a'))
check('scope:\ndb @name\n@name: equ 7\n',bytes([7]))
check('db ordinary_missing\n',error='identifier')
check('FACEh: equ 1\n',error='label')
print('5 additional numeric-filter cases passed')
