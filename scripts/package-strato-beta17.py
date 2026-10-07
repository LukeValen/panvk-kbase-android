#!/usr/bin/env python3
"""Package the rebuilt beta.17 for Strato's strict metadata reader."""
import hashlib,json,subprocess,zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=root/'dist/android-g615-v11-csf/libvulkan_panfrost.so'
out=root/'dist/strato-beta17';out.mkdir(parents=True,exist_ok=True)
data=source.read_bytes()
assert data[:6]==b'\x7fELF\x02\x01' and int.from_bytes(data[18:20],'little')==183,'Expected little-endian AArch64 ELF64'
meta=dict(schemaVersion=1,name='PanVK G615 Strato Beta 17 ANB',author='zenithblue-oss / GunaCharanTeja; Strato ANB integration: LukeValen',packageVersion='0.1.0-beta.17-strato.1',vendor='Mesa',driverVersion='Mesa 26.3.0-devel / PanVK-kbase beta.17 + Strato ANB1',minApi=35,description='Beta 17 rebuilt with compatible dma-buf selection for the Android native-buffer path used by Strato. Experimental; game validation required.',libraryName='libvulkan_panfrost.so')
meta_bytes=(json.dumps(meta,indent=2)+'\n').encode()
archive=out/'PanVK-G615-Strato-Beta17-ANB1.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
 z.writestr('meta.json',meta_bytes)
 z.writestr('libvulkan_panfrost.so',data)
with zipfile.ZipFile(archive) as z:
 assert set(z.namelist())=={'meta.json','libvulkan_panfrost.so'}
 assert z.testzip() is None
 assert json.loads(z.read('meta.json'))==meta
 assert z.read('libvulkan_panfrost.so')==data
patch=root/'patches/android/016-strato-native-buffer-fd.patch'
manifest=dict(upstreamRepository='https://github.com/zenithblue-oss/panvk-kbase-android',upstreamCommit='b4ee2e2490ffad0e6625ecbd78ef016bdf6cf5ce',upstreamTag='g615-v11-csf-v0.1.0-beta.17',sourceCommit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),mesaCommit=json.loads((root/'sources.lock').read_text())['mesaCommit'],patchSha256=hashlib.sha256(patch.read_bytes()).hexdigest(),driverSha256=hashlib.sha256(data).hexdigest(),archiveSha256=hashlib.sha256(archive.read_bytes()).hexdigest(),validation={'hostAnbRegressionTests':'passed','hardwareGameValidation':'pending'})
(out/'SOURCE.json').write_text(json.dumps(manifest,indent=2)+'\n')
(out/'SHA256SUMS.txt').write_text(f"{manifest['archiveSha256']}  {archive.name}\n{manifest['driverSha256']}  libvulkan_panfrost.so\n")
print(archive)
print(json.dumps(manifest,indent=2))
