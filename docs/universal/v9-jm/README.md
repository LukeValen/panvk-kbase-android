# Mali v9 (Valhall Job Manager) status

All file:line references throughout this document are relative to the Mesa root of the beta.16 tree (Mesa 5a07217f + csf-v11 series up to 107 + jm-v9 001-003), written as `src/panfrost/...:NNN`, unless marked "beta.17 tree" (the unreleased beta.17 candidate: beta.16 + csf-v11 108-118 + android/014 + wsi/017 + jm-v9 004-005).

## Summary
Update 2026-10-06: no new v9 tester uploads. The developer ran the unreleased beta.17 candidate on the G57 tablet. PanProbe reports 3/17, but `vmr_secondary` only skipped, so 2/17 are real passes (`vertex_stores`, and `swapchain_lifecycle` through the new `cpu-linear-probe` path). Patch 118 logged the first G57 kbase facts: JM uAPI 11.38 and `TEXTURE_FEATURES[0] = 0xf7fe03fe` (BC1-BC3 native, BC4-BC7 not). With 108, BC is exposed, but BC blits fail on JM. EXEC_INIT no longer fails (jm-v9/004). The G77 is still blocked: beta.17 has no `PAN_PROD_ID(9, 0, 0)` row.

The Mali v9 (Valhall Job Manager) backend has been experimental since beta.14. PanProbe scores 1/17 across beta.14, beta.15, and beta.16 (only `vertex_stores` passes). PanPlay cannot start any D3D game because the driver reports Vulkan 1.1 and lacks DXVK-required features (`geometryShader`, `multiViewport`, `fillModeNonSolid`, `multiDrawIndirect`, `textureCompressionBC`). Tested hardware is a Mali-G57 MC2 tablet (Lenovo TB336FU) and a Mali-G77 MC9 phone (POCO 21061110AG, MT6891). On the G77, beta.15 scores 0/17. PanVK opens the kbase node, but gpu_id `0x90800011` (`PAN_PROD_ID(9, 0, 0)`) has no model-table row, so no physical device is created. Mali-G68 and Mali-G78 remain untested.

## GPUs and devices tested

| Driver build | Anon ID(s) | Device | SoC | GPU | gpu_id -> Mesa model | Kernel | Android | App |
|---|---|---|---|---|---|---|---|---|
| beta.13 (no v9 support) | `5ec9f0c1`, `dae37485`, `80b7a0c9`, `c4d22728`, `70ce9223` | TB336FU | MT8755 | Mali-G57 MC2 | `0x90930010` -> G57 | 5.15 android13 | 16 | PanProbe 1.2.0/1.2.1 |
| beta.13 (no v9 support) | `079357e1`, `94f4d2bb` | TB336FU | MT8755 | Mali-G57 MC2 | `0x90930010` -> G57 | 5.15 android13 | 16 | PanPlay 1.2.0 |
| beta.14 | `fad0cb63`, `6ff8855c`, `e0944b04` | TB336FU | MT8755 | Mali-G57 MC2 | `0x90930010` -> G57 | 5.15 android13 | 16 | PanProbe 1.2.1 |
| beta.14 | `ef4f82a2`, `7b576aa7`, `d3de3763` | TB336FU | MT8755 | Mali-G57 MC2 | `0x90930010` -> G57 | 5.15 android13 | 16 | PanPlay 1.2.1 |
| beta.15 | `49abcff8` | TB336FU | MT8755 | Mali-G57 MC2 | `0x90930010` -> G57 | 5.15 android13 | 16 | PanProbe 1.2.2 |
| Excluded | `f907b333` | TB336FU | MT8755 | Mali-G57 MC2 | `0x90930010` -> G57 | 5.15 android13 | 16 | PanProbe 1.2.1 (non-release .so) |
| beta.15 | `c853b7b6`, `40359b15` (same run) | 21061110AG (POCO) | MT6891 | Mali-G77 MC9 | `0x90800011` -> none (`PAN_PROD_ID(9, 0, 0)` missing) | 4.14 custom | 13 | PanProbe 1.2.2 (direct tester zip) |

Mali-G68 and Mali-G78 are the same Valhall Job Manager architecture (v9) but have no test submissions yet.

