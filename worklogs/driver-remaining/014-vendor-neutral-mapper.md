# 014: Vendor-neutral gralloc mapper + HIDL mapper4 backend (PROGRESS item 15)

Patch: `patches/android/014-vendor-neutral-gralloc-mapper-and-hidl-mapper4.patch` (was `109-...` before 2026-10-05), applied on top of `013-vendor-mapper-metadata.patch`. Not committed or released.

## Bug

013 loaded only `mapper.mediatek.so` (AIMapper stable-C v5) by hard-coded name. On non-MediaTek vendors (Tensor, QTI) and on vendor API < 34 (only HIDL `android.hardware.graphics.mapper@4.0-impl-*.so`), the load failed and `panvk_v19_init_mapper()` returned `-ENOTSUP`. The layout was then refused, and `vk_android.c:152` returned `VK_ERROR_INVALID_EXTERNAL_HANDLE` from `vkCreateSwapchainKHR` and AHB import.

## Stage 1: vendor-neutral AIMapper v5 discovery

Candidate names are collected in this order and de-duplicated, up to 16 names:

1. `PANVK_MAPPER_NAMES` (comma-separated list).
2. Names from `/vendor/lib64/hw` and `/odm/lib64/hw`: `mapper.<n>.so` and `android.hardware.graphics.mapper@4.0-impl-<n>.so`. App-domain listing works (checked with run-as).
3. `ro.hardware.gralloc`.
4. `mediatek`, in case the directories cannot be listed.

For each name, the patch tries `AServiceManager_openDeclaredPassthroughHal("mapper", n)`, then SPHAL, then `dlopen`, and calls `AIMapper_loadIMapper` (version >= 5). This is the same loader order as libui `Gralloc5`.

The IAllocator AIDL `getIMapperLibrarySuffix` binder query was not used. The directory scan finds the same suffix without hand-written binder parceling. Add the binder query only if a device ships a mapper that the scan cannot see.

## Stage 2: HIDL IMapper@4.0 backend

The upstream `u_gralloc_imapper4_api.cpp` needs libhidlbase, libgralloctypes and libui. None of these are in the NDK, and they are not loadable from an app linker namespace, so that backend cannot be used.

Instead, the patch loads the passthrough impl through SPHAL and calls `HIDL_FETCH_IMapper("default")`. It then calls the frozen IMapper@4.0 aarch64 C++ ABI from C:

- vtable slots: importBuffer=14, freeBuffer=15, get=23. Slots 0-12 are IBase plus 2 destructors.
- `Return<T>` is returned through the x8 sret. The exception code is at +0, `Error` at +0x24, and the size is 0x28.
- Callbacks are a fake libc++ `std::function`: a 32-byte buffer with `__base*` at +0x20. The vtable holds D1, D0, `__clone()`, `__clone(p)`, destroy, destroy_deallocate, operator(), target and target_type.

Layout sources:
- Relocations of the `arm::mapper::GrallocMapper` vtable in the TB336FU `@4.0-impl-mediatek.so`.
- Disassembly of `freeBuffer`, `HIDL_FETCH_IMapper` and `importBuffer`. The first try assumed a 24-byte std::function buffer and crashed in `importBuffer+56`; the disassembly showed `__f_` at +0x20, and the fix was taken from that.

The `get()` blobs use the same gralloc4 encoding as `getStandardMetadata`, so the 013 decoder is reused unchanged. Incomplete metadata is still refused (no `AHardwareBuffer_describe` guessing). The mapper4 path is built only for `__aarch64__`.

## Validation

