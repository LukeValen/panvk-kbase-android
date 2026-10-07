# 111: event cleanups (PROGRESS item 10)

Patch: `patches/csf-v11/111-never-return-device-lost-from-set-reset-event.patch`

## SetEvent / ResetEvent
- 085 made `vkSetEvent`/`vkResetEvent` return `VK_ERROR_DEVICE_LOST` when `KBASE_IOCTL_CS_EVENT_SIGNAL` failed. The spec allows only `OUT_OF_HOST_MEMORY` and `OUT_OF_DEVICE_MEMORY` there.
- Now `panvk_event_publish()` returns void, so both calls return `VK_SUCCESS`.
- This is safe because the event word is already written and cleaned to memory before the signal. A lost notification only delays the CSF wait re-evaluation until the next scheduler tick or GPU event.
- `kbase_kmod_cs_event_signal()` already logs `kbase: KBASE_IOCTL_CS_EVENT_SIGNAL failed`.
- CreateEvent keeps its best-effort publish.
- Only `src/panfrost/vulkan/csf/panvk_vX_event.c` changes. The JM event code had no such return.

## 085/087/088 on v12-v14: documented, not gated
A static review (Sol, read-only) checked the hunks against the genxml and cs_builder sources.
- 085:
  - BASE_MEM_CSF_EVENT memory and CS_EVENT_SIGNAL are kbase ABI, not GPU generation (`kbase_kmod.c` event flag, `cs_event_signal`).
  - The ring sync offset 192 fits the 256-byte BO on every arch.
- 087:
  - It uses `cs_if/cs_else`, `cs_load32_to`, `cs_store32/64`, `cs_flush_stores`, `cs_wait_slot` and the subqueue ctx regs.
  - LOAD/STORE/BRANCH encodings match in v12/v13/v14 genxml.
  - v13 adds a SYNC_ADD32 defer default that the builder sets itself.
  - v14 drops the WAIT progress-increment field, which these helpers never set.
  - The v12+ ctx regs (r122-123) are handled in `panvk_cmd_buffer.h`.
  - The hunks are outside the `PAN_ARCH < 14` regions.
- 088: a NIR lowering plus a software record; it is not CS-generation specific.
- Tessellation, GS and conditional rendering are exposed on v10-v14 (`panvk_vX_physical_device.c:320,321,596`). Gating 087/088 would remove correctness fixes there, not add safety.
- The review also confirmed that the original 087 outer-predicate bug is already fixed by 093.

## Build / validation
- Built with 110 into `/var/tmp/panvk/dist-110/libvulkan_panfrost.so` (see the 110 worklog), universal v9-v14. No device run, because the G615 is not available.
- Tester gate (G615): deqp `dEQP-VK.api.event.*`, `dEQP-VK.synchronization.basic.event.*` and `dEQP-VK.synchronization.op.*event*` show no new failures, and PanProbe stays at 17/17.

## Risks
- If CS_EVENT_SIGNAL fails persistently, GPU waits on host-set events resume only on the next kernel re-evaluation, so stalls are possible but not errors. The logcat error line identifies this case.