### Mali-G77 MC9 (POCO 21061110AG, MT6891)
- **Records:** `c853b7b6` (sent twice as byte-identical zips, and also uploaded to D1 as id 40 with the same SHA-256, so it is one record) and `40359b15` (same run `20261005-153649`, re-exported with a different logcat window). Bundled beta.15, same `.so` hash as the other beta.15 records. `driverInfo` is null because no PanVK device existed.
- **Platform:** Android 13 (MIUI 14 base), product first API level 30, kernel 4.14 (custom, non-GKI), vendor DDK GLES r32p1, vendor Vulkan 1.1.177. `ro.hardware.gralloc` is not set. kbase uAPI version is not logged.
- **Result:** PanProbe 0/17. 15 tests end with `FAIL no Mali`. `bc_decode` ends with `CRASH signal=11` (PanProbe runner on the no-device path, as with the beta.13 SIGBUS on `80b7a0c9`). `swapchain_lifecycle` ends with `FAIL vkEnumeratePhysicalDevices res=-3 phase=init`. `device_info.json` lists no devices.
- **What did work:** The kbase node opened and the version handshake passed on 4.14. Each test logs the usual `KBASE_IOCTL_MEM_EXEC_INIT failed: Operation not permitted` warning, which is only printed after the kbase device is created (`src/panfrost/lib/kmod/kbase_kmod.c:1408`).
- **Why it stops:** `0x90800011` decodes to arch 9.0, arch_rev 8, product_major 0, r0p1. `pan_prod_id()` (`src/panfrost/model/pan_model.h:125-126`) gives `PAN_PROD_ID(9, 0, 0)`. The table has only `(9, 0, 1)`, `(9, 0, 3)` (G57) and `(9, 2, 4)` (G68) (`src/panfrost/model/pan_model.c:87-91`). The "G77" string in the G57 rows is the counter set name. `pan_get_model()` returns NULL (`src/panfrost/model/pan_model.c:148-158`). `panvk_physical_device_init_kbase()` then returns `VK_ERROR_INCOMPATIBLE_DRIVER` (`src/panfrost/vulkan/panvk_physical_device.c:1338-1345`), and `src/panfrost/vulkan/panvk_instance.c:223-228` skips the node. The `Unknown gpu_id` text is suppressed in release builds (`src/vulkan/runtime/vk_log.c:114-119`).
- **Expected after a model row is added:** The same v9 limits as the G57 (Vulkan 1.1, no compute pre-raster, BC, EXEC_INIT EPERM). The gralloc mapper blocker is also likely, because vendor API is below 34. The 4.14 JM kbase path beyond device open (JIT, job submit) is untested.

## Kernel / kbase interface seen
- **Kernel:** Linux 5.15 android13 (GKI 5.15) on the G57; Linux 4.14 custom (non-GKI) on the G77.
- **kbase uAPI:** JM uAPI 11.38 on the G57, measured by the beta.17 candidate (`kbase: JM driver, uAPI version 11.38, page_size=4096`; earlier worklogs said 11.0). Tester uploads do not log it yet. G77: JM, version not logged, handshake accepted by beta.15.
- **TEXTURE_FEATURES (G57, beta.17 candidate):** `texture_features 0xf7fe03fe 0xc3fff7ff 0xbfe1ff9f 0x10c6`. In word 0, bits 7-9 (BC1-BC3) are set and bits 10-16 (BC4-BC7) are clear, so native BC is partial. The BC decision log is `BC emulation on (native compressed mask 0xf7fe03fe)`.
- **Vendor DDK:** GLES r38p1 on the G57, r32p1 on the G77 (`GL_VERSION` from vendor driver).
- **Vendor API level:** `ro.board.first_api_level = 33` (Android 13), `ro.hardware.gralloc = common`.
- **Queues:** `queueCount = 1` (single Valhall JM queue).
- **Timestamps:** `timestampValidBits = 0` (no JM GPU timeinfo plumbing implemented).
- **EXEC_INIT warning:** `MESA: warning: kbase: KBASE_IOCTL_MEM_EXEC_INIT failed: Operation not permitted (executable BO allocation will not work)` on every run up to beta.16 (issue 3: JIT_INIT precedes EXEC_INIT in `src/panfrost/lib/kmod/kbase_kmod.c:1391-1410`). Gone on the G57 with the beta.17 candidate (jm-v9/004: EXEC_INIT before JIT_INIT).

## Per-test results

### PanProbe test suite (17 tests)

The last column is the unreleased beta.17 candidate, run by the developer on the TB336FU on 2026-10-06 with PanProbe built from the current tree. It is not a tester upload.

