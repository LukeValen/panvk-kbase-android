# Mali v13 (5th-gen CSF) status

Older file:line references in this document are relative to the Mesa root of the beta.16 tree (Mesa 5a07217f + csf-v11 series up to 107 + jm-v9 001-003). References marked "beta.17 tree" are relative to the unreleased beta.17 candidate (beta.16 + csf-v11 108-118 + android/014 + wsi/017 + jm-v9 004-005). Both are written as `src/panfrost/...:NNN`.

## Summary
Two Immortalis-G925 MC12 devices (both MT6991) score 14/17 on PanProbe with beta.15. The three failures are the same v12+ gates as on the Mali-G720 (v12): `gs_viewport_depth`, `vs_viewport_index` and `depth_bounds`. Unreleased patch 109 targets all three. The first v13 PanPlay data (2026-10-06, Xiaomi 15T Pro 2506BPN68G) shows D3D8, D3D9, D3D10 and D3D11 cubes running to exit 0 on x86, x64 and ARM64EC. DXVK creates the D3D11 device at FL11_0, not FL11_1, because PanVK keeps `vertexPipelineStoresAndAtomics` off on v13+. The Unity game Core Keeper creates its D3D11 device and reaches Steamworks init with no driver error. Plain Mali-G725 and Mali-G625 remain unseen.

## GPUs and devices tested

| Driver build | Anon ID(s) | Device | SoC | GPU | gpu_id -> Mesa model | Kernel | Android | App |
|---|---|---|---|---|---|---|---|---|
| beta.15 | `e74f908f` | 25060RK16C (Redmi) | MT6991 | Immortalis-G925 MC12 | `0xd8300015` -> G725 | 6.6 android15 (4 KiB pages) | 15 | PanProbe 1.2.2 (direct zip) |
| beta.15 | `feffc979` | 2506BPN68G (Xiaomi 15T Pro) | MT6991 | Immortalis-G925 MC12 | `0xd8300015` -> G725 | 6.6.89 android15 (4 KiB pages) | 16 | PanProbe 1.2.2 |
| beta.15 | `8a0edf91`, `3ca3549c`, `f05a5a17`, `9376b55c`, `45d6064d`, `58cc8090`, `a9324fc0`, `3d4bf8f3`, `7e543246`, `d73ee13a` | 2506BPN68G (Xiaomi 15T Pro) | MT6991 | Immortalis-G925 MC12 | `0xd8300015` -> G725 | 6.6.89 android15 (4 KiB pages) | 16 | PanPlay 1.2.2 |
| Third-party driver (not this project) | `1ba0b823`, `a5fa8644` | 2506BPN68G (Xiaomi 15T Pro) | MT6991 | Immortalis-G925 MC12 | deviceID `0xd8300010` (reported by that driver) | 6.6.89 / 6.6.118 android15 | 16 | PanPlay 1.2.2 (imported driver) |

The hardware identifier `0xd8300015` decodes to architecture 13.8, product 0, revision r0p1. That matches `PAN_PROD_ID(13, 8, 0)` v4 "G725" in `src/panfrost/model/pan_model.c:113`. The row is unchanged in the beta.17 tree (also line 113), so PanVK still names the GPU "Mali-G725 MC12". The vendor GL renderer reports "Mali-G925-Immortalis MC12". Only the name differs.

`1ba0b823` and `a5fa8644` ran an imported third-party driver whose version string is "PanVK-Mali-G925 0.13.0-dev-8gb-allocation". They are listed for completeness but are not results for this driver. That driver reports deviceID `0xd8300010`, which differs from `0xd8300015` only in the low revision bits. It is the same GPU, not a new gpu_id.

## Kernel / kbase interface seen
- **Kernel version:** Linux 6.6 android15 with a `-4k` build suffix (4 KiB pages). `ro.product.cpu.pagesize.max = 16384` only marks a 16 KiB-ready userspace.
- **kbase interface:** CSF. The uAPI version is not logged in the uploads (beta.17 patch 110 adds the log line). Allocation via `KBASE_IOCTL_MEM_ALLOC_EX`, queue groups and tiler heaps all work.
- **Vendor DDK:** GLES r49p1.
- **Vendor API level:** `ro.board.first_api_level = 202404`, `ro.hardware.gralloc = common`.
- **Swapchain / mapper:** The stable-C mapper loads: `[P0A-V19-FULLPLANE] accepted fourcc=0x34324241 modifier=0x0000000000000000 planes=1`, followed by successful dma-buf imports.
- **vulkaninfo:** Vulkan 1.4.363, 188 extensions, `queueCount = 2`, `textureCompressionBC = true`, `depthBounds = false`, `shaderOutputViewportIndex = false`, `shaderDeviceClock = false`.
- **DXVK feature dump (Core Keeper run `d73ee13a`):** `depthBounds : 0`, `shaderOutputViewportIndex : 0`, `vertexPipelineStoresAndAtomics : 0`, `logicOp : 1`.

