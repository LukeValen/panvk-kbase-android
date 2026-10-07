# Mali v10 (Valhall CSF) status

All file:line references throughout this document are relative to the Mesa root of the beta.16 tree (Mesa 5a07217f + csf-v11 series up to 107 + jm-v9 001-003), written as `src/panfrost/...:NNN`, unless marked "beta.17 tree" (the unreleased beta.17 candidate: beta.16 + csf-v11 108-118 + android/014 + wsi/017 + jm-v9 004-005).

## Summary
Update 2026-10-06: a third v10 device, vivo V2284A (MT6896Z/CZA, Mali-G610 MC6, Linux 5.10.233 android12, `5b799ebb`, beta.15), ran a user D3D game in PanPlay. DXVK skipped the adapter for missing `textureCompressionBC` and the game exited with code 3 after 4 s. This is the same blocker as on the other 5.10 G610 MC6. The unreleased patch 108 targets it. The G57 dev smoke of 108 confirms the partial-native mask theory (`TEXTURE_FEATURES[0] = 0xf7fe03fe`, BC1-BC3 native only), but no G610 mask has been logged yet.

The first v10 PanProbe run arrived after the beta.16 snapshot. A Mali-G610 MC4 (23090RA98I, MT6886, Linux 5.15 android13) scores 15/17 on beta.15 (`854bef5b`). `vkCreateDevice`, CSF queue groups, tiler heaps, shader upload and submission all work, including geometry, tessellation, XFB, depth bounds and both viewport tests. The two failures are the known cross-arch blockers: `bc_decode` (every BC format unsupported, `textureCompressionBC = false`) and `swapchain_lifecycle` (`vkCreateSwapchainKHR` -1000072003 after `mapper load failed`). No `KBASE_IOCTL_MEM_EXEC_INIT` warning appears on this 5.15 kernel. The earlier Mali-G610 MC6 on Linux 5.10 (`559830af`, `0b151151`, beta.14) still has only PanPlay data, where DXVK rejects the adapter for missing `textureCompressionBC`. Mali-G710, Mali-G510 and Mali-G310 share the architecture but remain unseen.

## GPUs and devices tested

| Driver build | Anon ID(s) | Device | SoC | GPU | gpu_id -> Mesa model | Kernel | Android | App |
|---|---|---|---|---|---|---|---|---|
| beta.14 | `559830af` | 23054RA19C | MT6896 | Mali-G610 MC6 | `0xa8670000` -> G610 | 5.10 android12 | 15 | PanPlay 1.2.1 (D3D10) |
| beta.14 | `0b151151` | 23054RA19C | MT6896 | Mali-G610 MC6 | `0xa8670000` -> G610 | 5.10 android12 | 15 | PanPlay 1.2.1 (D3D11) |
| beta.15 | `854bef5b` | 23090RA98I | MT6886 | Mali-G610 MC4 | `0xa8670000` -> G610 | 5.15.180 android13 (4 KiB pages) | 16 | PanProbe 1.2.2 |
| beta.15 | `5b799ebb` | vivo V2284A | MT6896Z/CZA (`ro.hardware mt6895`) | Mali-G610 MC6 | `0xa8670000` -> G610 | 5.10.233 android12 (4 KiB pages) | 15 | PanPlay 1.2.2 (user D3D game) |

Both G610 variants report the same gpu_id. The core count only shows in the device name.

The tested device SoC reports MT6896 (`ro.hardware mt6895`). The hardware identifier `0xa8670000` decodes to architecture 10.8, product 7, revision r0p0, which matches `PAN_PROD_ID(10,8,7)` for "G610" in `src/panfrost/model/pan_model.c:93`.

Other Mali v10 models belonging to the same Valhall CSF architecture—specifically Mali-G710 (used in Google Pixel 7 / Tensor G2), Mali-G510, and Mali-G310—have not been observed in any uploaded records. None of the records report an "Unknown gpu_id" error (`src/panfrost/vulkan/panvk_physical_device.c:1213/1345`).

