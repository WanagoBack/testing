#!/usr/bin/env python3
"""Basic packaging checks; not an emulator/hardware test."""
import pathlib, struct
elf=pathlib.Path('WanagoBack.elf').read_bytes()
assert elf[:4]==b'\x7fELF' and struct.unpack_from('<H',elf,18)[0]==40,'Not an ARM ELF'
assert pathlib.Path('WanagoBack.3dsx').read_bytes()[:4]==b'3DSX'
p=pathlib.Path('WanagoBack.cia');data=p.read_bytes()
header,kind,version,cert,ticket,tmd,meta,content=struct.unpack_from('<IHHIIIIQ',data)
align=lambda n:(n+63)&~63
assert header>=32 and cert>0 and ticket>0 and tmd>0 and content>0
minimum=align(header)+align(cert)+align(ticket)+align(tmd)+content
assert len(data)>=minimum,'Truncated CIA'
print(f'ARM ELF, 3DSX header and CIA section bounds passed ({len(data)} bytes).')