## Per-test results

### PanProbe test suite (17 tests)

| Test | `e74f908f` | `feffc979` | Notes |
|---|---|---|---|
| `gpu_prerast_slice` | PASS | PASS | |
| `clip_cull` | PASS | PASS | |
| `multi_viewport` | PASS | PASS | |
| `fill_mode` | PASS | PASS | |
| `bc_decode` | PASS | PASS | All BC formats `optimal=0x1d401` via emulation |
| `geometry` | PASS | PASS | |
| `tessellation` | PASS | PASS | |
| `xfb` | PASS | PASS | |
| `pipeline_stats` | PASS | PASS | |
| `vertex_stores` | PASS | PASS | |
| `gs_viewport_depth` | FAIL | FAIL | Same wrong depths as G720 |
| `vs_viewport_index` | FAIL | FAIL | `shaderOutputViewportIndex not reported` |
| `depth_bounds` | FAIL | FAIL | `depthBounds not reported` |
| `large_draw` | PASS | PASS | |
| `vmr_secondary` | PASS | PASS | |
| `tess_cond_state` | PASS | PASS | |
| `swapchain_lifecycle` | PASS | PASS | `FPS 121.3`, `FPS_RESIZED 121.8` on `feffc979` |

### PanPlay runs (beta.15, 2506BPN68G)

"Last frame" is the last `Present frame=N` line logged in the run (about 27 s per cube run).

| Anon ID | Target | Exit | Last frame | Notes |
|---|---|---|---|---|
| `8a0edf91` | D3D9 x64 cube | 0 | 420 | |
| `3ca3549c` | D3D11 ARM64EC cube | 0 | 1360 | `create hr=0x00000000 fl=0xb000` (FL11_0) |
| `f05a5a17` | D3D11 x86 cube | 0 | - | Archive not retrievable; D1 metadata only |
| `9376b55c` | D3D11 x64 cube | 0 | 1399 | FL11_0 |
| `45d6064d` | D3D8 x86 cube | 0 | - | Archive not retrievable; D1 metadata only |
| `58cc8090` | D3D8 x64 cube | 0 | 448 | |
| `a9324fc0` | D3D8 ARM64EC cube | 0 | 450 | |
| `3d4bf8f3` | D3D10 ARM64EC cube | 0 | 1400 | FL11_0 |
| `7e543246` | D3D8 x86 cube | 137 | 0 | Stopped by the user after 16 s; only frame 0 logged. Inconclusive (`45d6064d` ran the same target to exit 0) |
| `d73ee13a` | Core Keeper (Unity, D3D11) | 137 | - | `Using feature level D3D_FEATURE_LEVEL_11_0`; Unity renderer "Mali-G725 MC12", 8457 MB VRAM; assembly load took 196.7 s; reached Steamworks init; no driver error; stopped by the user after 233 s |
| `1ba0b823` | D3D11 ARM64EC cube | 137 | - | Third-party driver; stopped by the user after 14 s |
| `a5fa8644` | D3D11 ARM64EC cube | 0 | - | Third-party driver; FL11_1 |

## Failures and log excerpts
```
FAIL case A_clamp depthL=[0.375000..0.375000] expL=0.250000 depthR=[0.000000..0.000000] expR=0.500000 badDepth=1024 badColor=0
FAIL case B_clamp_clip depthL=[0.375000..0.375000] expL=0.125000 depthR=[0.250000..0.250000] expR=0.750000 badDepth=1024 badColor=0
FAIL case C_noclamp depthL=[0.375000..0.375000] expL=0.125000 depthR=[0.250000..0.250000] expR=0.750000 badDepth=1024 badColor=0
FAIL shaderOutputViewportIndex not reported
FAIL CreateGraphicsPipelines r=-13 line=550
FAIL depthBounds not reported
```
These lines are identical to the G720 records.

DXVK on v13 (`d73ee13a`, `wine-run.log`):
```
info:  Found device: Mali-G725 MC12 (panvk 26.2.99)
info:    depthBounds                    : 0
info:    shaderOutputViewportIndex      : 0
info:    vertexPipelineStoresAndAtomics : 0
info:  D3D11InternalCreateDevice: Maximum supported feature level: D3D_FEATURE_LEVEL_11_0
```

