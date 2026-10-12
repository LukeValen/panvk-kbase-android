#!/usr/bin/env python3
"""RC3 Strato conservative Kbase sync-file export workaround.

The observed Celeste fatal is preceded by VK_ERROR_UNKNOWN:
vk_drm_syncobj_export_sync_file attempted DRM_IOCTL on mali_kbase.
When an Android Kbase logical device exports a SYNC_FD, first drain all GPU
work and return the legal already-signaled fence sentinel (-1).
The fallback does not enable GPU-side cross-subqueue waits.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
f=root/"base/work/mesa/src/vulkan/runtime/vk_semaphore.c"
s=f.read_text()
needle="""   struct vk_sync *sync = vk_semaphore_get_active_sync(semaphore);

   VkResult result;
   switch (pGetFdInfo->handleType) {"""
assert s.count(needle)==1, f"Expected single GetSemaphoreFdKHR anchor, found {s.count(needle)}"
replacement="""   struct vk_sync *sync = vk_semaphore_get_active_sync(semaphore);

#ifdef HAVE_PAN_KMOD_KBASE
   /* Strato RC3 Safety v3. The Mali Kbase device has no DRM syncobj
    * handle-to-fd ioctl. This is deliberately synchronous until the
    * cross-CSG sync-fd path is safely proven. -1 is the valid already-
    * signaled SYNC_FD value once DeviceWaitIdle has completed.
    *
    * Distinct from OPAQUE_FD: never spoof an opaque FD with -1.
    */
   if (device->physical->properties.driverID == VK_DRIVER_ID_MESA_PANVK &&
       device->copy_sync_payloads == NULL &&
       pGetFdInfo->handleType ==
          VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT) {
      VkResult idle_result = device->dispatch_table.DeviceWaitIdle(_device);
      if (idle_result != VK_SUCCESS)
         return idle_result;

      /* Match the copy-payload reset semantics for a native Kbase sync.
       * An unexpected non-Kbase sync is reported instead of issuing an
       * unsupported DRM ioctl. Keep it alive for diagnosis.
       */
      if (sync->type == device->physical->supported_sync_types[0]) {
         if (sync == &semaphore->permanent) {
            VkResult reset_result = vk_sync_reset(device, sync);
            if (reset_result != VK_SUCCESS)
               return reset_result;
         } else {
            vk_semaphore_reset_temporary(device, semaphore);
         }
      } else {
         mesa_logw("[STRATO-KBASE-SAFE-V3] Unexpected sync type on Kbase "
                   "SYNC_FD export; DeviceWaitIdle completed, no DRM ioctl");
      }
      *pFd = -1;
      return VK_SUCCESS;
   }
#endif

   VkResult result;
   switch (pGetFdInfo->handleType) {"""
s=s.replace(needle,replacement)
assert "STRATO-KBASE-SAFE-V3" in s and s.count(needle)==0
assert s.count("VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT")==s.count("VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT")
f.write_text(s)
print("APPLIED Strato Kbase synchronous GetSemaphoreFdKHR safety v3",flush=True)