- Build: universal Android ICD (`g615-v11-csf` + jm-v9) OK from worktree `/var/tmp/panvk/wt-mapper` (pinned 5a07217f + series + mapper patch). Output: `/var/tmp/panvk/dist-mapper/libvulkan_panfrost.so`. No new warnings in `u_gralloc_fallback.c`. NEEDED is unchanged (liblog, libnativewindow, libsync, libm, libz, libdl, libc). The only new imports are the libc functions `opendir`, `readdir` and `__system_property_get`.
- Combined build (2026-10-05): worktree `/var/tmp/panvk/wt-combined` (pin + full series incl. csf-v11/108 + android/014 + jm-v9, 112 patches), v6/v7/v9-v14 libs. Output `/var/tmp/panvk/dist-combined/libvulkan_panfrost.so`, SHA256 `0590dc40bbfe0cf9361d4fb3aac774c933a34514b34ee03c8905f08b1211eb84`, BuildID `f0ea02795fa4793c62ccd9e776e40183c490ade6`. Device smoke not run (tablet.lock held).
- Host build needs `LD_LIBRARY_PATH=/var/tmp/panvk/llvm22/usr/lib HOST_TOOLS=tmp/rel9/src/build/host-tools/bin`. `build/host-tools` is stale (panfrost_compile asserts).
- Standalone NDK test (`/var/tmp/panvk/mapper-re/mtest.c`): it allocates a 256x128 RGBA AHB, then calls `u_gralloc_get_buffer_basic_info` 3 times with the FALLBACK backend.
  - Lenovo TB336FU (MT8755, Mali-G57, vendor API 33, Arm gralloc4): `init backend=hidl-mapper4 name=mediatek how=SPHAL`. Result: fourcc AB24, modifier `0x0800000000000351` (AFBC), stride 1024, alloc 135168, rc=0. It passes as shell and as app domain (run-as panvktest). With `PANVK_MAPPER_NAMES=bogus,mediatek`, the bogus name is skipped.
  - OnePlus CPH2691 (SM8650, QTI mapper4): `init backend=hidl-mapper4 name=qti-display how=SPHAL`. Result: linear, stride 1024, rc=0. This confirms the ABI on a second, non-Arm gralloc4.
- Not run: the full ICD in panvk-test or PanProbe `swapchain_lifecycle`. The dev G615 was not connected, and the tablet was locked (`tablet.lock`) by the BC agent. The v5 (mediatek) path was not run on a device; only the loader changed there.

## Open

- Re-run panvk-test/PanProbe `swapchain_lifecycle` with this ICD on: the dev G615 (expect `backend=aimapper name=mediatek`), TB336FU (expect `hidl-mapper4`), and stock-ROM G615, G610 and Pixel 8 testers.
- Tensor (`mapper.pixel.so`?) and other vendors are untested. Ask testers for `ls /vendor/lib64/hw | grep mapper` and the `init backend=` logcat line.
- Known limits: names are de-duplicated but not sorted, so the first v5 match wins. A plain `android.hardware.graphics.mapper@4.0-impl.so` with no suffix is not probed. HIDL status messages are not freed when the exception is non-zero (never seen in passthrough mode).

Final combined build (2026-10-05): `/var/tmp/panvk/dist-final/libvulkan_panfrost.so` sha `12d49610...`, BuildID `479f5f57...`. TB336FU panvk-test (imported ICD) **FAIL**. In the app's `clns-9` namespace, `libvndksupport.so` and `/vendor/lib64/hw/android.hardware.graphics.mapper@4.0-impl-mediatek.so` are "not accessible for the namespace", so neither SPHAL nor the direct dlopen works. `isDeclared` for mapper/mediatek is SELinux-denied. The log shows `mapper load failed (2 candidate names, first=mediatek)`, then `swapchain_lifecycle` fails with `vkCreateSwapchainKHR` -1000072003, the same as the baseline. The mtest proof (standalone exe, run-as) does not cover the app linker namespace. Logs: `/var/tmp/panvk/final-logcat.txt`, `/var/tmp/panvk/final-logs/`.

## Stage 3: in-app fallback (revision, 2026-10-05)