## Root causes
- **PanProbe failures:** The same three v12+ root causes as on v12. See [v12/README.md](../v12/README.md#root-causes):
  - `depthBounds` is gated to `PAN_ARCH >= 10 && PAN_ARCH < 12` (`src/panfrost/vulkan/panvk_vX_physical_device.c:330`).
  - `shaderOutputViewportIndex` is disabled on v12+ (`panvk_vX_physical_device.c:443`), and viewport-run preparation returns false on v12+ (`src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c:4709-4710`).
  - Per-run depth clamp updates are `PAN_ARCH < 12` only (`csf/panvk_vX_cmd_draw.c:4388-4390`), and the driver programs one union depth range for all viewports (`csf/panvk_vX_cmd_draw.c:1038`).
  - Unreleased patch 109 targets all three. In the beta.17 tree both gates read `PAN_ARCH >= 10` (`panvk_vX_physical_device.c:330`, `:443`), and `prepare_vp` writes each run's depth range into the VIEWPORT_LOW min/max depth words (`csf/panvk_vX_cmd_draw.c:1025-1071`). It needs a v13 rerun.
- **FL11_0 instead of FL11_1:** DXVK 3.1.1 needs `logicOp` and `vertexPipelineStoresAndAtomics` for FL11_1. In the beta.17 tree, VPSA is on for v10-v12 and is opt-in on v13+ through the drirc option `enable_vertex_pipeline_stores_atomics` (`src/panfrost/vulkan/panvk_vX_physical_device.c:345-348`). The code comment gives the reason: v10-v12 run memory-writing pre-raster stages through gpu_prerast compute, while v13+ hardware no longer speculatively references invalid indices. No beta.17 patch changes this. The third-party driver exposes VPSA on the same GPU and gets FL11_1, but that does not prove VPSA is correct through PanVK's v13 IDVS path.
- **D3D8/D3D9 frame rate:** The D3D8 and D3D9 cubes log about 1/3 of the frames of the D3D10 and D3D11 cubes in the same 27 s (420-450 against 1360-1400). On the stock G615 the D3D8 and D3D9 cubes reach 1132-1195 frames and the D3D10 and D3D11 cubes 1282-1332. The cause is not known and has not been traced.

## What the driver lacks on this arch
- Per-viewport depth runs, validated depth-bounds emulation and VS viewport index on v12+ (unreleased patch 109, not yet run on v12 or v13).
- `vertexPipelineStoresAndAtomics` by default on v13+ (drirc opt-in only), which caps DXVK at FL11_0.
- A G925 model name: the `(13, 8, 0)` row is still called "G725" in the beta.17 tree.
- Untested on v13: beta.16 changes (106-107) and every beta.17 patch.
- Untested variants: plain Mali-G725 and Mali-G625 (G625 has no model row).

## Fix plan

| Rank | Item | Effort | Unblocks |
|---|---|---|---|
| 1 | Ship patch 109 and rerun PanProbe on a G925 | Tester rerun | `gs_viewport_depth`, `vs_viewport_index`, `depth_bounds` (17/17) |
| 2 | Decide VPSA on v13+: run `vertex_stores` and the CTS vertex atomics cases on v13 with the drirc option, then enable by default if clean | not estimated; needs a v13 tester run | DXVK FL11_1 on v13 |
| 3 | Find the D3D8/D3D9 frame-rate gap on v13 (DXVK HUD and frame times for the D3D9 cube) | not estimated | D3D8/D3D9 performance on v13 |
| 4 | Rename the `(13, 8, 0)` row or add an Immortalis-G925 alias | < 1 h | Cosmetic device name |

## Open questions / data needed from testers
- **VPSA on v13/v14:** Is `vertexPipelineStoresAndAtomics` safe on v13+ without gpu_prerast? A PanProbe `vertex_stores` run with `enable_vertex_pipeline_stores_atomics` set would answer it.
- **D3D9 slowdown:** A D3D9 cube run with `DXVK_HUD=full` and a beta.17 build.
- **PanProbe with beta.17:** Confirms 109 and logs the kbase uAPI version (110).
- **Other v13 parts:** `gpu_id` values for plain Mali-G725 and Mali-G625.

## Links
- [Universal Mali status](../README.md)
- [Tested devices](../DEVICES.md)
- [v12 status (same root causes)](../v12/README.md)
- [v14 status](../v14/README.md)
- [FL11_1 re-check worklog](../../../worklogs/driver-remaining/fl11_1-recheck.md)
