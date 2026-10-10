#!/usr/bin/env python3
"""Apply LukeValen Switch / Safety v2 overlay on Guna PanVK 1.1.0-FC."""
from pathlib import Path
import re, subprocess
root = Path(__file__).resolve().parents[2]
mesa = root / "base" / "work" / "mesa"
safety = root / "safety"
assert (mesa / "src/vulkan/runtime/vk_android.c").exists(), mesa
def patch(name, relative, marker, target):
    file = mesa / target
    if marker in file.read_text():
        print("ALREADY_PRESENT", name, flush=True)
        return
    patchfile = safety / relative
    test = subprocess.run(["git","-C",str(mesa),"apply","--check",str(patchfile)], capture_output=True,text=True)
    if test.returncode:
        raise RuntimeError(f"{name}: patch drift; cannot safely proceed:\n{test.stderr}")
    subprocess.run(["git","-C",str(mesa),"apply",str(patchfile)],check=True)
    assert marker in file.read_text(), f"Postcondition failed: {name}"
    print("APPLIED", name, flush=True)
# Guna's android/008 covers AHardwareBuffer but NOT VkNativeBufferANDROID.
patch("MediaTek/Android native-buffer FD finder",
      "patches/android/016-anb-dma-buf-finder.patch",
      "[ANB-FD] probing", "src/vulkan/runtime/vk_android.c")
patch("Kbase skip DRM syncobj copy callback",
      "patches/csf-v11/103-disable-drm-sync-copy-on-kbase.patch",
      "physical_device->kbase_node_path[0]", "src/panfrost/vulkan/panvk_vX_device.c")
patch("Safe synchronous native-buffer release",
      "patches/csf-v11/105-eden-kbase-synchronous-release.patch",
      "[ANB-SYNC] Eden/Kbase synchronous release", "src/vulkan/runtime/vk_android.c")
patch("Keep GPU command stream 64-bit address",
      "patches/lemon-rc2/109-preserve-64bit-cs-address.patch",
      "uint64_t fn_addr =", "src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c")
f = mesa / "src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c"
data = f.read_text()
p = r'(DEBUG_GET_ONCE_BOOL_OPTION\(kbase_gpu_semaphore_waits,\s*"PANVK_KBASE_GPU_SEMAPHORE_WAITS",\s*)(true|false)(\s*\))'
data, n = re.subn(p, lambda m: m.group(1)+"false"+m.group(3), data)
assert n == 1, f"Unsafe GPU semaphore gate drift: {n}"
f.write_text(data)
print("SAFETY Kbase GPU cross-subqueue waits OFF by default",flush=True)
vk = (mesa/"src/vulkan/runtime/vk_android.c").read_text()
assert "QueueWaitIdle(_queue)" in vk and "*pNativeFenceFd = -1;" in vk
assert "[ANB-FD] probing" in vk
assert "physical_device->kbase_node_path[0]" in (mesa/"src/panfrost/vulkan/panvk_vX_device.c").read_text()
assert "uint64_t fn_addr =" in (mesa/"src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c").read_text()
print("ALL SAFETY POSTCONDITIONS PASSED",flush=True)