| Test | beta.14 (`fad0cb63`, `6ff8855c`, `e0944b04`) | beta.15 (`49abcff8`) | beta.17 candidate (dev smoke) | Result + reason |
|---|---|---|---|---|
| `gpu_prerast_slice` | FAIL | FAIL | FAIL | Passes until replay: JM atom 35 (`core_req=0x16`) and atom 36 (`core_req=0x1`) fail with event `0x58` (`DATA_INVALID_FAULT`), `Wait r=-4` (DEVICE_LOST). Unchanged in beta.17 |
| `clip_cull` | FAIL | FAIL | FAIL | `CreateDevice r=-8` (`VK_ERROR_FEATURE_NOT_PRESENT`: missing `shaderClipDistance`, `shaderCullDistance`) |
| `multi_viewport` | FAIL | FAIL | FAIL | `CreateDevice r=-8` (`VK_ERROR_FEATURE_NOT_PRESENT`: missing `multiViewport`) |
| `fill_mode` | FAIL | FAIL | FAIL | `CreateDevice r=-8` (`VK_ERROR_FEATURE_NOT_PRESENT`: missing `fillModeNonSolid`) |
| `bc_decode` | FAIL | FAIL | FAIL | beta.14/15: all BC formats unsupported (`ifp=-11`, blocker 1). beta.17: `textureCompressionBC=1`, every format query passes, and raw and copy decode pass for all 16 formats, but blit fails for all 16 (`raw=PASS copy=PASS blit=FAIL`, `BC_DEVICE_FAILS=16`) |
| `geometry` | FAIL | FAIL | FAIL | `CreateDevice r=-8` (`VK_ERROR_FEATURE_NOT_PRESENT`: missing `geometryShader`) |
| `tessellation` | FAIL | FAIL | FAIL | `FAIL tessellationShader not exposed` |
| `xfb` | FAIL | FAIL | FAIL | `FAIL VK_EXT_transform_feedback not exposed` |
| `pipeline_stats` | FAIL | FAIL | FAIL | `CreateDevice r=-8` (`VK_ERROR_FEATURE_NOT_PRESENT`: missing `pipelineStatisticsQuery`) |
| `vertex_stores` | PASS | PASS | PASS | Passes all verification cases |
| `gs_viewport_depth` | FAIL | FAIL | FAIL | `FAIL required feature missing` (`geometryShader` / viewport depth clamp features not reported) |
| `vs_viewport_index` | FAIL | FAIL | FAIL | `FAIL required feature missing` (`shaderOutputViewportIndex` not reported) |
| `depth_bounds` | FAIL | FAIL | FAIL | `FAIL depthBounds not reported` |
| `large_draw` | FAIL | FAIL | FAIL | `FAIL VK_EXT_transform_feedback not exposed` |
| `vmr_secondary` | FAIL | FAIL | SKIP (reported as PASS) | beta.14/15: `cnt[slot 1 sample 1] = 0, want 256` (samples 1-3 never counted). beta.17 PanProbe skips the test because v9 reports `vmr=0`: `SKIP no 4x sample shading or VMR without attachments`, then `RESULT PASS`. Not a fix |
| `tess_cond_state` | FAIL | FAIL | FAIL | `CreateDevice r=-7` (`VK_ERROR_EXTENSION_NOT_PRESENT`: missing `VK_EXT_conditional_rendering`) |
| `swapchain_lifecycle` | FAIL | FAIL | PASS | beta.14/15: `FAIL vkCreateSwapchainKHR res=-1000072003` (blocker 2). beta.17: the vendor mapper still fails to load, but the new `cpu-linear-probe` fallback (android/014 + wsi/017) gives `match=9/9 -> LINEAR`; 90.9 FPS |

beta.17 candidate total: 3/17 reported, 2/17 real (`vertex_stores`, `swapchain_lifecycle`).

### PanPlay execution runs

| Driver build | Anon ID | Target executable | Exit code | Observed behavior / DXVK lines |
|---|---|---|---|---|
| beta.13 | `079357e1` | D3D8 x86 cube | 5 | Driver initialization fails (beta.13 has no v9 support) |
| beta.13 | `94f4d2bb` | D3D8 x86 cube | 137 | `Failed to enumerate physical devices, res -3` |
| beta.14 | `ef4f82a2` | D3D8 ARM64EC cube | 5 | Wine exits with code 5 before DXVK logs an adapter line; not analysed further |
| beta.14 | `7b576aa7` | D3D9 ARM64EC cube | 3 | `Skipping: Device does not support Vulkan 1.3`, `DXVK: No adapters found`, uncaught `dxvk::DxvkError` |
| beta.14 | `d3de3763` | D3D9 ARM64EC cube | 3 | `Skipping: Device does not support Vulkan 1.3`, exit 3 |

