"""Suffix encodings and diagnostics; pass a host assembler binary."""
import pathlib, runpy
ns=runpy.run_path(str(pathlib.Path(__file__).with_name('regression.py')))
check=ns['check']
forms={'sis':(0x40,2),'lis':(0x49,2),'sil':(0x52,3),'lil':(0x5b,3)}
for adl in (0,1):
    aliases=({'s':'sis','l':'lis','is':'sis','il':'sil'} if not adl else
             {'s':'sil','l':'lil','is':'lis','il':'lil'})
    for suffix in list(forms)+list(aliases):
        prefix,width=forms[aliases.get(suffix,suffix)]
        expected=bytes([prefix,0x21])+bytes.fromhex('34 12 00')[:width]
        # Uppercase spelling must continue to use the same explicit parser.
        check('assume adl='+str(adl)+'\nld.'+suffix.upper()+' hl,$1234\n',expected)
    check('assume adl='+str(adl)+'\nld hl,$1234\n',bytes([0x21])+bytes.fromhex('34 12 00')[:3 if adl else 2])
check('ld.xyz hl,0\n',error='suffix')
check('ld. hl,0\n',error='suffix')
print('20 additional suffix cases passed')
