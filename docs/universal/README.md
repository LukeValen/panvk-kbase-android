# Universal Mali status (tester data)

This document indexes test results, driver execution stages, cross-architecture blockers, and hardware coverage for the PanVK universal Mali investigation. Snapshot: 2026-10-06. The newest release is beta.16 (Mesa 5a07217f with csf-v11 series up to 107, and the experimental v9 JM backend active since beta.14). A beta.17 candidate is built but unreleased, and no tester has run it. The data is 88 records from 19 devices: 86 D1 upload rows (ids 2 to 90; ids 1, 16, 31 and 56 absent) plus 2 PanProbe archives sent directly by testers (see Methodology). The 36 rows added on 2026-10-05/06 (D1 ids 54 to 90) bring the first v13 DXVK runs (Immortalis-G925 cubes and a Unity game), five more G720 runs, a full set of D3D8-D3D11 cubes on a stock G615, the first beta.16 tester upload, and four new devices (vivo V2284A G610 MC6, Infinix GT 30, LAVA LXX525, Redmi Note 14 Pro 5G). Every tested device is listed in [DEVICES.md](DEVICES.md). Architecture plans, worklogs, and roadmap progression are tracked in [PANVK_UNIVERSAL_MALI_PLAN.md](../plans/PANVK_UNIVERSAL_MALI_PLAN.md).

Source file:line references are relative to the Mesa root of the beta.16 tree (Mesa 5a07217f + csf-v11 series up to 107 + jm-v9 001-003), written as `src/panfrost/...:NNN`. References marked "beta.17 tree" are relative to the beta.17 candidate (beta.16 + csf-v11 108-118 + android/014 + wsi/017 + jm-v9 004-005; 124 patches on base 169d6a0).

## beta.17 patch per blocker

None of these patches is released or committed, and none has run on G615 hardware. The developer ran the candidate only on the G57 tablet (PanProbe 3/17 reported, 2/17 real).

| Blocker | Archs hit | beta.17 patch | Status |
|---|---|---|---|
| 1. BC emulation off with partial native BC | v9, v10 | csf-v11/108 | G57: BC exposed, format queries pass, but blits fail on JM. v10 untested |
| 2. Gralloc mapper on stock ROMs | v9, v10, v11 stock | android/014 + wsi/017 (`cpu-linear-probe`) | G57: `swapchain_lifecycle` passes (9/9 LINEAR). Stock G615/G610 untested |
| 3. EXEC_INIT ordering | v9, v10 (5.10) | jm-v9/004 + 005 | G57: EPERM warning gone. v10 untested |
| 4. G77 gpu_id missing | v9 | None. 118 only logs `Unknown gpu_id` | Open |
| 5. `MEM_ALLOC_EX` ENOTTY | v14 | csf-v11/110 | Untested on v14 |
| 6. v12+ viewport depth, `depthBounds`, `shaderOutputViewportIndex` | v12, v13 (v14 expected) | csf-v11/109 (+116 for depth bounds FPK) | Untested on v12/v13 |
| 7. FL11_0 on v13+ (VPSA is drirc opt-in) | v13 (v14 expected) | None | Open question |
| Upload diagnostics (uAPI, `TEXTURE_FEATURES`, BC decision) | all | csf-v11/110 + 118 + app changes | Verified on the G57 only |

## Arch matrix

| Arch | GPUs seen | gpu_id(s) | Kernel / kbase interface | Records (devices) | Result (best driver build) | Top blockers | Doc link |
|---|---|---|---|---|---|---|---|
| v9 (Valhall JM) | Mali-G57 MC2, Mali-G77 MC9 | 0x90930010 (G57), 0x90800011 (G77, not in model table) | 5.15 android13 / JM uAPI 11.38 (G57, measured by beta.17); 4.14 custom / JM, uAPI not logged (G77) | 17 (2 devices) | G57 partial: PanProbe 1/17 (beta.14–beta.16), beta.17 candidate 2/17 real (dev smoke); PanPlay cube exit 3/5. G77: PanProbe 0/17, no physical device (beta.15) | G77 gpu_id missing from model table (not in beta.17); Vulkan 1.1 reporting (DXVK needs 1.3), no compute pre-raster path, replay fault, BC blit on JM | [v9-jm/README.md](v9-jm/README.md) |
| v10 | Mali-G610 MC6 (2 devices), Mali-G610 MC4 | 0xa8670000 | 5.10 android12 (MC6), 5.15 android13 (MC4) / CSF, uAPI not logged | 4 (3 devices) | G610 MC4: PanProbe 15/17 (beta.15), device, queues, heaps and shaders work. Both G610 MC6: fail at DXVK adapter selection (beta.14, beta.15) | BC emulation disabled (108), gralloc mapper (014 + wsi/017); EXEC_INIT EPERM and old-CSF layouts only on 5.10 (jm-v9/004, 110) | [v10/README.md](v10/README.md) |
| v11 | Mali-G615 MC6, Mali-G615 MC2 | 0xb8a31030 | 6.1 android14, 6.1 custom / CSF (uAPI 1.21 on the dev device; not logged on tester devices) | 37 (8 devices) | Dev device: PanProbe 17/17, PanPlay cubes exit 0 (beta.15–beta.16). Stock ROMs: 16/17 (incl. first beta.16 upload); stock 2311DRK48G D3D8–D3D11 cubes exit 0 on x86/x64/ARM64EC at FL11_1 | Gralloc mapper on stock ROMs (SELinux denies `mapper/mediatek`; 014 + wsi/017 untested there) | [v11/README.md](v11/README.md) |
| v12 | Mali-G720 MC7 | 0xc8700010 | 6.6 android15 (4k) / CSF uAPI ~1.30 | 6 (1 device) | Partial: PanProbe 14/17 in all 6 runs (beta.14, beta.15); Android swapchain passes; games not tested | v12+ per-viewport depth runs (gs_viewport_depth), depthBounds and shaderOutputViewportIndex gates (109) | [v12/README.md](v12/README.md) |
| v13 | Immortalis-G925 MC12 (PanVK names it "Mali-G725 MC12") | 0xd8300015 | 6.6 android15 (4k) / CSF, uAPI not logged | 14 (2 devices; 2 records ran a third-party driver) | PanProbe 14/17 on both devices (beta.15); D3D8–D3D11 cubes exit 0 at FL11_0; Core Keeper (D3D11) reaches Steamworks init | Same three v12+ gates as G720 (109); FL11_0 because VPSA is opt-in on v13+; D3D8/D3D9 cubes run at about 1/3 of the D3D10/D3D11 frame count (cause unknown) | [v13/README.md](v13/README.md) |
| v14 | Mali-G1-Ultra MC12 | 0xe8800010 | 6.12 android16 (4k) / CSF, uAPI not logged | 8 (1 device) | Fails at `vkCreateDevice` (beta.15): device enumerates, DXVK finds it, first allocation fails; PanPlay cube exit 1 in all 8 runs. No new data | `KBASE_IOCTL_MEM_ALLOC_EX` returns ENOTTY -> `VK_ERROR_OUT_OF_DEVICE_MEMORY` (110) | [v14/README.md](v14/README.md) |
| v7 (Bifrost, not a PanVK target) | Mali-G52 MC2 | 0x74021000 | 6.6 android15 (4k) / vendor driver | 1 (1 device) | Not applicable: PanProbe ran on the vendor system Vulkan driver (r49p1), 5/17 | Not applicable (PanVK not involved) | None (not applicable) |
| Non-Mali | Adreno 825 | - | 6.6 android15 / Qualcomm kgsl | 1 (1 device) | Not applicable: ran on Qualcomm system driver, 0/1 geometry | Not applicable (PanVK not involved) | None (not applicable, no folder) |