## Failures and log excerpts

### JM atom DATA_INVALID_FAULT (`gpu_prerast_slice`)
```
CASE idvs_before RGBA=255 0 0 255 PASS
...
CASE gpu_written_indirect RGBA=255 0 0 255 PASS
MESA: error: kbase: JM atom 35 failed: event=0x58 core_req=0x16
MESA: error: kbase: JM atom 36 failed: event=0x58 core_req=0x1
FAIL Wait r=-4 line=588
```
Atom 35 (vertex/tiler, `core_req=0x16`) encounters event `0x58` (`DATA_INVALID_FAULT`) upon command buffer replay. Atom 36 (fragment, `core_req=0x1`) fails due to dependency failure.

### Feature and extension gate rejections
```
ICD device=Mali-G57 MC2 geometryShader=0 fillModeNonSolid=0 multiViewport=0 shaderClipDistance=0 shaderCullDistance=0 maxViewports=1 maxClip=0 maxCull=0 maxCombined=0
FAIL CreateDevice r=-8 line=160
```
```
FAIL CreateDevice r=-7 line=160
```
Tests querying unadvertised features fail device creation with `r=-8` (`VK_ERROR_FEATURE_NOT_PRESENT`). Tests requiring unsupported extensions (`VK_EXT_conditional_rendering` in `tess_cond_state`) fail with `r=-7` (`VK_ERROR_EXTENSION_NOT_PRESENT`).

### VMR secondary sample count failure (`vmr_secondary`)
```
FEATURE noAttachmentSamples=0xd sampleRateShading=1
  cnt[slot 1 sample 1] = 0, want 256
  cnt[slot 1 sample 2] = 0, want 256
  cnt[slot 1 sample 3] = 0, want 256
FAIL case prim_1x_4x_1x bad=3
...
PASS case sec_1x bad=0
...
RESULT FAIL
```

### Swapchain creation failure (`swapchain_lifecycle`)
```
FAIL vkCreateSwapchainKHR res=-1000072003 phase=create_swapchain
```
Logcat excerpt:
```
MESA: [P0A-V19-FULLPLANE] mapper load failed
MESA: [P0A-V19-FULLPLANE] complete metadata unavailable rc=-95; refusing guessed layout
```

### DXVK adapter rejection in PanPlay D3D9 (`wine-run.log`)
```
info:  Found device: Mali-G57 MC2 ( 26.2.99)
info:    Skipping: Device does not support Vulkan 1.3
warn:  DXVK: No adapters found. Please check your device filter settings
warn:  and Vulkan drivers. A Vulkan 1.3 capable setup is required.
libc++abi: terminating due to uncaught exception of type dxvk::DxvkError
exit=3
```

