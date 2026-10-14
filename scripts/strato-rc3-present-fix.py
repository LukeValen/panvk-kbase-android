#!/usr/bin/env python3
"""Avoid DRM SYNC_FD export on PanVK Kbase after completing GPU work.

Keep Mesa's binary semaphore validation, pending-submit wait, permanent
payload reset and temporary payload restoration in the common path.
"""
from pathlib import Path
root = Path(__file__).resolve().parents[2]
f = root / "base/work/mesa/src/vulkan/runtime/vk_semaphore.c"
s = f.read_text()
needle = """      result = vk_sync_export_sync_file(device, sync, pFd);
      if (unlikely(result != VK_SUCCESS))
         return result;"""
replacement = """#ifdef HAVE_PAN_KMOD_KBASE
      /* STRATO-KBASE-SAFE-V3: Kbase has no DRM handle-to-fd ioctl.
       * copy_sync_payloads is NULL specifically for Kbase in our device
       * overlay. Wait for all submitted work before returning the legal
       * already-signaled SYNC_FD sentinel. Do not bypass payload reset.
       */
      if (device->physical->properties.driverID == VK_DRIVER_ID_MESA_PANVK &&
          device->copy_sync_payloads == NULL) {
         result = device->dispatch_table.DeviceWaitIdle(_device);
         if (unlikely(result != VK_SUCCESS))
            return result;
         *pFd = -1;
      } else
#endif
      {
         result = vk_sync_export_sync_file(device, sync, pFd);
         if (unlikely(result != VK_SUCCESS))
            return result;
      }"""
assert s.count(needle) == 1, "GetSemaphoreFdKHR export anchor drift"
s = s.replace(needle, replacement)
assert "Cannot export a timeline semaphore as SYNC_FD" in s
assert "vk_sync_reset(device, sync)" in s
assert "vk_semaphore_reset_temporary(device, semaphore)" in s
assert "mesa_logw" not in replacement
f.write_text(s)
print("APPLIED Strato Kbase synchronous SYNC_FD export with payload reset", flush=True)