### Architecture summary notes
- **v9 (17 records, 2 devices):** Lenovo tablet TB336FU (MT8755, G57). Progressed from unsupported on beta.13 to 1/17 on beta.14–beta.16. The beta.17 candidate (developer smoke, not uploaded) reports 3/17, but `vmr_secondary` only skipped, so 2/17 are real. It logs JM uAPI 11.38 and `TEXTURE_FEATURES[0] = 0xf7fe03fe` (BC1-BC3 native). Blocked by Vulkan 1.1 reporting and absence of compute pre-raster lowering. New: POCO 21061110AG (MT6891, Mali-G77 MC9, `c853b7b6`, `40359b15`) gets 0/17 on beta.15. The kbase node opens on its 4.14 kernel, but `0x90800011` decodes to `PAN_PROD_ID(9, 0, 0)`, which has no entry in the model table, so PanVK silently drops the device (see "Model table status").
- **v10 (4 records, 3 devices):** New: vivo V2284A (MT6896Z/CZA, G610 MC6, 5.10.233 android12, `5b799ebb`, beta.15) runs a user D3D game, which exits with code 3 because DXVK skips the adapter for missing `textureCompressionBC` (blocker 1). Xiaomi 23054RA19C (MT6896, G610 MC6, 5.10). Physical device enumerates cleanly in PanPlay, but DXVK aborts adapter selection when `textureCompressionBC` is missing. New: Xiaomi 23090RA98I (MT6886, G610 MC4, 5.15 android13, `854bef5b`) gives the first v10 PanProbe run, 15/17 on beta.15. `vkCreateDevice`, CSF queue groups, tiler heaps and shaders all work. Only `bc_decode` (blocker 1) and `swapchain_lifecycle` (blocker 2, `mapper load failed`) fail, and there is no EXEC_INIT warning.
- **v11 (37 records, 8 devices):** New on 2026-10-06: the stock 2311DRK48G runs D3D8, D3D9, D3D10 and D3D11 cubes on x86, x64 and ARM64EC to exit 0 (FL11_1) and repeats PanProbe 16/17. Two of its PanProbe runs hit the runner's 60 s `large_draw` limit with Mesa shader logging on (harness limit, not a driver fault). New stock devices: LAVA LXX525 (16/17), Infinix GT 30 X6876 (old pre-beta.13 `.so`, swapchain-only run fails) and Redmi Note 14 Pro 5G 24090RA29I (D3D8 x86 cube exit 0). The first beta.16 tester upload (Infinix X6857, `96a133dc`) scores 16/17. Developer G615 MC6 passes 17/17 on PanProbe and completes D3D9 cube exit 0 on PanPlay. Commercial stock ROMs pass 16/17, blocked only by gralloc mapper loading on swapchain creation. New: the stock 2311DRK48G repeats 16/17 on beta.15 (`30d35b5c`) and logs an SELinux denial for the `mapper/mediatek` service lookup. A new stock device, 2406APNFAG (MT6897), runs the PanPlay D3D8 cube to exit 0 on x86 and x64 (beta.14).
- **v12 (6 records, 1 device):** Poco X7 Pro (MT6899, G720 MC7). Passes 14/17 on beta.14 and in five beta.15 runs, including swapchain creation. Fails 3 tests gated by viewport-depth and depth-bounds features.
- **v13 (14 records, 2 devices):** Redmi 25060RK16C (MT6991, Immortalis-G925 MC12, `e74f908f`) and Xiaomi 15T Pro 2506BPN68G (MT6991, same GPU). Both pass 14/17 on beta.15 with the same three failures as the G720 (gs_viewport_depth, vs_viewport_index, depth_bounds). BC emulation and Android swapchain pass (about 121 FPS present). New: first v13 PanPlay data. D3D8, D3D9, D3D10 and D3D11 cubes exit 0, and DXVK picks FL11_0 because `vertexPipelineStoresAndAtomics` is off on v13+. Core Keeper (Unity, D3D11) creates its device and reaches Steamworks init. D3D8/D3D9 cubes log 420-450 frames in 27 s against 1360-1400 for D3D10/D3D11; the cause is unknown. Two runs used a third-party driver and are not counted as results.
- **v14 (8 records, 1 device, no new data):** vivo V2515 (MT6993, Mali-G1-Ultra MC12, `0xe8800010`, 6.12 android16 with 4 KiB pages), eight PanPlay cube runs on beta.15 (D3D8/D3D9/D3D11, x86/x64/ARM64EC). The gpu_id matches the G1-Ultra model row and DXVK finds the device with `textureCompressionBC = 1`. Every run then fails in `vkCreateDevice` (`VK_ERROR_OUT_OF_DEVICE_MEMORY`) because the kernel rejects `KBASE_IOCTL_MEM_ALLOC_EX` with ENOTTY (new cross-arch blocker 5).
- **v7 (1 record):** NLA-LX2 (MT6769V/CB, Mali-G52 MC2, Bifrost, `35d84e6a`). PanProbe ran on the vendor system Vulkan driver (`driverType: System Vulkan`), which scored 5/17. PanVK was not involved. Bifrost is not a target of the universal builds.
- **Non-Mali (1 record):** Snapdragon SM8735 with Adreno 825 executed on Qualcomm's proprietary driver; PanVK was not involved.

## Cross-arch blockers

### 1. BC emulation decision
- **Affected archs & records:**
  - v9 Valhall JM: Lenovo tablet TB336FU (e.g. record `49abcff8`). PanProbe `bc_decode` logs failure for every format; `vulkaninfo` reports `textureCompressionBC = false`.
  - v10 Valhall CSF: Xiaomi device 23054RA19C (`559830af`, `0b151151`) and vivo V2284A (`5b799ebb`, beta.15, user D3D game exit 3). DXVK skips adapter due to missing `textureCompressionBC`. Also the G610 MC4 23090RA98I (`854bef5b`): `bc_decode` FAIL with every BC format `optimal=0x0 ifp=-11`, and vulkaninfo `textureCompressionBC = false`.
  - Contrast: v11 (G615), v12 (G720), v13 (G925, `e74f908f`, `feffc979`) and v14 (G1-Ultra, DXVK feature dump) report `textureCompressionBC = true` via emulation; v11–v13 pass `bc_decode`.
  - Candidate fix: unreleased `patches/csf-v11/108-emulate-bc-unless-every-bc-format-is-native.patch` (not yet verified on a G610).
  - Not yet known on v9 G77 (`c853b7b6`, `40359b15`): no physical device was created, so BC reporting was never reached.
  - **beta.17 status:** 108 emulates BC unless all ten BC formats are native (`src/panfrost/vulkan/panvk_physical_device.c:1834`, `panvk_vX_physical_device.c:294`, beta.17 tree). On the G57 the candidate logs `texture_features 0xf7fe03fe ...` and `BC emulation on (native compressed mask 0xf7fe03fe)`. That mask has BC1-BC3 native and BC4-BC7 missing, which confirms the partial-native theory. `bc_decode` then exposes every format and passes raw and copy decode, but all 16 blits fail on JM. No v10 device has run 108.
- **Exact log lines:**
```
info: Found device: Mali-G610 MC6 (panvk 26.2.99)
info: Skipping: Device does not support required feature 'textureCompressionBC'
warn: DXVK: No adapters found.
err: Failed to initialize DXVK.
CUBE: FAIL create hr=0x80004005
```
```
FORMAT BC1_RGB_UNORM optimal=0x0 linear=0x0 ifp=-11 FAIL
```
- **Root cause:**
  - `panvk_bc_emul_enabled()` at `src/panfrost/vulkan/panvk_physical_device.c:1815-1826` turns software emulation off if kbase `TEXTURE_FEATURES` reports BC1 (DXT1, bit 7).
  - `has_texture_compression_bc()` at `src/panfrost/vulkan/panvk_vX_physical_device.c:294-302` requires all 10 BC format bits natively (`panvk_vX_physical_device.c:338`).
  - When emulation is disabled without full native support, `get_image_plane_format_features()` at `src/panfrost/vulkan/panvk_physical_device.c:1837-1839` returns 0 for every BC format, including native ones.
  - Texture features derive from `GET_GPUPROPS` (`src/panfrost/lib/kmod/kbase_kmod.c:372` -> `src/panfrost/lib/pan_props.c:84`), where full BC is mask `0x1ff80` (`src/panfrost/genxml/common.xml:76`). G57 and G610 likely report native BC1 without the remaining formats.
- **Fix options & effort:** Emulate all BC formats unless the complete native mask (`0x1ff80`) is present; otherwise expose per-format native support. Maintain consistency with image view/creation logic at `src/panfrost/vulkan/panvk_image.c:674`. Effort: 4–8 h including validation. Unblocks DXVK adapter selection on v10 (v9 remains blocked by Vulkan 1.1 requirements).