## Root causes
- **API version 1.1 & DXVK requirements:** `get_api_version()` returns 1.1 on `PAN_ARCH == 9` at `src/panfrost/vulkan/panvk_vX_physical_device.c:817` (patch `jm-v9/003`). Bundled DXVK (`components-build dxvk-src src/dxvk/dxvk_device_info.cpp:828-840`) requires Vulkan 1.3 and enforces `geometryShader`, `multiViewport`, `fillModeNonSolid`, and `multiDrawIndirect` even for D3D9. Vulkan 1.2/1.3 features and properties are hidden, producing empty driverName `( 26.2.99)`.
- **Replay fault (hypothesis):** In `tests/dxvk/vulkan/gpu_prerast_slice.c:488`, command buffers are submitted a second time. JM queue restore (`src/panfrost/vulkan/jm/panvk_vX_gpu_queue.c:88`) restores 16-byte job headers and tiler descriptors, but omits the GPU-written `MALLOC_VERTEX_JOB` draw payload where `vertex_array` was packed with `packet=true` (`src/panfrost/vulkan/jm/panvk_vX_cmd_draw.c:2309`, genxml `src/panfrost/genxml/v9.xml:1567`). A stale payload on the second submit would explain `DATA_INVALID_FAULT` (event `0x58`) on the vertex/tiler atom. Not yet confirmed by a descriptor dump.
- **VMR sample count:** Attachment-less rendering leaves `fb.nr_samples = 1` (`src/panfrost/vulkan/panvk_vX_cmd_draw.c:631/732`). v9 allocates the framebuffer without updating sample counts (`src/panfrost/vulkan/jm/panvk_vX_cmd_draw.c:2454`; legacy JM sets it at `:1299`) and never sets `flags_0.evaluate_per_sample` (`src/panfrost/vulkan/jm/panvk_vX_cmd_draw.c:2306`), leaving samples 1..3 uncounted.
- **Compute pre-raster pipeline is CSF-only:** Pre-raster shader lowering is restricted to CSF (`src/panfrost/vulkan/meson.build:102`, `src/panfrost/vulkan/panvk_shader.h:22`). Without it, JM has no lowering for `geometryShader`, `multiViewport`, `fillModeNonSolid`, `shaderClipDistance`, `shaderCullDistance`, and `vertexPipelineStoresAndAtomics`.
- **Tessellation silently dropped:** The JM draw dispatch contains placeholder handling that silently discards tessellation draws (`src/panfrost/vulkan/jm/panvk_vX_cmd_draw.c:2427-2429`), and `tessellationShader` is not advertised.
- **BC texture compression disabled (blocker 1):** `panvk_bc_emul_enabled()` at `src/panfrost/vulkan/panvk_physical_device.c:1815-1826` disables software BC emulation if kbase `TEXTURE_FEATURES` reports native BC1. However, `has_texture_compression_bc()` at `src/panfrost/vulkan/panvk_vX_physical_device.c:294-302` requires all 10 BC formats natively. Disabling emulation causes `get_image_plane_format_features()` (`src/panfrost/vulkan/panvk_physical_device.c:1837-1839`) to return 0 for all BC formats.
- **Android gralloc mapper mismatch (blocker 2):** MediaTek stable-C mapper in `src/util/u_gralloc/u_gralloc_fallback.c:83,95,102` fails on Android 13 (`ro.board.first_api_level = 33` < 34), returning `-ENOTSUP` (`u_gralloc_fallback.c:125-130,425-431`) and triggering `VK_ERROR_INVALID_EXTERNAL_HANDLE` (`src/vulkan/runtime/vk_android.c:150-152`).
- **G77 gpu_id not in model table (cross-arch blocker 4):** `PAN_PROD_ID(9, 0, 0)` has no row in `src/panfrost/model/pan_model.c:87-91`, so the G77 kbase device is skipped as `VK_ERROR_INCOMPATIBLE_DRIVER` (`src/panfrost/vulkan/panvk_physical_device.c:1345`, `src/panfrost/vulkan/panvk_instance.c:227`).
- **EXEC_INIT EPERM (issue 3):** `src/panfrost/lib/kmod/kbase_kmod.c:1391-1396,1404-1410` calls JIT_INIT before EXEC_INIT; older JM kbase returns `-EPERM` for EXEC_INIT.
- **Indirect draws and firstInstance gaps:** JM indirect dispatch helpers exist (`src/panfrost/vulkan/jm/panvk_vX_cmd_draw.c:2575,2603`), but lack `VK_KHR_draw_indirect_count`, multi-draw gating, and per-instance attribute offsets for GPU `firstInstance` (`src/panfrost/vulkan/jm/panvk_vX_cmd_draw.c:1989`).

## What the driver lacks on this arch
- Vulkan 1.2, 1.3, and 1.4 API reporting and feature structs (pinned to 1.1).
- Compute pre-raster lowering on JM for geometry shading, multi-viewport, non-solid fill mode, and clip/cull distances.
- Tessellation shader execution (draws currently dropped).
- Transform feedback (`VK_EXT_transform_feedback`).
- Replay support without payload corruption on `MALLOC_VERTEX_JOB`.
- Multisample / VMR attachment-less sample evaluation.
- BC texture decompression fallback when only partial native BC is exposed.
- Android gralloc mapper support for vendor API < 34 / HIDL mapper4.
- Indirect count draws (`VK_KHR_draw_indirect_count`), multi-draw indirect, and GPU `firstInstance` attribute rebasing.
- Hardware timestamp plumbing (`timestampValidBits = 0`).
- Missing extensions vs G615 (170 vs 188): `VK_EXT_transform_feedback`, `VK_EXT_robustness2`, `VK_EXT_conditional_rendering`, `VK_EXT_multi_draw`, `VK_EXT_primitives_generated_query`, `VK_EXT_nested_command_buffer`, `VK_EXT_memory_priority`, `VK_EXT_pageable_device_local_memory`, `VK_EXT_sampler_filter_minmax`, `VK_KHR_draw_indirect_count`, `VK_KHR_cooperative_matrix`, `VK_KHR_copy_memory_indirect` and others.