Root cause: an untrusted app process has no way to reach the vendor mapper on vendor API < 34. In the app's `clns-N` namespace, `libvndksupport.so` and the `/vendor/lib64/hw` impl are not accessible. `servicemanager isDeclared` is SELinux-denied. A replacement for `android_load_sphal_library` was tried and failed: `android_get_exported_namespace` via `libdl_android.so` and `__loader_android_get_exported_namespace` via `RTLD_DEFAULT` both resolve to NULL in the app (`get_ns=0x0`), so it was dropped. On G615 / vendor API >= 34, AIMapper v5 loads through `AServiceManager_openDeclaredPassthroughHal` inside libbinder_ndk, which runs in the system namespace. That path is unchanged.

The in-app swapchain is panvk's own Android WSI (`wsi/016`, `panvk_wsi.c`), not the platform loader swapchain. It allocates an AHB with `GPU_COLOR_OUTPUT | GPU_SAMPLED_IMAGE | CPU_READ_OFTEN`, imports it, and the failure is at `vk_android_get_ahb_layout`. The ANB / `GetSwapchainGrallocUsage` path is not used there, so it was left alone.

Fix (`android/014` vk_android.c/.h hunks, plus `wsi/017`): when u_gralloc refuses, `vk_android_probe_linear_ahb()` runs. It only runs when all of these hold:
- The AHB is the one the swapchain just allocated. A thread-local is set via `vk_android_set_owned_ahb()` right after `AHardwareBuffer_allocate` and cleared after the loop and in destroy. App-supplied AHBs are never written.
- The AHB is single-layer R8G8B8A8 or R8G8B8X8, with CPU read usage and `stride >= width`.
- The dma-buf is the first handle fd with a `lseek` size, which is the same rule as `panvk_android_find_dma_buf_fd`. On this Arm gralloc, data[0] has no size. The dma-buf size must be `>= stride*4*height`.

The probe writes 9 cookies (corners and center) through its own mmap at `y*stride*4 + x*4`, inside `DMA_BUF_IOCTL_SYNC` write brackets. It then reads them through `AHardwareBuffer_lock`. libnativewindow imports with the platform mapper in the system namespace. By the NDK contract, the lock view is linear with `desc.stride`. Only 9/9 gives `DRM_FORMAT_MOD_LINEAR`, offset 0, `rowPitch = stride*4`. Anything else is refused. Log: `[P0A-V19-FULLPLANE] backend=cpu-linear-probe WxH fmt= stride= size= lock_rc= match=N/9 -> LINEAR|<reason>`.

Result on TB336FU in panvk-test (imported ICD `/var/tmp/panvk/dist-final2/libvulkan_panfrost.so`, SHA256 `43f5b344...c966`, BuildID `3ff6c489...`):
- Probe: `1520x320 fmt=1 stride=1520 size=1945600 lock_rc=0 match=9/9 -> LINEAR`. The size equals exactly `1520*320*4`, so the buffer is not AFBC.
- `swapchain_lifecycle` PASS: 90.6 FPS, resize and recreate OK.
- `autorun all`: 2/17 (was 1/17). No crashes.

Afterwards the original driver was restored (sha `c0e54909...` checked) and the lock was released. Log: `/var/tmp/panvk/final2-logcat.txt`.

Risks / limits:
- The probe proves that the CPU-lock view equals the raw dma-buf at a linear pitch. It relies on gralloc honouring the lock contract, which means no CPU lock that hands back raw AFBC memory. Arm gralloc does not allocate AFBC with CPU usage.
- The fallback covers only panvk's WSI swapchain. Imported app AHBs, video, YUV and AFBC are still refused without a mapper.
- The swapchain AHB is CPU_READ_OFTEN (cached), which was already the case before this change.
- Not tested: dev G615 (AIMapper path, expected unchanged), SM8650 in-app, and the platform-loader ANB path.
- The visual output was not checked: the screenshot taken mid-test shows a black surface, which may be the test's clear color.