### 2. Android gralloc mapper hard-coded to MediaTek stable-C mapper
- **Affected archs & records:**
  - v9 tablet: all beta.14 and beta.15 runs (`49abcff8`, `6ff8855c`, `fad0cb63`).
  - v10: G610 in third-party Winlator fork (issue #5 comment); G610 MC4 23090RA98I (`854bef5b`, vendor API 33): `mapper load failed`, `u_gralloc_get_buffer_basic_info failed`, swapchain -1000072003.
  - v11 stock ROMs: Infinix X6857 (`7962f613`, `54cdc0c7`, and `96a133dc` on beta.16), 24090RA29G (`bd79c8af`), stock 2311DRK48G (`ec20d5f3`, `30d35b5c`, `abd5de92`, `75fc84bf`, `426e3e4b`, `9a5b73cb`, `0b225021`, `cddc1702`), LAVA LXX525 (`01df95bb`, `652453a6`) and Infinix X6876 (`2fe57299`, old `.so`). On the stock 2311DRK48G and LXX525 the binder lookup is refused: `Failed to get isDeclared for mapper/mediatek: Status(-1, EX_SECURITY): 'SELinux denied for service.'`, and Android's own client also fails: `Gralloc5: Failed to load mapper.mediatek.so`.
  - G715 on Google Tensor (issue #7).
  - Passing devices: G720 Poco X7 Pro (vendor API 202404), G925 Redmi 25060RK16C (`e74f908f`) and G925 2506BPN68G (`feffc979`), both vendor API 202404, and developer G615 (`32144abe`, `71dc96c0`).
  - Candidate fix: unreleased `patches/android/014-vendor-neutral-gralloc-mapper-and-hidl-mapper4.patch` plus `patches/wsi/017-android-swapchain-owned-ahb.patch`. All devices log harmless `No gralloc hwmodule detected`.
  - **beta.17 status:** In a real app the vendor mapper still does not load on the G57 (linker namespace and SELinux). The new `cpu-linear-probe` fallback (`src/vulkan/runtime/vk_android.c:759`, beta.17 tree) handles driver-allocated swapchain AHBs: it writes cookies through the dma-buf mmap, reads them back with `AHardwareBuffer_lock`, and accepts LINEAR only on a 9/9 match. On the G57 it gives `match=9/9 -> LINEAR` and `swapchain_lifecycle` passes at 90.9 FPS. On stock G615 ROMs it is untested; it works only if gralloc hands out linear RGBA buffers there. The HIDL mapper4 backend is unreachable from app namespaces.
  - Not reached on v9 G77 (`c853b7b6`, `40359b15`; first API level 30, Android 13): no physical device. Expect this blocker there next, because the device is a MediaTek board with vendor API below 34.
- **Exact log lines:**
```
FAIL vkCreateSwapchainKHR res=-1000072003 phase=create_swapchain
MESA: [P0A-V19-FULLPLANE] mapper load failed
MESA: [P0A-V19-FULLPLANE] complete metadata unavailable rc=-95; refusing guessed layout
No gralloc hwmodule detected (video buffers won't be supported)
```
- **Root cause:**
  - Introduced by `patches/android/013-vendor-mapper-metadata.patch`.
  - `src/util/u_gralloc/u_gralloc_fallback.c:83` hardcodes binder passthrough `open_hal("mapper", "mediatek")`, and `u_gralloc_fallback.c:95, 102` attempts `mapper.mediatek.so` directly.
  - On non-MediaTek platforms (e.g. Tensor) or devices lacking AIMapper stable-C v5 (vendor API < 34), `u_gralloc_fallback.c:106` logs mapper load failed and `u_gralloc_fallback.c:125-130` returns `-ENOTSUP` (-95).
  - `u_gralloc_fallback.c:425-431` refuses guessed layouts, causing `u_gralloc_get_buffer_basic_info()` to fail at `src/vulkan/runtime/vk_android.c:150-152` with `VK_ERROR_INVALID_EXTERNAL_HANDLE` (-1000072003).
  - Calling paths: swapchain creation (`src/panfrost/vulkan/panvk_image.c:803` -> `src/panfrost/vulkan/panvk_android.c:172/125`) and AHB dedicated import (`src/panfrost/vulkan/panvk_device_memory.c:68` -> `src/panfrost/vulkan/panvk_android.c:318/236` -> `src/vulkan/runtime/vk_android.c:687` -> `vk_android.c:152`).
  - Note: PanPlay is unaffected because it presents via X11 software WSI without AHB.
- **Fix options & effort:**
  1. Vendor-neutral mapper discovery (`allocator getIMapperLibrarySuffix` / declared passthrough instance, matching AOSP `Gralloc5.cpp`) plus HIDL mapper4 metadata backend for vendor API < 34: 24–48 h.
  2. Quick configuration: configurable mapper name list via `PANVK_MAPPER_NAMES` and `ro.hardware.gralloc` hints: 4–8 h (insufficient for HIDL mapper4-only vendors).
  3. Constrained fallback for verified linear single-plane RGBA: 6–12 h (blind `AHardwareBuffer_describe` guessing is unsafe without modifier/offset).

### 3. KBASE_IOCTL_MEM_EXEC_INIT ordering
- **Affected archs & records:**
  - v9 Valhall JM: all tablet records (`5ec9f0c1` through `49abcff8`); G77 on a 4.14 kernel (`c853b7b6`, `40359b15`), where it is logged once per test before the device is dropped for its unknown gpu_id.
  - v10 Valhall CSF: 23054RA19C (`559830af`, `0b151151`).
  - Never occurs on G615 (v11), G720 (v12), G925 (v13) or G1-Ultra (v14), nor on the 5.15 G610 MC4 (`854bef5b`), whose shaders run fine. So on v10 it is tied to the 5.10 kernel, not to the architecture.
  - Candidate fix: unreleased `patches/jm-v9/004-init-exec-va-before-jit.patch`.
  - **beta.17 status:** jm-v9/004 runs EXEC_INIT before JIT_INIT with a 4 GiB JM zone, and jm-v9/005 sizes the zones in kernel pages (`src/panfrost/lib/kmod/kbase_kmod.c:1473-1478`, beta.17 tree). The G57 smoke logs no EXEC_INIT failure. The new V2284A 5.10 log (`5b799ebb`, beta.15) also has no EXEC_INIT warning, but it never reached `vkCreateDevice`. No 5.10 kernel has run the fix.
- **Exact log lines:**
```
MESA: warning: kbase: KBASE_IOCTL_MEM_EXEC_INIT failed: Operation not permitted (executable BO allocation will not work)
```
- **Root cause:**
  - In `src/panfrost/lib/kmod/kbase_kmod.c:1391-1396`, `JIT_INIT` runs prior to `EXEC_INIT` (`kbase_kmod.c:1404-1410`). JM requests 1024 pages and CSF requests 0x100000.
  - Older kbase kernels return `-EPERM` when initializing executable VA after JIT initialization or when the zone already exists; newer CSF sets up the zone automatically.
  - On the v9 JM kernel, shaders still run (many PanProbe cases draw correctly), so the warning is not fatal there. Shader BOs use `PAN_KMOD_BO_FLAG_EXECUTABLE` -> `BASE_MEM_PROT_GPU_EX` (`kbase_kmod.c:1624`); if a kernel does reject them, shader upload fails with `VK_ERROR_OUT_OF_DEVICE_MEMORY` at `src/panfrost/vulkan/panvk_vX_shader.c:3133`. The effect on v10 is unknown because no shader has run there yet.
- **Fix options & effort:** Call `KBASE_IOCTL_MEM_EXEC_INIT` before `JIT_INIT` where explicit initialization is needed, and size the JM zone (currently 4 MiB) sensibly. Effort: 6–12 h including JM, older CSF, and newer CSF verification. Low priority until v10 shader submission data confirms failure.

### 4. Mali-G77 gpu_id missing from the model table (silent drop)
- **Affected archs & records:** v9 Valhall JM, POCO 21061110AG, MT6891, Mali-G77 MC9, `0x90800011` (`c853b7b6`, `40359b15`; beta.15).
- **Exact log lines:**
```
MESA: warning: kbase: KBASE_IOCTL_MEM_EXEC_INIT failed: Operation not permitted (executable BO allocation will not work)
FAIL no Mali
FAIL vkEnumeratePhysicalDevices res=-3 phase=init
CRASH signal=11
```
  `FAIL no Mali` is printed by 15 of 17 tests, `CRASH signal=11` by `bc_decode`, and the `res=-3` line by `swapchain_lifecycle`. No `Unknown gpu_id` line appears anywhere.
- **Root cause:**
  - The kbase node opens and the handshake passes on the 4.14 JM kernel: the EXEC_INIT warning (`src/panfrost/lib/kmod/kbase_kmod.c:1408`) is only printed after the device is created.
  - `pan_prod_id()` builds `PAN_PROD_ID(arch_major, arch_minor, product_major)` (`src/panfrost/model/pan_model.h:125-126`). `0x90800011` gives arch 9.0, arch_rev 8, product_major 0, r0p1, so `PAN_PROD_ID(9, 0, 0)`.
  - The v9 entries are `PAN_PROD_ID(9, 0, 1)` and `(9, 0, 3)` "G57" and `(9, 2, 4)` "G68" (`src/panfrost/model/pan_model.c:87-91`). The "G77" string in those rows is the performance counter set name, not a model match. `pan_get_model()` (`src/panfrost/model/pan_model.c:148-158`) returns NULL.
  - `panvk_physical_device_init_kbase()` then fails with `VK_ERROR_INCOMPATIBLE_DRIVER` "Unknown gpu_id" (`src/panfrost/vulkan/panvk_physical_device.c:1338-1345`). The kbase enumerator treats that as "no device here" and skips it (`src/panfrost/vulkan/panvk_instance.c:223-228`), so the driver enumerates 0 devices. The other tests then print `FAIL no Mali`. The swapchain runner reports `res=-3` for the same run. The same `-3` was seen on the G57 tablet under beta.13 when v9 was rejected (`94f4d2bb`). Where the `-3` comes from (runner or loader) was not traced.
  - The error text is never printed in release builds without a debug messenger (`src/vulkan/runtime/vk_log.c:114-119`), which is why the tester log has no `Unknown gpu_id` line.
  - The `bc_decode` SIGSEGV is a PanProbe runner crash on the no-device path. It matches the beta.13 SIGBUS on the G57 tablet (`80b7a0c9`) and is not a driver fault.
- **beta.17 status: not fixed.** The beta.17 tree still has only the `(9, 0, 1)`, `(9, 0, 3)` and `(9, 2, 4)` v9 rows (`src/panfrost/model/pan_model.c:87-91`, beta.17 tree). Patch 118 adds `mesa_loge("panvk: Unknown gpu_id ...")` (`src/panfrost/vulkan/panvk_physical_device.c:1356`, beta.17 tree), so a rerun would at least show the reason in logcat. The device would still enumerate 0 GPUs.
- **Fix options & effort:** Add a `VALHALL_MODEL(PAN_PROD_ID(9, 0, 0), 0, "G77", "G77", ...)` row. Tile-buffer sizes and rates are not known from the logs; the G57 values are a starting guess that must be checked against the G77 TRM or kbase. 1–2 h for the row, then a tester rerun. After that the G77 is expected to hit the same v9 blockers as the G57 (Vulkan 1.1, no compute pre-raster, BC, gralloc mapper). Also log the unknown gpu_id with `mesa_logw` so testers can see this case. That needs about 0.5 h.

### 5. `KBASE_IOCTL_MEM_ALLOC_EX` rejected with ENOTTY (v14)
- **Affected archs & records:** v14, vivo V2515, MT6993, Mali-G1-Ultra MC12, `0xe8800010`, 6.12 android16 (4 KiB pages), all eight PanPlay records on beta.15 (`8fd54bb9`, `d321b12a`, `eb5ccca3`, `9aca5216`, `0cc289c7` retrieved; `703077ca`, `0a6b82e7`, `fa35ee7e` known from D1 only). Not seen on v11 (uAPI 1.21), v12 or v13, which take the same code path successfully.
- **Exact log lines:**
```
info:  Found device: Mali-G1-Ultra MC12 (panvk 26.2.99)
E MESA    : kbase: KBASE_IOCTL_MEM_ALLOC_EX failed: Inappropriate ioctl for device
err:   Failed to create Vulkan device: VK_ERROR_OUT_OF_DEVICE_MEMORY, retrying 'safe mode'.
err:   Failed to create Vulkan device: VK_ERROR_OUT_OF_DEVICE_MEMORY
CUBE: FAIL CreateDevice hr=0x8876086a
```
- **Root cause:**
  - The gpu_id is recognised: `0xe8800010` -> `PAN_PROD_ID(14, 8, 0)` variant 4 "G1-Ultra" (`src/panfrost/model/pan_model.c:115`), and DXVK reads the full feature list.
  - BO allocation uses `KBASE_IOCTL_MEM_ALLOC_EX` (ioctl 59, 64-byte union) for every CSF kernel with uAPI >= 1.9 (`src/panfrost/lib/kmod/kbase_kmod.c:1909-1927`). There is no fallback, so the first allocation in `vkCreateDevice` fails and the driver reports `VK_ERROR_OUT_OF_DEVICE_MEMORY`.
  - `ENOTTY` means the kernel's dispatcher did not match the request at all. An incomplete context setup would give `EPERM`, and a field-layout change alone would not give `ENOTTY`. This vendor kbase therefore lacks ioctl 59 or encodes it with a different size. Which one is unknown, because the kbase uAPI version is only a debug log (`src/panfrost/lib/kmod/kbase_kmod.c:1344`). This is inferred from the error code and not checked against vendor kernel source.
  - `pan_kmod_driver_version_at_least()` treats any higher major version as compatible (`src/panfrost/lib/kmod/pan_kmod.h:605-612`). If this kernel reports a new major, queue-group creation (`kbase_kmod.c:589`) and tiler-heap init (`kbase_kmod.c:1170`) may also need checking.
  - Page size is not the cause: the kernel is a `-4k` build, matching the 4096-byte page size in `kbase_kmod.c:1881`.
- **Fix options & effort:** On `ENOTTY`, retry once with a freshly initialised legacy `KBASE_IOCTL_MEM_ALLOC` request (`kbase_kmod.c:1932`) and cache "EX unsupported" for the device. Current requests use no fixed address and no FIXED/FIXABLE flags, so the legacy call is equivalent. Keep other errors fatal. Log the uAPI version with `mesa_logi` once per process. 1–2 h plus a tester rerun.
- **beta.17 status:** Patch 110 does this. On ENOTTY or EINVAL before the first successful allocation it falls back to `MEM_ALLOC` and sticks to the working path (`src/panfrost/lib/kmod/kbase_kmod.c:2023-2055`, beta.17 tree). It logs `kbase: CSF driver, uAPI version X.Y, page_size=N` at INFO (`kbase_kmod.c:1423`, beta.17 tree). It also tries the other public queue-group and tiler-heap layouts on EINVAL/ENOTTY. It is untested on v14. No new v14 data arrived in this pass.

### 6. v12+ viewport depth runs, `depthBounds` and `shaderOutputViewportIndex`
- **Affected archs & records:** v12 G720 (all 6 runs, `94a3d66c`, `47011dc2`, `96d408f3`, `8400732a`, `da97c873`, `f774eb29`) and v13 G925 (`e74f908f`, `feffc979`). All score 14/17 and fail `gs_viewport_depth`, `vs_viewport_index` and `depth_bounds`. v14 is expected to match once it gets past `vkCreateDevice`.
- **Root cause and fix:** See [v12/README.md](v12/README.md#root-causes). **beta.17 status:** Patch 109 writes each viewport run's depth range into the v12+ VIEWPORT_LOW min/max depth words (`src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c:1025-1071`, beta.17 tree) and reports both features on v10+ (`panvk_vX_physical_device.c:330`, `:443`, beta.17 tree). Patch 116 keeps FPK and early ZS while the depth bounds test is off. Neither has run on v12 or v13.

### 7. FL11_0 on v13+ (`vertexPipelineStoresAndAtomics` off)
- **Affected archs & records:** v13 G925, every DXVK run on 2506BPN68G (`3ca3549c`, `9376b55c`, `3d4bf8f3`, `d73ee13a`). DXVK prints `vertexPipelineStoresAndAtomics : 0` and `Maximum supported feature level: D3D_FEATURE_LEVEL_11_0`. The stock G615 gets FL11_1 with the same DXVK.
- **Root cause:** VPSA is on for v10-v12 and is a drirc opt-in (`enable_vertex_pipeline_stores_atomics`) on v13+ (`src/panfrost/vulkan/panvk_vX_physical_device.c:345-348`, beta.17 tree). DXVK 3.1.1 needs `logicOp` and VPSA for FL11_1. A third-party driver on the same phone exposes VPSA and gets FL11_1, but that does not show the feature is correct on PanVK's v13 path.
- **beta.17 status:** No patch. Open question: is VPSA safe on v13/v14 without gpu_prerast? A v13 PanProbe `vertex_stores` run with the drirc option would answer it.

## Ranked fix plan (all archs)

| Rank | Item | Impact / Unblocks | Effort |
|---|---|---|---|
| 0 | Release beta.17 after the G615 gate (PanProbe 17/17, CTS sync + memory 56 known failures, NFS HUD at least 90 fps), then ask testers for reruns | Puts items 1-4, 7-9 in front of testers; none of them has tester data yet | G615 checklist + release |
| 1 | BC emulation decision (108, in beta.17) | Fixes `bc_decode` on G610 MC4 and unblocks DXVK adapter selection on v10 (3 G610 devices); exposes BC on v9 | Done; G610 rerun |
| 2 | `MEM_ALLOC_EX` ENOTTY fallback to `MEM_ALLOC` + uAPI version log (110, in beta.17) | Unblocks `vkCreateDevice` on v14 (Mali-G1-Ultra, 8 failing records) | Done; V2515 rerun |
| 3 | Vendor-neutral gralloc mapper + `cpu-linear-probe` (android/014 + wsi/017, in beta.17) | Fixes Android-surface swapchain on stock ROMs if their gralloc gives linear RGBA (G57 proven; stock G615/G610 untested) | Done; stock G615 rerun |
| 4 | v12+ per-viewport depth runs + lift depthBounds/shaderOutputViewportIndex gates (109, in beta.17) | Unblocks remaining 3 PanProbe failures on G720 and both G925 devices | Done; G720/G925 rerun |
| 5 | G77 model-table row (`PAN_PROD_ID(9, 0, 0)`) | Lets PanVK create a physical device on Mali-G77 (v9 JM); after that it reaches the same v9 blockers as the G57. Not in beta.17 | 1–2 h + tester rerun |
| 6 | VPSA on v13+ (blocker 7) | FL11_1 on v13 (and likely v14) | Test run with the drirc option first; effort not estimated |
| 7 | v13 D3D8/D3D9 frame-rate gap | D3D8/D3D9 games on v13 | Not estimated (cause unknown) |
| 8 | v9 replay fault, BC blit on JM | Fixes `gpu_prerast_slice` DEVICE_LOST on replay and `bc_decode` blits on Valhall JM | 4–8 h diagnosis + 1–2 days (replay); blit not estimated |
| 9 | v9 Vulkan 1.3 reporting then JM compute pre-raster port | Step 1 (report Vulkan 1.3 core) unblocks DXVK entry on v9; Step 2 ports compute pre-raster path to JM job chains for GS, multi-viewport, fill mode, clip/cull | 1–3 days (audit & 1.3 reporting); 15–30 days (compute pre-raster port) |
| 10 | G610 MC6 PanProbe run on a 5.10 kernel with beta.17 | Shows the 5.10 queue-group and heap layouts (110 logs them) and whether jm-v9/004 matters on CSF 5.10 | Tester verification (0 code effort) |

### Plan implementation notes
- **Items 1-4:** Implemented in the beta.17 candidate; see "beta.17 patch per blocker" above and blockers 1, 2, 5 and 6. Tester reruns decide whether they work.
- **Item 5 (G77):** Add the model row in `src/panfrost/model/pan_model.c:87-91` (next to the G57 rows). The unknown-gpu_id log is done (118). Separately, make PanProbe `bc_decode` handle zero devices without crashing (app side).
- **Item 6 (VPSA on v13+):** The gate is `panvk_vX_physical_device.c:345-348` (beta.17 tree). Run PanProbe `vertex_stores` and the CTS vertex atomics cases on a v13 device with `enable_vertex_pipeline_stores_atomics` before changing the default.
- **Item 8 (v9):** Replay crash occurs in `gpu_prerast_slice` after `gpu_written_indirect` when resubmitting cmdbuf (`tests/dxvk/vulkan/gpu_prerast_slice.c:488`, a repo test path, not Mesa). Likely fix (hypothesis, not yet proven by a descriptor dump): restore the GPU-written malloc-job draw payload (`src/panfrost/vulkan/jm/panvk_vX_cmd_draw.c:2309`) before resubmission. VMR secondary requires setting `flags_0.evaluate_per_sample` and updating `fb.nr_samples`.
- **Item 9 (v9):** Bundled DXVK requires Vulkan 1.3. Reporting 1.3 requires auditing feature booleans currently hidden by 1.1 reporting (`panvk_vX_physical_device.c:817`). Pre-raster compute lowering is CSF-only today (`src/panfrost/vulkan/meson.build:102`). Subsequent JM work includes tessellation (5–15 days), XFB (3–7 days), and indirect draws (2–5 days).
- **Item 10 (G610 on 5.10):** The 5.15 G610 MC4 (`854bef5b`) already shows that `vkCreateDevice`, queue groups, tiler heaps and shaders work on a v10 kernel. Only 5.10 remains unknown (23054RA19C, V2284A). With 110 the driver tries every public queue-group layout (32, 40 and 112 bytes) in a version-defined order and logs the one the kernel accepts.
- **Diagnostics (done in beta.17, unreleased):** 110 and 118 log the kbase uAPI version, page size, queue-group and heap layouts, the allocation ioctl, gpu_id, `TEXTURE_FEATURES` and the BC decision at INFO. The apps now put a run-window logcat, device facts and DXVK logs into the upload zip (`worklogs/apps/upload-logs.md`). Verified only on the G57.

## Model table status

Hardware matching is performed in `src/panfrost/model/pan_model.c`. No "Unknown gpu_id" line appears in any record, but release builds do not print it (`src/vulkan/runtime/vk_log.c:114-119`). The G77 records (`c853b7b6`, `40359b15`) are a confirmed unknown-gpu_id case (cross-arch blocker 4; `src/panfrost/vulkan/panvk_physical_device.c:1213/1345`).

- **Vendor DDK and API levels seen:**
  - Mali-G57: DDK r38p1, first API level 33.
  - Mali-G610: first API level 31 on both MC6 devices (GL version not captured in PanPlay); DDK r38p1 and first API level 33 on the MC4.
  - Mali-G615: DDK r44p1, first API level 34.
  - Mali-G720: DDK r49p1, first API level 202404.
  - Mali-G77: DDK r32p1, product first API level 30, vendor Vulkan 1.1.177.
  - Immortalis-G925: DDK r49p1, first API level 202404.
  - Mali-G1-Ultra: DDK not captured (PanPlay only), board first API level 202504, product first API level 36.
  - Mali-G52 (system driver record): DDK r49p1, board first API level 30.
  - All tested devices that set it report `ro.hardware.gralloc = common` (not set on the G77 device).
- **Recognised gpu_ids seen in uploads:**
  - `0x90930010`: arch 9.0, product 3, r0p1 -> `PAN_PROD_ID(9, 0, 3)` "G57" (`src/panfrost/model/pan_model.c:89`).
  - `0xa8670000`: arch 10.8, product 7, r0p0 -> `PAN_PROD_ID(10, 8, 7)` "G610" (`src/panfrost/model/pan_model.c:93`).
  - `0xb8a31030`: arch 11.8, product 3, r1p3 -> `PAN_PROD_ID(11, 8, 3)` v4 "G615" (`src/panfrost/model/pan_model.c:108`), shared by MC2 and MC6 variants.
  - `0xc8700010`: arch 12.8, product 0, r0p1 -> `PAN_PROD_ID(12, 8, 0)` v4 "G720" (`src/panfrost/model/pan_model.c:111`).
  - `0xd8300015`: arch 13.8, product 0, r0p1 -> `PAN_PROD_ID(13, 8, 0)` v4 "G725" (`src/panfrost/model/pan_model.c:113`). The vendor GL renderer says "Mali-G925-Immortalis MC12", but PanVK reports "Mali-G725 MC12". Immortalis-G925 therefore matches the G725 row. Only the name differs, which is cosmetic. The beta.17 tree keeps the row as "G725" (`src/panfrost/model/pan_model.c:113`, beta.17 tree). Two v13 records (`1ba0b823`, `a5fa8644`) ran a third-party driver that reports deviceID `0xd8300010`. That value differs only in the low revision bits, so it is the same GPU, not a new gpu_id.
  - `0xe8800010`: arch 14.8, product 0, r0p1 -> `PAN_PROD_ID(14, 8, 0)` v4 "G1-Ultra" (`src/panfrost/model/pan_model.c:115`). PanVK names it "Mali-G1-Ultra MC12", matching the vendor name. First v14 gpu_id seen.
  - `0xa8670000` is shared by the G610 MC6 and MC4; only the core count differs.
- **Not a PanVK target:** `0x74021000` (Mali-G52 MC2, Bifrost v7) ran on its vendor driver only.
- **Unrecognised gpu_id seen:**
  - `0x90800011` (Mali-G77 MC9): arch 9.0, arch_rev 8, product 0, r0p1 -> `PAN_PROD_ID(9, 0, 0)`, with no row in `src/panfrost/model/pan_model.c:87-91`. The device is dropped (cross-arch blocker 4). Still no row in the beta.17 tree.
  - Mali-G715 MC7 (Pixel 8, issue #7): enumerates as `PAN_PROD_ID(11, 8, 2)` v4 (`src/panfrost/model/pan_model.c:106`).
- **Still-unseen GPUs in uploads:**
  - v10: G710, G510, G310
  - v12: G620, Immortalis-G720
  - v9: G68, G78 (`PAN_PROD_ID(9, 2, 4)` row exists; real gpu_ids not seen)
  - v13: G625 (missing from model table), plain G725 (`src/panfrost/model/pan_model.c:113`; Immortalis-G925 is now seen and matches this row)
  - v14: G1-Premium and G1-Pro (`src/panfrost/model/pan_model.c:117-122`; G1-Ultra is now seen)

## GitHub issues cross-check

| Issue | Device / GPU | Backend data status | Likely cause | Doc link |
|---|---|---|---|---|
| #2 | Unspecified / GTA IV tester | No backend record | `VK_ERROR_OUT_OF_DEVICE_MEMORY` on staging buffer allocation; driver allocation leak or physical memory exhaustion | [v11/README.md](v11/README.md) |
| #4 | Poco X8 Pro (Dimensity 8500, G720-class) | No backend record (no upload shows Mali on system driver) | App did not load PanVK; fell back to vendor driver (reported Vulkan 1.3 / 150 extensions vs PanVK 1.4 / 188 extensions) | [v12/README.md](v12/README.md) |
| #5 | Mali-G610 MC6 & Mali-G615 (Poco X6 Pro) | 2 backend records on G610 (`559830af`, `0b151151`, both fail adapter selection); G615 heap usage not reproduced | G610 `vkCreateSwapchainKHR` assertion in Winlator fork matches gralloc blocker 2, now reproduced in PanProbe on a G610 MC4 (`854bef5b`); G615 memory report relates to beta.13 upfront 160 MiB prerast arena commit | [v10/README.md](v10/README.md), [v11/README.md](v11/README.md) |
| #6 | Pixel 8 Pro (Mali-G715) | No backend record (no logs submitted) | Unknown (no logs); possibly the same gralloc mapper issue as #7 | [v11/README.md](v11/README.md) |
| #7 | Pixel 8 (Mali-G715 MC7, Google Tensor) | No backend record in uploads table; issue attachment log uses pre-beta.13 build | Black screen caused by `AllocateMemory` returning -1000072003 (`VK_ERROR_INVALID_EXTERNAL_HANDLE`) due to hard-coded MediaTek gralloc mapper on Tensor (or buffer/fd import failure) | [v11/README.md](v11/README.md) |

### Issue details and correlations
- **Issue #2:** Staging buffer allocation failure (`Failed to allocate staging buffer memory, res -2`) during GTA IV gameplay. Indicates device out of memory or a memory leak under repeated staging buffer creation. No upload record exists from this run.
- **Issue #4:** Poco X8 Pro running Winlator Mali 1.2 beta. The tester's screen showed Vulkan 1.3 and 150 extensions, which corresponds to the system Mali driver rather than PanVK (which reports Vulkan 1.4 with ~188 extensions and "PanVK-kbase"). PanVK was not loaded by the application.
- **Issue #5:** Covers two observations: a comment noting a `vkCreateSwapchainKHR` assertion failure on Mali-G610 in a Winlator fork (consistent with gralloc mapper failure), and tester reports of ~5–6 GB heap 0 usage on G615 under beta.13 compared to ~1.1 GB on beta.10. Patch 100 commits 160 MiB prerast arenas per `VkDevice` up front; the excessive heap usage reported was not reproduced in backend logs.
- **Issue #6:** Reports crash on Pixel 8 Pro (Mali-G715) without logs.
- **Issue #7:** Pixel 8 running a pre-beta.13 build (`git-5a07217f03`). Winlator wrapper logs reveal `AllocateMemory ... failed with result: -1000072003` (`VK_ERROR_INVALID_EXTERNAL_HANDLE`). This likely matches the gralloc mapper failure (Tensor has no `mapper.mediatek.so`), but a plain buffer/fd import failure is not ruled out.

## Records

Full set of 88 records: 86 D1 upload rows (one of them, id 40, is the same zip as the direct-zip record `c853b7b6`) plus 2 direct tester zips (`40359b15`, `e74f908f`). The first 30 rows are the original snapshot. The rows after `e74f908f` up to `0cc289c7` are D1 ids 30 to 53, and the rows after `0cc289c7` are D1 ids 54 to 90 (id 56 absent), all in upload order. Rows `f907b333` (non-release driver binary whose build ID differs from beta.14), `1ba0b823` and `a5fa8644` (third-party "PanVK-Mali-G925 0.13.0-dev-8gb-allocation" driver) are excluded from test results. Tablet rows with repeated hardware specifications carry MT8755, Mali-G57 MC2, 0x90930010, kernel 5.15 android13, and Android 16. In PanPlay rows, "frame N" is the last `Present frame=N` line logged.

| Anon id | Date (UTC) | App | Arch | Device | SoC | GPU | gpu_id | Kernel | Android | Driver | Result |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 5ec9f0c1 | 2026-10-05 02:20 | panprobe 1.2.0 | v9 | TB336FU | MT8755 | Mali-G57 MC2 | 0x90930010 | 5.15 android13 | 16 | beta.13 | PanProbe 0/1: "FAIL no Mali" (beta.13 has no v9) |
| dae37485 | 2026-10-05 02:24 | panprobe 1.2.0 | v9 | TB336FU | MT8755 | Mali-G57 MC2 | 0x90930010 | 5.15 android13 | 16 | beta.13 | 0/1 no Mali |
| 80b7a0c9 | 2026-10-05 02:25 | panprobe 1.2.0 | v9 | TB336FU | MT8755 | Mali-G57 MC2 | 0x90930010 | 5.15 android13 | 16 | beta.13 | 0/17 no Mali (+ app SIGBUS in bc_decode runner) |
| 079357e1 | 2026-10-05 02:29 | panplay 1.2.0 | v9 | TB336FU | MT8755 | Mali-G57 MC2 | 0x90930010 | 5.15 android13 | 16 | beta.13 | D3D8 x86 cube exit 5 |
| c4d22728 | 2026-10-05 02:47 | panprobe 1.2.0 | v9 | TB336FU | MT8755 | Mali-G57 MC2 | 0x90930010 | 5.15 android13 | 16 | beta.13 | 0/1 no Mali |
| 94f4d2bb | 2026-10-05 03:03 | panplay 1.2.0 | v9 | TB336FU | MT8755 | Mali-G57 MC2 | 0x90930010 | 5.15 android13 | 16 | beta.13 | D3D8 x86 cube: "Failed to enumerate physical devices, res -3", exit 137 |
| 70ce9223 | 2026-10-05 03:27 | panprobe 1.2.1 | v9 | TB336FU | MT8755 | Mali-G57 MC2 | 0x90930010 | 5.15 android13 | 16 | beta.13 | 0/1 |
| fad0cb63 | 2026-10-05 03:42 | panprobe 1.2.1 | v9 | TB336FU | MT8755 | Mali-G57 MC2 | 0x90930010 | 5.15 android13 | 16 | beta.14 | PanProbe 1/17 |
| ef4f82a2 | 2026-10-05 03:50 | panplay 1.2.1 | v9 | TB336FU | MT8755 | Mali-G57 MC2 | 0x90930010 | 5.15 android13 | 16 | beta.14 | D3D8 ARM64EC cube exit 5 |
| 6ff8855c | 2026-10-05 03:57 | panprobe 1.2.1 | v9 | TB336FU | MT8755 | Mali-G57 MC2 | 0x90930010 | 5.15 android13 | 16 | beta.14 | 1/17 |
| 7b576aa7 | 2026-10-05 04:05 | panplay 1.2.1 | v9 | TB336FU | MT8755 | Mali-G57 MC2 | 0x90930010 | 5.15 android13 | 16 | beta.14 | D3D9 ARM64EC cube exit 3: "Skipping: Device does not support Vulkan 1.3" |
| ec20d5f3 | 2026-10-05 04:14 | panprobe 1.2.1 | v11 | 2311DRK48G | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1 android14 | 16 | beta.14 | 0/1 (swapchain_lifecycle only run; FAIL) |
| 94a3d66c | 2026-10-05 04:17 | panprobe 1.2.1 | v12 | 2412DPC0AG | MT6899 | Mali-G720 MC7 | 0xc8700010 | 6.6 android15 | 16 | beta.14 | 14/17 (fail gs_viewport_depth, vs_viewport_index, depth_bounds) |
| 559830af | 2026-10-05 04:38 | panplay 1.2.1 | v10 | 23054RA19C | MT6896 | Mali-G610 MC6 | 0xa8670000 | 5.10 android12 | 15 | beta.14 | D3D10 ARM64EC cube exit 1 after 1 s |
| 0b151151 | 2026-10-05 04:41 | panplay 1.2.1 | v10 | 23054RA19C | MT6896 | Mali-G610 MC6 | 0xa8670000 | 5.10 android12 | 15 | beta.14 | D3D11 ARM64EC cube exit 1 after 1 s |
| e0944b04 | 2026-10-05 05:23 | panprobe 1.2.1 | v9 | TB336FU | MT8755 | Mali-G57 MC2 | 0x90930010 | 5.15 android13 | 16 | beta.14 | 1/17 |
| d3de3763 | 2026-10-05 05:29 | panplay 1.2.1 | v9 | TB336FU | MT8755 | Mali-G57 MC2 | 0x90930010 | 5.15 android13 | 16 | beta.14 | D3D9 ARM64EC cube exit 3 (Vulkan 1.3) |
| 7962f613 | 2026-10-05 08:21 | panprobe 1.2.0 | v11 | Infinix X6857 | MT6878 | Mali-G615 MC2 | 0xb8a31030 | 6.1 android14 | 16 | beta.13 (imported .so) | 16/17 (fail swapchain_lifecycle) |
| 29cd37fb | 2026-10-05 08:51 | panprobe 1.2.1 | none | 25053PC47G | SM8735 | Adreno 825 | - | 6.6 android15 | 16 | Qualcomm system driver | 0/1 geometry (not a Mali; PanVK not involved) |
| 54cdc0c7 | 2026-10-05 08:52 | panprobe 1.2.1 | v11 | Infinix X6857 | MT6878 | Mali-G615 MC2 | 0xb8a31030 | 6.1 android14 | 16 | beta.14 (imported .so) | 16/17 (fail swapchain_lifecycle) |
| bd79c8af | 2026-10-05 09:01 | panprobe 1.2.1 | v11 | 24090RA29G | MT6878 | Mali-G615 MC2 | 0xb8a31030 | 6.1 android14 | 16 | beta.14 | 16/17 (fail swapchain_lifecycle) |
| f907b333 | 2026-10-05 10:57 | panprobe 1.2.1 | v9 | TB336FU | MT8755 | Mali-G57 MC2 | 0x90930010 | 5.15 android13 | 16 | non-release .so (differs from beta.14) | 0/17 "no Mali"; EXCLUDED (non-release .so) |
| 32144abe | 2026-10-05 11:22 | panprobe 1.2.2 | v11 | 2311DRK48I | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1 custom kernel (dev device) | 16 | beta.15 | 17/17 |
| e63009bd | 2026-10-05 11:29 | panplay 1.2.2 | v11 | 2311DRK48I | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1 custom | 16 | beta.15 | D3D9 i686 cube exit 0 (works) |
| 49abcff8 | 2026-10-05 11:36 | panprobe 1.2.2 | v9 | TB336FU | MT8755 | Mali-G57 MC2 | 0x90930010 | 5.15 android13 | 16 | beta.15 | 1/17 |
| 71dc96c0 | 2026-10-05 11:42 | panprobe 1.2.2 | v11 | 2311DRK48I | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1 custom | 16 | beta.15 | 17/17 |
| f2fc9ca6 | 2026-10-05 11:45 | panplay 1.2.2 | v11 | 2311DRK48I | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1 custom | 16 | beta.15 | D3D9 i686 cube exit 0 |
| c853b7b6 | 2026-10-05 15:36 (device time) | panprobe 1.2.2 (direct zip) | v9 | 21061110AG (POCO) | MT6891 | Mali-G77 MC9 | 0x90800011 | 4.14 custom | 13 | beta.15 | 0/17: 15x "FAIL no Mali", bc_decode runner SIGSEGV, swapchain `res=-3`; gpu_id not in model table. Received twice as byte-identical zips |
| 40359b15 | 2026-10-05 15:36 (same run, re-exported 15:52) | panprobe 1.2.2 (direct zip) | v9 | 21061110AG (POCO) | MT6891 | Mali-G77 MC9 | 0x90800011 | 4.14 custom | 13 | beta.15 | Same run and results as `c853b7b6`; only the logcat window differs (no driver lines) |
| e74f908f | 2026-10-05 21:52 (device time) | panprobe 1.2.2 (direct zip) | v13 | 25060RK16C (Redmi) | MT6991 | Immortalis-G925 MC12 (PanVK: "Mali-G725 MC12") | 0xd8300015 | 6.6 android15 (4k) | 15 | beta.15 | 14/17 (fail gs_viewport_depth, vs_viewport_index, depth_bounds) |
| 45e92c09 | 2026-10-05 15:24 | panprobe 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.15 | 1/1 (`fill_mode` only) |
| 1bc8a758 | 2026-10-05 15:24 | panprobe 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1 android14 | 16 | beta.15 | Archive not retrievable (external host only) |
| ff8db0c8 | 2026-10-05 15:24 | panprobe 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1 android14 | 16 | beta.15 | Archive not retrievable (external host only) |
| d7801456 | 2026-10-05 15:25 | panprobe 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1 android14 | 16 | beta.15 | Archive not retrievable (external host only) |
| f0583680 | 2026-10-05 15:25 | panprobe 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1 android14 | 16 | beta.15 | Archive not retrievable (external host only) |
| 36cb2ca0 | 2026-10-05 15:25 | panprobe 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1 android14 | 16 | beta.15 | Archive not retrievable (external host only) |
| 00a5888b | 2026-10-05 15:25 | panprobe 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1 android14 | 16 | beta.15 | Archive not retrievable (external host only) |
| 34239d24 | 2026-10-05 15:26 | panprobe 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1 android14 | 16 | beta.15 | No archive stored |
| 35d84e6a | 2026-10-05 15:32 | panprobe 1.2.2 | v7 | NLA-LX2 | MT6769V/CB | Mali-G52 MC2 | 0x74021000 | 6.6 android15 (4k) | 15 | Vendor system driver (r49p1) | 5/17 on the vendor driver (PanVK not involved) |
| feffc979 | 2026-10-05 16:55 | panprobe 1.2.2 | v13 | 2506BPN68G | MT6991 | Immortalis-G925 MC12 (PanVK: "Mali-G725 MC12") | 0xd8300015 | 6.6.89 android15 (4k) | 16 | beta.15 | 14/17 (fail gs_viewport_depth, vs_viewport_index, depth_bounds) |
| 9641845b | 2026-10-05 17:04 | panplay 1.2.1 | v11 | 2406APNFAG (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.14 | D3D8 x86 cube exit 0 (30 s) |
| a13ae15f | 2026-10-05 17:05 | panplay 1.2.1 | v11 | 2406APNFAG (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.14 | D3D8 x64 cube exit 0 (27 s) |
| 854bef5b | 2026-10-05 17:21 | panprobe 1.2.2 | v10 | 23090RA98I | MT6886 | Mali-G610 MC4 | 0xa8670000 | 5.15.180 android13 | 16 | beta.15 | 15/17 (fail bc_decode, swapchain_lifecycle) |
| 30d35b5c | 2026-10-05 17:24 | panprobe 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.15 | 16/17 (fail swapchain_lifecycle; SELinux denies `mapper/mediatek` lookup) |
| 8fd54bb9 | 2026-10-05 17:46 | panplay 1.2.2 | v14 | V2515 (vivo) | MT6993 | Mali-G1-Ultra MC12 | 0xe8800010 | 6.12.58 android16 (4k) | 16 | beta.15 | D3D8 x86 cube exit 1: `MEM_ALLOC_EX` ENOTTY, `vkCreateDevice` OOM |
| d321b12a | 2026-10-05 17:48 | panplay 1.2.2 | v14 | V2515 (vivo) | MT6993 | Mali-G1-Ultra MC12 | 0xe8800010 | 6.12.58 android16 (4k) | 16 | beta.15 | D3D8 x86 cube exit 1 (same) |
| 703077ca | 2026-10-05 17:48 | panplay 1.2.2 | v14 | V2515 (vivo) | MT6993 | Mali-G1-Ultra MC12 | 0xe8800010 | 6.12 android16 | 16 | beta.15 | D3D9 x86 cube exit 1 (archive not retrievable) |
| eb5ccca3 | 2026-10-05 17:49 | panplay 1.2.2 | v14 | V2515 (vivo) | MT6993 | Mali-G1-Ultra MC12 | 0xe8800010 | 6.12.58 android16 (4k) | 16 | beta.15 | D3D9 ARM64EC cube exit 1 (same) |
| 0a6b82e7 | 2026-10-05 17:53 | panplay 1.2.2 | v14 | V2515 (vivo) | MT6993 | Mali-G1-Ultra MC12 | 0xe8800010 | 6.12 android16 | 16 | beta.15 | D3D11 ARM64EC cube exit 1 (archive not retrievable) |
| 9aca5216 | 2026-10-05 17:53 | panplay 1.2.2 | v14 | V2515 (vivo) | MT6993 | Mali-G1-Ultra MC12 | 0xe8800010 | 6.12.58 android16 (4k) | 16 | beta.15 | D3D11 ARM64EC cube exit 1 (same) |
| fa35ee7e | 2026-10-05 17:55 | panplay 1.2.2 | v14 | V2515 (vivo) | MT6993 | Mali-G1-Ultra MC12 | 0xe8800010 | 6.12 android16 | 16 | beta.15 | D3D11 x64 cube exit 1 (archive not retrievable) |
| 0cc289c7 | 2026-10-05 17:55 | panplay 1.2.2 | v14 | V2515 (vivo) | MT6993 | Mali-G1-Ultra MC12 | 0xe8800010 | 6.12.58 android16 (4k) | 16 | beta.15 | D3D11 x64 cube exit 1 (same) |
| abd5de92 | 2026-10-05 18:40 | panprobe 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.15 | 16/17 (fail swapchain_lifecycle; SELinux denies `mapper/mediatek`) |
| 47011dc2 | 2026-10-05 19:10 | panprobe 1.2.2 | v12 | 2412DPC0AG | MT6899 | Mali-G720 MC7 | 0xc8700010 | 6.6.118 android15 (4k) | 16 | beta.15 | 14/17 (fail gs_viewport_depth, vs_viewport_index, depth_bounds) |
| 75fc84bf | 2026-10-05 19:36 | panprobe 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.15 | 0/1 (`swapchain_lifecycle` only; FAIL -1000072003) |
| 426e3e4b | 2026-10-05 19:37 | panprobe 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.15 | 0/1 (`swapchain_lifecycle` only; FAIL) |
| 2161a553 | 2026-10-05 19:38 | panprobe 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.15 | 0/1 (`large_draw` only; TIMEOUT at 60 s under Mesa shader logging, 11 cases passed) |
| 9a5b73cb | 2026-10-05 19:38 | panprobe 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.15 | 0/1 (`swapchain_lifecycle` only; FAIL) |
| 0b225021 | 2026-10-05 19:40 | panprobe 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.15 | 15/17 (fail swapchain_lifecycle; `large_draw` TIMEOUT under shader logging) |
| 22f66bc0 | 2026-10-05 20:06 | panplay 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.15 | D3D8 x64 cube exit 0 (27 s, frame 1159) |
| 9f9a941e | 2026-10-05 20:09 | panplay 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.15 | D3D9 x86 cube exit 0 (27 s, frame 1195) |
| 97589597 | 2026-10-05 20:10 | panplay 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.15 | D3D9 ARM64EC cube exit 0 (27 s, frame 1132) |
| 4d7b89b8 | 2026-10-05 20:12 | panplay 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.15 | D3D10 x64 cube exit 0 (27 s, frame 1311, FL11_1) |
| 77b612ef | 2026-10-05 20:13 | panplay 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.15 | D3D10 ARM64EC cube exit 0 (27 s, frame 1282) |
| 44601387 | 2026-10-05 20:14 | panplay 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.15 | D3D11 x86 cube exit 0 (28 s, frame 1332, FL11_1) |
| 2fe57299 | 2026-10-06 01:58 | panprobe 1.2.2 | v11 | Infinix X6876 | MT6878 | Mali-G615 MC2 | 0xb8a31030 | 6.1.157 android14 | 16 | pre-beta.13 (imported .so) | 0/1 (`swapchain_lifecycle` only; FAIL) |
| 96a133dc | 2026-10-06 02:47 | panprobe 1.2.2 | v11 | Infinix X6857 | MT6878 | Mali-G615 MC2 | 0xb8a31030 | 6.1.157 android14 | 16 | beta.16 (imported .so) | 16/17 (fail swapchain_lifecycle); first beta.16 tester upload |
| cddc1702 | 2026-10-06 03:08 | panprobe 1.2.2 | v11 | 2311DRK48G (stock) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.15 | 16/17 (fail swapchain_lifecycle) |
| 96d408f3 | 2026-10-06 03:53 | panprobe 1.2.2 | v12 | 2412DPC0AG | MT6899 | Mali-G720 MC7 | 0xc8700010 | 6.6.118 android15 (4k) | 16 | beta.15 | 14/17 (same 3 fails) |
| 8400732a | 2026-10-06 03:55 | panprobe 1.2.2 | v12 | 2412DPC0AG | MT6899 | Mali-G720 MC7 | 0xc8700010 | 6.6.118 android15 (4k) | 16 | beta.15 | 14/17 (same 3 fails) |
| 8a0edf91 | 2026-10-06 04:18 | panplay 1.2.2 | v13 | 2506BPN68G | MT6991 | Immortalis-G925 MC12 (PanVK: "Mali-G725 MC12") | 0xd8300015 | 6.6.89 android15 (4k) | 16 | beta.15 | D3D9 x64 cube exit 0 (27 s, frame 420) |
| 3ca3549c | 2026-10-06 04:19 | panplay 1.2.2 | v13 | 2506BPN68G | MT6991 | Immortalis-G925 MC12 (PanVK: "Mali-G725 MC12") | 0xd8300015 | 6.6.89 android15 (4k) | 16 | beta.15 | D3D11 ARM64EC cube exit 0 (27 s, frame 1360, FL11_0) |
| f05a5a17 | 2026-10-06 04:20 | panplay 1.2.2 | v13 | 2506BPN68G | MT6991 | Immortalis-G925 MC12 (PanVK: "Mali-G725 MC12") | 0xd8300015 | not known (no archive) | 16 | beta.15 | D3D11 x86 cube exit 0 (archive not retrievable) |
| 9376b55c | 2026-10-06 04:21 | panplay 1.2.2 | v13 | 2506BPN68G | MT6991 | Immortalis-G925 MC12 (PanVK: "Mali-G725 MC12") | 0xd8300015 | 6.6.89 android15 (4k) | 16 | beta.15 | D3D11 x64 cube exit 0 (27 s, frame 1399, FL11_0) |
| 45d6064d | 2026-10-06 04:22 | panplay 1.2.2 | v13 | 2506BPN68G | MT6991 | Immortalis-G925 MC12 (PanVK: "Mali-G725 MC12") | 0xd8300015 | not known (no archive) | 16 | beta.15 | D3D8 x86 cube exit 0 (archive not retrievable) |
| 58cc8090 | 2026-10-06 04:23 | panplay 1.2.2 | v13 | 2506BPN68G | MT6991 | Immortalis-G925 MC12 (PanVK: "Mali-G725 MC12") | 0xd8300015 | 6.6.89 android15 (4k) | 16 | beta.15 | D3D8 x64 cube exit 0 (27 s, frame 448) |
| a9324fc0 | 2026-10-06 04:24 | panplay 1.2.2 | v13 | 2506BPN68G | MT6991 | Immortalis-G925 MC12 (PanVK: "Mali-G725 MC12") | 0xd8300015 | 6.6.89 android15 (4k) | 16 | beta.15 | D3D8 ARM64EC cube exit 0 (26 s, frame 450) |
| 3d4bf8f3 | 2026-10-06 04:26 | panplay 1.2.2 | v13 | 2506BPN68G | MT6991 | Immortalis-G925 MC12 (PanVK: "Mali-G725 MC12") | 0xd8300015 | 6.6.89 android15 (4k) | 16 | beta.15 | D3D10 ARM64EC cube exit 0 (27 s, frame 1400, FL11_0) |
| 7e543246 | 2026-10-06 04:32 | panplay 1.2.2 | v13 | 2506BPN68G | MT6991 | Immortalis-G925 MC12 (PanVK: "Mali-G725 MC12") | 0xd8300015 | 6.6.89 android15 (4k) | 16 | beta.15 | D3D8 x86 cube exit 137: stopped by user after 16 s, only frame 0 logged (inconclusive) |
| 1ba0b823 | 2026-10-06 04:35 | panplay 1.2.2 | v13 | 2506BPN68G | MT6991 | Immortalis-G925 MC12 | 0xd8300010 (third-party driver) | 6.6.89 android15 (4k) | 16 | third-party driver | D3D11 ARM64EC cube exit 137 (user stop, 14 s); EXCLUDED |
| d73ee13a | 2026-10-06 04:37 | panplay 1.2.2 | v13 | 2506BPN68G | MT6991 | Immortalis-G925 MC12 (PanVK: "Mali-G725 MC12") | 0xd8300015 | 6.6.89 android15 (4k) | 16 | beta.15 | Core Keeper (Unity, D3D11) exit 137: FL11_0 device, reached Steamworks init, user stop at 233 s |
| 5b799ebb | 2026-10-06 04:54 | panplay 1.2.2 | v10 | V2284A (vivo) | MT6896Z/CZA | Mali-G610 MC6 | 0xa8670000 | 5.10.233 android12 | 15 | beta.15 | User D3D game exit 3 after 4 s: DXVK skips adapter (`textureCompressionBC`) |
| a5fa8644 | 2026-10-06 05:41 | panplay 1.2.2 | v13 | 2506BPN68G | MT6991 | Immortalis-G925 MC12 | 0xd8300010 (third-party driver) | 6.6.118 android15 (4k) | 16 | third-party driver | D3D11 ARM64EC cube exit 0, FL11_1; EXCLUDED |
| 01df95bb | 2026-10-06 05:56 | panprobe 1.2.2 | v11 | LXX525 (LAVA) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.15 | 0/1 (`swapchain_lifecycle` only; FAIL) |
| 652453a6 | 2026-10-06 05:57 | panprobe 1.2.2 | v11 | LXX525 (LAVA) | MT6897 | Mali-G615 MC6 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.15 | 16/17 (fail swapchain_lifecycle) |
| da97c873 | 2026-10-06 06:04 | panprobe 1.2.2 | v12 | 2412DPC0AG | MT6899 | Mali-G720 MC7 | 0xc8700010 | 6.6.89 android15 (4k) | 16 | beta.15 | 14/17 (same 3 fails) |
| f774eb29 | 2026-10-06 06:09 | panprobe 1.2.2 | v12 | 2412DPC0AG | MT6899 | Mali-G720 MC7 | 0xc8700010 | 6.6.89 android15 (4k) | 16 | beta.15 (imported .so) | 14/17 (same 3 fails) |
| d9f8487c | 2026-10-06 06:10 | panplay 1.2.2 | v11 | 24090RA29I (Redmi) | MT6878 | Mali-G615 MC2 | 0xb8a31030 | 6.1.138 android14 | 16 | beta.15 | D3D8 x86 cube exit 0 (30 s, frame 1072) |

## Methodology

- **Data source:** Cloudflare D1 database `panvk-uploads`, table `uploads` (86 rows spanning ids 2 to 90; ids 1, 16, 31 and 56 absent). The first snapshot covered ids 2 to 29; ids 30 to 53 were added in a second pass on 2026-10-05, and ids 54 to 90 (36 rows, uploaded 2026-10-05 18:40 to 2026-10-06 06:11 UTC) in a third pass on 2026-10-06. All Cloudflare access was read-only. Extracted columns include app, version, device_model, soc, gpu_model (GL_RENDERER), gpu_id, arch, driver_version, android_version, game, exit_code, and extra_json(glVersion).
- **Blob storage & verification:** Upload payloads were retrieved from Workers KV (26 blobs under path `blob/<sha256>`) and one external file host (1 blob). No URLs or access keys are recorded. All 27 archive zips were successfully fetched and verified against their SHA-256 digests. Of the 23 newer rows, 13 archives were fetched from Workers KV (12) or the same external file host (1) and all matched their SHA-256 digests. The other 10 could not be retrieved: 9 were stored only on a second external host that cannot be read without creating an account, and 1 has no stored archive. Those 10 records use their D1 metadata only (app, driver, game, exit code). Of the 36 rows in the third pass, 34 archives were fetched (30 from Workers KV, 4 from the first external host) and all matched their SHA-256 digests. The other 2 (`f05a5a17`, `45d6064d`) were stored only on the second external host and use D1 metadata only.
- **Direct tester zips (3 records, 4 files):** Four PanProbe 1.2.2 zips were sent directly by testers. Two are byte-identical (same anon id `c853b7b6`); the same zip was later found in D1 as id 40, so it is counted once. A third (`40359b15`) is the same G77 run re-exported with a different logcat window. The fourth (`e74f908f`) is a v13 Immortalis-G925 run, despite the batch being labelled as G77 files. All four ran the beta.15 bundled driver (same `.so` hash and build ID). For these, `upload_sha256` in the anon id formula is the SHA-256 of the zip file.
- **Anonymous identifier:** Records are identified solely by an 8-character anonymous ID computed as the first 8 hex characters of `sha256("panvk:" + upload_sha256)`. Raw log files remain strictly in gitignored `tmp/universal-logs/`.
- **Extraction & code verification:** Analysis was performed using automated extraction scripts over the retrieved archives. In the first two passes, root-cause analyses were produced via model bridge (codex gpt-6.1-sol) and spot-verified against the driver source tree. In the third pass, log parsing and root-cause checks were done directly against the beta.17 candidate tree.
- **Source tree:** Mesa repository with the beta.16 patch series (base commit `5a07217f` + csf-v11 patches 001–107 + jm-v9 patches 001–003 + 099 version bump). The beta.17 candidate (csf-v11 108–112 and 114–118, android/014, wsi/017, jm-v9 004–005; no 113) applies to a fresh pin of base 169d6a0 as 124 patches with no fuzz (checked 2026-10-06). It is named where it targets an observed failure, but no tester record ran it.

## Data gaps

- **No kbase uAPI version in uploads:** Tester uploads do not log the kernel kbase uAPI version. Kernel uAPI levels are known only from developer runs (G57 tablet = JM uAPI 11.38, measured by the beta.17 candidate; dev G615 = CSF 1.21). beta.17 (110) logs it.
- **TEXTURE_FEATURES only for the G57:** Tester uploads omit the raw `TEXTURE_FEATURES` mask. The beta.17 candidate measured the G57 (`0xf7fe03fe`, BC1-BC3 native). The G610 mask is still unknown.
- **Restricted PanProbe logcat:** Early PanProbe logcat captures were restricted to warning level and minimal byte counts (0 bytes on G720 record `94a3d66c`, 320 bytes on `bd79c8af`). Third-pass PanProbe uploads carry 160-3022 lines, but still no Mesa info-level lines from beta.15 (the driver logged little at INFO before 110/118).
- **No vulkaninfo in PanPlay:** PanPlay upload archives contain launcher and game exit logs but lack `vulkaninfo` diagnostic dumps.
- **One beta.16 tester upload, no beta.17 data:** Only `96a133dc` (Infinix X6857, imported `.so`) ran beta.16. Other beta.16 status comes from the repository CHANGELOG (dev G615 17/17, v9 tablet 1/17). The beta.17 candidate has only a developer run on the G57; the G615 was not connected.
- **v13 and v14 coverage:** v13 has two Immortalis-G925 devices and no data for G625 or plain G725. v14 has one Mali-G1-Ultra device with PanPlay runs only. No v14 PanProbe run exists, and nothing past `vkCreateDevice` has run on v14 (queue-group layout, tiler heaps and shaders are unknown).
- **Unretrievable archives:** Twelve records have no fetchable archive: seven stock 2311DRK48G PanProbe runs, three G1-Ultra PanPlay runs and two v13 PanPlay runs (`f05a5a17`, `45d6064d`).
- **Debug logging in uploads:** Two stock G615 uploads ran with Mesa shader logging on, which made `large_draw` exceed the runner's 60 s limit. Uploads do not record which debug variables were set.
- **Kernel uAPI behind the v14 ioctl failure:** The G1-Ultra kernel rejects `KBASE_IOCTL_MEM_ALLOC_EX` with ENOTTY, and its kbase uAPI version is not logged, so the exact ABI change is unknown.
- **Driver errors hidden in release builds:** `vk_errorf` messages such as `Unknown gpu_id` are dropped unless a debug messenger is attached (`src/vulkan/runtime/vk_log.c:114-119`). Testers cannot see why a device was rejected (G77 case).
- **G77 uAPI and post-enumeration behaviour unknown:** The G77 4.14 kernel accepted the kbase handshake, but its JM uAPI minor version is not logged. Nothing past enumeration ran on it.
