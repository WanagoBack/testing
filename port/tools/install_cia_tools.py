#!/usr/bin/env python3
"""Download version-pinned public CIA packaging tools into tools/bin."""
import io, os, pathlib, urllib.request, zipfile
DEST=pathlib.Path(__file__).resolve().parent/'bin'
DEST.mkdir(exist_ok=True)
RELEASES={
 'makerom':'https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.18.4/makerom-v0.18.4-ubuntu_x86_64.zip',
 'bannertool':'https://github.com/diasurgical/bannertool/releases/download/1.2.0/bannertool.zip',
}
for name,url in RELEASES.items():
 with urllib.request.urlopen(url,timeout=120) as r:
  archive=zipfile.ZipFile(io.BytesIO(r.read()))
 candidates=[p for p in archive.namelist() if pathlib.PurePosixPath(p).name==name and ('linux' in p.lower() or name=='makerom')]
 if name=='bannertool':
  preferred=[p for p in candidates if '64' in p]
  if preferred:candidates=preferred
 if len(candidates)!=1:raise RuntimeError(f'Ambiguous {name} executable: {candidates}')
 output=DEST/name
 output.write_bytes(archive.read(candidates[0]));output.chmod(0o755)
 print('Installed',name)