## Fix plan

| Rank | Item | Effort | Unblocks |
|---|---|---|---|
| 0 | Add G77 model row `PAN_PROD_ID(9, 0, 0)` (tile-buffer sizes/rates to verify). **Not in beta.17**; the unknown-gpu_id log is done (118, `mesa_loge` at `src/panfrost/vulkan/panvk_physical_device.c:1356`, beta.17 tree) | 1–2 h + tester rerun | G77 physical device creation; G77 reaches the same tests as G57 |
| 1 | Replay fault: snapshot/restore `MALLOC_VERTEX_JOB` payload | 4–8 h diagnosis + 1–2 days | `gpu_prerast_slice` PASS |
| 2 | VMR sample count: update `fb.nr_samples` and `evaluate_per_sample` | 1–2 days | `vmr_secondary` PASS |
| 3 | BC emulation decision: **done in 108 (unreleased)**, BC exposed on G57. Remaining: BC blit fails for all 16 formats on JM (cause not traced) | not estimated | `bc_decode` |
| 4 | Gralloc mapper: **done for the G57 in android/014 + wsi/017 (unreleased)** via `cpu-linear-probe` | Done | `swapchain_lifecycle` passes on the G57 (dev smoke) |
| 5 | Audit and report Vulkan 1.3 core features on JM | 1–3 days | Exposes Vulkan 1.3 to DXVK |
| 6 | Port compute pre-raster lowering to JM job chains | 15–30 working days | GS, multiViewport, fillModeNonSolid, clip/cull, VPSA |
| 7 | Tessellation shader pipeline support on JM | 5–15 days | `tessellation`, `tess_cond_state` |
| 8 | Transform feedback support (`VK_EXT_transform_feedback`) | 3–7 days | `xfb`, `large_draw` |
| 9 | Draw indirect count, multiDraw, and GPU firstInstance offset | 2–5 days | Indirect draw conformance |
| 10 | Timestamp query plumbing for JM | not estimated | `timestampValidBits > 0` |
| 11 | EXEC_INIT ordering fix (call before JIT_INIT): **done in jm-v9/004 + 005 (unreleased)** | Done | EPERM warning gone on the G57 |

DXVK requires Vulkan 1.3, `geometryShader`, `multiViewport`, `fillModeNonSolid`, `multiDrawIndirect`, and `textureCompressionBC` before it will accept the physical device adapter. Consequently, PanPlay games remain hard-blocked until rank 6 lands.

## Open questions / data needed from testers
- **Other v9 GPUs:** Need `gpu_id` values and kbase uAPI versions for Mali-G68 and Mali-G78 devices. G77 gpu_id is now known (`0x90800011`). Its kbase uAPI version is still unknown.
- **G77 rerun:** After the model row lands, a PanProbe rerun on the POCO 21061110AG is needed to see how the 4.14 JM kernel handles JIT, job submit and the mapper path.
- **TEXTURE_FEATURES on G57:** Answered by the beta.17 candidate: `0xf7fe03fe`, BC1-BC3 native only.
- **BC blit on JM:** Why do BC blits fail on the G57 while raw and copy decode pass? Not traced yet.
- **`vmr_secondary` on v9:** The new PanProbe skips it (`vmr=0`) and reports PASS. The runner should report a skip as a skip.
- **Replay hypothesis validation:** Need a descriptor and job memory dump before and after replay execution in `gpu_prerast_slice` to confirm `MALLOC_VERTEX_JOB` payload corruption.
- **DXVK D3D9 requirements:** Verify whether D3D9 translation on this bundled DXVK version strictly requires `geometryShader` when Vulkan 1.3 is advertised, or if feature gating can be relaxed.

## Links
- [Universal Mali status](../README.md)
- [Tested devices](../DEVICES.md)
- [v9 JM Test Driver Worklog](../../../worklogs/driver-remaining/101-v9-jm-test-driver.md)
- [G615 DXVK Progress Worklog](../../../worklogs/g615-dxvk/PROGRESS.md)
