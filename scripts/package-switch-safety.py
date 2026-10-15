#!/usr/bin/env python3
"""Create a strict nine-field AdrenoTools package accepted by Strato."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import zipfile

blob = Path(sys.argv[1]).read_bytes()
assert blob[:6] == b'\x7fELF\x02\x01', 'Expected little-endian ELF64'
assert struct.unpack_from('<H', blob, 18)[0] == 183, 'Expected AArch64'
meta = {
    'schemaVersion': 1,
    'name': 'PanVK 1.1.4-FC LukeValen Switch Safety',
    'author': 'LukeValen (Switch Safety), GunaCharanTeja (1.1.4-FC), Mesa',
    'packageVersion': '1.1.4-FC-switch-safety.1',
    'vendor': 'Mesa',
    'driverVersion': 'Mesa 26.3.0-devel / PanVK 1.1.4-FC Switch Safety.1',
    'minApi': 35,
    'description': 'Experimental Switch emulator build: Android DMA-BUF selection, conservative Kbase synchronization, 64-bit CS addresses. Synchronous presentation may reduce performance. Strato/Eden/Lemon runtime validation pending.',
    'libraryName': 'libvulkan_panfrost.so',
}
output = Path('PanVK-1.1.4-FC-LukeValen-Switch-Safety.zip')
with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    z.writestr('meta.json', json.dumps(meta, indent=2) + '\n')
    z.writestr(meta['libraryName'], blob)
with zipfile.ZipFile(output) as z:
    assert z.testzip() is None
    assert set(z.namelist()) == {'meta.json', meta['libraryName']}
    assert z.read(meta['libraryName']) == blob
    assert json.loads(z.read('meta.json')) == meta and len(meta) == 9
hashes = f'{hashlib.sha256(output.read_bytes()).hexdigest()}  {output.name}\n{hashlib.sha256(blob).hexdigest()}  {meta["libraryName"]}\n'
Path('SHA256SUMS.txt').write_text(hashes)
print('VALIDATED', output, output.stat().st_size)
print(hashes)