## Kernel / kbase interface seen
- **Kernel version:** Linux 5.10 android12 (vendor Android 12 GKI / kernel tree).
- **kbase interface:** Command Stream Frontend (CSF) backend. The kbase uAPI version is not logged in upload telemetry (likely an older uAPI, < 1.18, unverified).
- **Vendor API level:** `ro.board.first_api_level = 31` (Android 12), `ro.hardware.gralloc = common`.
- **Gralloc module:** gralloc0 is absent (`MESA: No gralloc hwmodule detected (video buffers won't be supported)`).
- **EXEC_INIT warning:** `MESA: warning: kbase: KBASE_IOCTL_MEM_EXEC_INIT failed: Operation not permitted (executable BO allocation will not work)` on initialization.
- **Queues & heaps (MC6, 5.10):** GPU queues and tiler heaps were not initialized due to early adapter rejection before `vkCreateDevice`.
- **G610 MC4 (5.15 android13, `854bef5b`):** CSF, uAPI not logged. `vkCreateDevice`, queue-group creation, tiler heaps and submission all work (15 tests render correctly). No `MEM_EXEC_INIT` warning. Vendor GLES r38p1, `ro.board.first_api_level = 33`, `ro.hardware.gralloc = common`. vulkaninfo: Vulkan 1.4.363, 185 extensions, `queueCount = 2`, `textureCompressionBC = false`, `depthBounds = true`, `shaderOutputViewportIndex = true`, `shaderDeviceClock = true`, 8.9 GB device heap. The Android mapper fails: `[P0A-V19-FULLPLANE] mapper load failed`, `complete metadata unavailable rc=-95; refusing guessed layout`, `u_gralloc_get_buffer_basic_info failed`.

## Per-test results

### PanPlay execution runs

| Driver build | Anon ID | Target executable | Exit code | Observed behavior / DXVK lines |
|---|---|---|---|---|
| beta.14 | `559830af` | D3D10 ARM64EC cube | 1 | DXVK rejects adapter (`textureCompressionBC` false), exits after 1 s |
| beta.14 | `0b151151` | D3D11 ARM64EC cube | 1 | DXVK rejects adapter (`textureCompressionBC` false), exits after 1 s |
| beta.15 | `5b799ebb` | User D3D game (V2284A) | 3 | `Skipping: Device does not support required feature 'textureCompressionBC'`, `DXVK: No adapters found`, uncaught `dxvk::DxvkError`, exit after 4 s |

All three PanPlay runs abort during adapter discovery before any GPU queues, command buffers, or device memory structures can be initialized.

### PanProbe test suite (17 tests)

One PanProbe run exists, from the Mali-G610 MC4 on beta.15 (`854bef5b`): 15/17.

| Test | G610 MC4 beta.15 (`854bef5b`) | Notes |
|---|---|---|
| `gpu_prerast_slice` | PASS | |
| `clip_cull` | PASS | |
| `multi_viewport` | PASS | |
| `fill_mode` | PASS | |
| `bc_decode` | FAIL | Every BC format `optimal=0x0 linear=0x0 ifp=-11` (cross-arch blocker 1) |
| `geometry` | PASS | |
| `tessellation` | PASS | |
| `xfb` | PASS | |
| `pipeline_stats` | PASS | |
| `vertex_stores` | PASS | |
| `gs_viewport_depth` | PASS | v10 has per-viewport depth runs |
| `vs_viewport_index` | PASS | |
| `depth_bounds` | PASS | |
| `large_draw` | PASS | |
| `vmr_secondary` | PASS | |
| `tess_cond_state` | PASS | |
| `swapchain_lifecycle` | FAIL | `vkCreateSwapchainKHR res=-1000072003` (cross-arch blocker 2) |

## Failures and log excerpts

