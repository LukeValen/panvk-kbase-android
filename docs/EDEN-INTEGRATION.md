# Eden Android Integration

## Scope

This fork currently targets **Eden Android Nintendo Switch emulation** on
Mali-G615/Kbase-CSF devices. It is an experimental integration branch derived
from `GunaCharanTeja/panvk-kbase-android`.

The driver is not currently presented as a general-purpose Winlator, X11,
GameHub, or desktop driver. Those upstream use cases remain part of the source
history, but Eden is the active target of this fork.

## Required Eden build

A **specific experimental Eden build** is required because stock/public Eden
builds do not currently provide the Mali custom-driver loading path used by
this project.

That Eden build is intentionally **not distributed here yet**. The driver
repository documents the requirement but does not ship, attach, or link the
experimental Eden APK.

## Reference device

- Device: Poco X6 Pro
- SoC: MediaTek Dimensity 8300 Ultra
- GPU: Mali-G615 MC6
- Mesa architecture: Pan arch v11
- Frontend: CSF
- Kernel interface: `/dev/mali0`
- Kbase UAPI observed during development: CSF 1.21
- Android driver ABI: arm64-v8a / Bionic

## Eden-specific driver work

The current patch stack includes work needed by Eden's Android Vulkan path:

1. **MediaTek mapper metadata**
   - obtains plane, allocation, fourcc and modifier metadata through the
     Android vendor mapper path;
   - handles the MediaTek allocation layout seen on the Poco X6 Pro.

2. **Android Native Buffer DMA-BUF discovery**
   - does not assume `native_handle_t->data[0]` is the DMA-BUF;
   - probes the native handle FDs with `GetMemoryFdPropertiesKHR`;
   - on the reference device, index 0 is not importable while index 1 is the
     valid DMA-BUF;
   - this removed the previous `VK_ERROR_INVALID_EXTERNAL_HANDLE` during
     native-buffer image creation.

3. **Kbase release synchronization**
   - instruments `VK_ANDROID_native_buffer` release synchronization;
   - avoids using `vk_drm_syncobj_copy_payloads` on a Kbase logical device;
   - routes release synchronization through `QueueSubmit2` followed by
     `GetSemaphoreFdKHR` and the Kbase `sync_file` exporter.

4. **KCPU sync-file export diagnostics**
   - traces KCPU queue creation and enqueue operations;
   - traces CQS targets and fence export results;
   - preserves diagnostics while presentation stability is being validated.

## Current status

Confirmed on the reference device:

- Eden can load `libvulkan_panfrost.so` as the selected custom Vulkan driver.
- The Vulkan device is reported as Mali-G615 MC6 with Mesa PanVK.
- MediaTek mapper metadata is accepted.
- Native-buffer DMA-BUF import succeeds after probing the correct handle FD.
- The old `GetMemoryFdPropertiesKHR` / `VK_ERROR_INVALID_EXTERNAL_HANDLE`
  blocker is no longer the current failure.

The next presentation blocker was isolated to the native-buffer release path.
Before the Kbase-specific fix, logs showed:

```text
[ANB-SYNC] release path=copy_sync_payloads
[ANB-SYNC] copy_sync_payloads result=-13
QueueSignalReleaseImageANDROID failed: -13
```

Patch `csf-v11/103-disable-drm-sync-copy-on-kbase.patch` changes Kbase devices
so this DRM-syncobj payload-copy path is not selected. The resulting
`QueueSubmit2 -> GetSemaphoreFdKHR -> Kbase sync_file` path is the active
development path and remains under on-device validation.

## Known issues

- The current branch is experimental and may still show black frames, surface
  presentation stalls, or crashes depending on the game.
- A frame may become visible when emulation is paused, which points to
  presentation/release synchronization rather than a total rendering failure.
- MediaTek gralloc may log unsupported candidate formats such as `0x38` and
  `0x3b`; the accepted mapper metadata path is logged separately.
- Do not treat a successful driver load as proof that all Switch workloads are
  stable.
- Do not advertise this branch as conformant or production-ready without the
  required runtime validation.

## Eden-optimized build

The Eden CI target is intentionally narrower than the upstream multi-consumer
build:

- Android WSI only (`platforms=android`);
- Kbase only (`panfrost-kmds=kbase`);
- no X11 WSI in the Eden artifact;
- no Panthor KMD in the Eden artifact;
- release build with assertions disabled after patch application;
- flat custom-driver package containing `libvulkan_panfrost.so` and `meta.json`.

This reduces unrelated code and keeps the artifact focused on Eden's in-process
Android Vulkan path. Correctness and crash fixes take priority over aggressive
compiler optimization until presentation is stable.

## Development policy

- Fix real runtime failures instead of spoofing feature bits.
- Keep diagnostic patches while a blocker is active.
- Every new presentation/crash fix must be tested on the reference device.
- The experimental Eden APK remains private/unpublished until the integration
  is ready for public testing.
- Preserve attribution to Mesa, PanVK, Kbase work, and the upstream
  `GunaCharanTeja/panvk-kbase-android` project.