### DXVK adapter rejection (`wine-run.log`)
```
info:  Game: dxcube-arm64ec.exe
info:  DXVK: v3.1.1+
info:  Build: aarch64 clang 23.1.2
info:  Found device: Mali-G610 MC6 (panvk 26.2.99)
info:    Skipping: Device does not support required feature 'textureCompressionBC'
warn:  DXVK: No adapters found. Please check your device filter settings
warn:  and Vulkan drivers. A Vulkan 1.3 capable setup is required.
err:   Failed to initialize DXVK.
CUBE: FAIL create hr=0x80004005
exit=1
```

For the D3D11 test run (`0b151151`), DXGI factory initialization fails immediately as DXVK finds no compatible adapter:
```
err:   D3D11CreateDevice: Failed to create a DXGI factory
```

### PanProbe failures on G610 MC4 (`854bef5b`)
```
FORMAT BC1_RGB_UNORM optimal=0x0 linear=0x0 ifp=-11 FAIL
FORMAT BC7_SRGB optimal=0x0 linear=0x0 ifp=-11 FAIL
FAIL vkCreateSwapchainKHR res=-1000072003 phase=create_swapchain
W MESA    : [P0A-V19-FULLPLANE] mapper load failed
W MESA    : [P0A-V19-FULLPLANE] complete metadata unavailable rc=-95; refusing guessed layout
E MESA    : u_gralloc_get_buffer_basic_info failed
```
Every BC format (BC1 to BC7, UNORM, SNORM and SRGB variants) reports `ifp=-11` (`VK_ERROR_FORMAT_NOT_SUPPORTED`).

### DXVK adapter rejection on V2284A (`5b799ebb`, beta.15)
```
info:  Found device: Mali-G610 MC6 (panvk 26.2.99)
info:    Skipping: Device does not support required feature 'textureCompressionBC'
warn:  DXVK: No adapters found. Please check your device filter settings
libc++abi: terminating due to uncaught exception of type dxvk::DxvkError
```
This log has no `KBASE_IOCTL_MEM_EXEC_INIT` warning. The run stopped before `vkCreateDevice`, so queue groups, heaps and shaders were not reached.

### MESA driver warnings (G610 MC6, 5.10)
Logcat during early driver initialization records kbase and gralloc warnings:
```
W MESA: kbase: KBASE_IOCTL_MEM_EXEC_INIT failed: Operation not permitted (executable BO allocation will not work)
W MESA: No gralloc hwmodule detected (video buffers won't be supported)
I MESA: Using fallback gralloc implementation
```

## Root causes
- **Cross-arch blocker 1 (BC texture compression):** `panvk_bc_emul_enabled()` at `src/panfrost/vulkan/panvk_physical_device.c:1815-1826` disables software BC emulation if kbase `TEXTURE_FEATURES` exposes native BC1 (bit 7). However, `has_texture_compression_bc()` at `src/panfrost/vulkan/panvk_vX_physical_device.c:294-302` requires all 10 BC format bits natively before advertising `textureCompressionBC` at `:338`. With emulation turned off, `get_image_plane_format_features()` at `src/panfrost/vulkan/panvk_physical_device.c:1837-1839` returns 0 for every BC format, including native ones. Texture features come from `GET_GPUPROPS` (`src/panfrost/lib/kmod/kbase_kmod.c:372` -> `src/panfrost/lib/pan_props.c:84`), where BC1 is bit 7 and the full BC set is mask `0x1ff80` (`src/panfrost/genxml/common.xml:76`). Fixing this requires keeping `src/panfrost/vulkan/panvk_image.c:674` consistent. This is marked as an inference because no raw `TEXTURE_FEATURES` dump was captured in uploaded records.
- **Cross-arch blocker 2 (Gralloc mapper), now confirmed on v10 by `854bef5b` (vendor API 33):** the mapper loader added by `patches/android/013-vendor-mapper-metadata.patch` needs an AIMapper stable-C v5 mapper (vendor API >= 34); this device has vendor API 31. In `src/util/u_gralloc/u_gralloc_fallback.c:83,95,102`, loading `mapper.mediatek.so` would then fail, triggering `-ENOTSUP` (`u_gralloc_fallback.c:125-130,425-431`) and `VK_ERROR_INVALID_EXTERNAL_HANDLE` (`src/vulkan/runtime/vk_android.c:150-152`). Calling paths include swapchain creation (`src/panfrost/vulkan/panvk_image.c:803` -> `src/panfrost/vulkan/panvk_android.c:172/125`) and AHB dedicated memory import (`src/panfrost/vulkan/panvk_device_memory.c:68` -> `src/panfrost/vulkan/panvk_android.c:318/236` -> `src/vulkan/runtime/vk_android.c:687` -> `:152`). In GitHub issue #5, a Mali-G610 user in a third-party Winlator fork hit `err:msvcrt:_wassert (L"!status && \"vkCreateSwapchainKHR\"")`. PanPlay is unaffected because it presents through X11 software WSI rather than Android AHardwareBuffer / Gralloc.
- **Cross-arch issue 3 (EXEC_INIT EPERM):** `src/panfrost/lib/kmod/kbase_kmod.c:1391-1410` executes JIT_INIT before EXEC_INIT; older CSF kbase returns `-EPERM` when initializing the executable VA zone after JIT. Setting `PAN_KMOD_BO_FLAG_EXECUTABLE` maps to `BASE_MEM_PROT_GPU_EX` at `src/panfrost/lib/kmod/kbase_kmod.c:1624`. An executable allocation failure would surface as `VK_ERROR_OUT_OF_DEVICE_MEMORY` at `src/panfrost/vulkan/panvk_vX_shader.c:3133`. It was seen only on the 5.10 G610 MC6. The 5.15 G610 MC4 (`854bef5b`) logs no EXEC_INIT warning and runs shaders in 15 tests. The 5.10 impact is still unknown because no shader has run there. Unreleased patch `patches/jm-v9/004-init-exec-va-before-jit.patch` moves EXEC_INIT before JIT_INIT.
- **Candidate fixes (not yet released):** `patches/csf-v11/108-emulate-bc-unless-every-bc-format-is-native.patch` targets `bc_decode` and DXVK adapter selection. `patches/android/014-vendor-neutral-gralloc-mapper-and-hidl-mapper4.patch` targets `swapchain_lifecycle` on vendor API < 34. Both need a G610 rerun.

## What the driver lacks on this arch
- Software BC emulation fallback when kbase exposes partial native BC texture features.
- kbase CSF interface gaps. **Patched, unreleased (`patches/csf-v11/110` + `patches/jm-v9/005`); no CSF hardware run yet.** Layouts were checked against public headers and `mali_kbase_core_linux.c`: Pixel google-modules/gpu raviole 1.10, pantah 1.14 and caimito 1.24, and rockchip-linux develop-6.1 1.30 and develop-5.10 1.31.
  - **Queue-group create:**
    - Layouts: 32-byte `_1_6` (nr 42, every CSF uAPI), 40-byte `_1_18` (nr 58, 1.7+; `csi_handlers` 1.12+) and 112-byte (nr 58, 1.19+; `cs_fault_report_enable` 1.25+).
    - Order: 1.25+ tries 112 then 32 then 40. 1.19-1.24 tries 32, 112, 40. 1.7-1.18 tries 32, 40. Below 1.7 only 32.
    - Only EINVAL/ENOTTY moves on to the next layout. Every public kernel keeps `_1_6`/`_1_18` as compat cases that build the same request.
  - **Tiler heap init:** the 16-byte `_1_13` layout comes first. On 1.14+ the 24-byte layout (`buf_desc_va = 0`, same as the kernel 1.13 compat handler) is the EINVAL/ENOTTY fallback.
  - **16 KiB pages:** kbase counts `va_pages`, alias lengths, special mmap handles (`3 << PAGE_SHIFT`, ...) and the 3 CS USER_IO pages in kernel PAGE_SIZE units (`LOCAL_PAGE_SHIFT = PAGE_SHIFT`). These, the USER_IO input/output offsets and the EXEC_VA/JIT zone sizes now use `sysconf(_SC_PAGESIZE)`. Without this, the special-handle mmap offsets are misaligned on 16 KiB kernels (EINVAL).
  - **Behaviour on known devices:** G615 (1.21) still sends the 32-byte and 16-byte requests first, and G720 (~1.30) the 112-byte one with fault reporting. On 4 KiB kernels all sizes are unchanged.
  - **Tester check (G610 PanProbe logcat):**
    - `kbase: CSF driver, uAPI version 1.x, page_size=4096`
    - `kbase: queue_group_create layout=32 bytes`
    - `kbase: tiler_heap_init layout=16 bytes`
    - `kbase: mem_alloc via ALLOC_EX` (`ALLOC` below uAPI 1.9)
    - no `layout=... failed` warnings
- Timestamps: `GET_CPU_GPU_TIMEINFO` may be absent on older CSF kbase; timestamp queries would then read 0.
- CS work-register count: an implausible firmware value falls back to 96 on v10/v11 (a Pixel 7 G710 reported a bad value before beta.12); unverified on a real v10 kernel.
- Android gralloc mapper metadata support for vendor API < 34 / HIDL mapper4 for Android-surface swapchains.

## Fix plan

| Rank | Item | Effort | Unblocks |
|---|---|---|---|
| 1 | BC emulation decision (emulate all BC formats unless full 0x1ff80 mask native; candidate patch 108) | 4–8 h | `bc_decode` and DXVK adapter selection on v10 |
| 2 | Gralloc mapper: vendor-neutral discovery / HIDL mapper4 backend (candidate patch android/014) | 24–48 h (quick: 4–8 h) | `swapchain_lifecycle` on G610 MC4 (17/17 with rank 1) |
| 3 | PanPlay cube run on the G610 MC4 after rank 1 | Data collection | First DXVK device creation and present on v10 |
| 4 | PanProbe run on the 5.10 G610 MC6 | Data collection | Shows whether 5.10 needs old-CSF layouts |
| 5 | EXEC_INIT ordering fix (call before JIT_INIT; candidate patch jm-v9/004) | 6–12 h | Eliminates kbase EPERM warning on 5.10 |
| 6 | Old-CSF layout support (40-byte 1.18 queue group, 24-byte heap init, 16 KiB pages): **patched, unreleased (csf-v11/110)** | Done; tester logcat | Only if rank 4 shows failures |

## Open questions / data needed from testers
- **PanProbe zip on a 5.10 G610 MC6:** The 5.15 G610 MC4 has one (`854bef5b`). Neither 5.10 MC6 (23054RA19C, V2284A) has one. No upload records the kbase uAPI version. A beta.17 run would log it (110) together with `TEXTURE_FEATURES` and the BC decision (118).
- **PanPlay on the G610 MC4:** No PanPlay data yet. DXVK is expected to reject the adapter until rank 1 lands.
- **TEXTURE_FEATURES on G610:** Need a raw dump of kbase `TEXTURE_FEATURES` (`gpuinfo`) to verify exact native texture feature bits.
- **Other v10 hardware:** Need `gpu_id` values and variant strings from Mali-G710 (e.g. Google Pixel 7, Tensor G2), Mali-G510, and Mali-G310 hardware owners.
- **Queue-group layout:** Find out whether 5.10 / Android 12 vendor kernels need the 40-byte 1.18 CSF queue-group layout. With 110, the `kbase: queue_group_create layout=` logcat line answers this directly.

## Links
- [Universal Mali status](../README.md)
- [Tested devices](../DEVICES.md)
- [Universal Mali Plan](../../plans/PANVK_UNIVERSAL_MALI_PLAN.md)
