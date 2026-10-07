# Tested devices (all tester data)

Snapshot: 2026-10-06. This file lists every device that has sent PanProbe or PanPlay data: 88 records from 19 devices. The records are 86 D1 upload rows (ids 2 to 90; ids 1, 16, 31 and 56 absent) and 2 direct tester zips. Records are named only by their 8-character anonymous ID (see [README.md](README.md#methodology)). The per-arch details are in the arch docs linked in the last column.

"Best" means the best result on any driver build the device ran. "beta.17" is the unreleased beta.17 candidate (csf-v11 108-118, android/014, wsi/017, jm-v9/004-005). No tester has run it yet.

## Device table

| Anon IDs | Model | SoC | GPU + MC | gpu_id | Arch | Kernel / kbase | Driver release(s) | PanProbe (best) | DXVK / game result | Main blocker (beta.17 patch) | Arch doc |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `5ec9f0c1`, `dae37485`, `80b7a0c9`, `079357e1`, `c4d22728`, `94f4d2bb`, `70ce9223`, `fad0cb63`, `ef4f82a2`, `6ff8855c`, `7b576aa7`, `e0944b04`, `d3de3763`, `f907b333` (excluded), `49abcff8` | Lenovo TB336FU (tablet) | MT8755 | Mali-G57 MC2 | `0x90930010` | v9 | 5.15 android13 / JM uAPI 11.38 | beta.13, beta.14, beta.15 (beta.16 and the beta.17 candidate run by the developer, not uploaded) | 1/17 (beta.14 to beta.16); beta.17 candidate 3/17, of which 2 are real passes | D3D9 cubes exit 3 (`Device does not support Vulkan 1.3`); D3D8 cubes exit 5 | Vulkan 1.1 reporting, no JM compute pre-raster, replay fault (no beta.17 patch) | [v9-jm](v9-jm/README.md) |
| `c853b7b6`, `40359b15` | POCO 21061110AG | MT6891 | Mali-G77 MC9 | `0x90800011` | v9 | 4.14 custom / JM, uAPI not logged | beta.15 | 0/17 (no physical device) | Not tested | gpu_id missing from the model table (not fixed in beta.17; 118 only logs it) | [v9-jm](v9-jm/README.md) |
| `559830af`, `0b151151` | Xiaomi 23054RA19C | MT6896 | Mali-G610 MC6 | `0xa8670000` | v10 | 5.10 android12 / CSF, uAPI not logged | beta.14 | No run | D3D10/D3D11 cubes exit 1: DXVK skips the adapter (`textureCompressionBC`) | BC emulation decision (108) | [v10](v10/README.md) |
| `854bef5b` | Xiaomi 23090RA98I | MT6886 | Mali-G610 MC4 | `0xa8670000` | v10 | 5.15.180 android13 / CSF, uAPI not logged | beta.15 | 15/17 | Not tested | BC (108), gralloc mapper (android/014 + wsi/017) | [v10](v10/README.md) |
| `5b799ebb` | vivo V2284A | MT6896Z/CZA | Mali-G610 MC6 | `0xa8670000` | v10 | 5.10.233 android12 / CSF, uAPI not logged | beta.15 | No run | User D3D game exit 3: DXVK skips the adapter (`textureCompressionBC`) | BC (108) | [v10](v10/README.md) |
| `32144abe`, `e63009bd`, `71dc96c0`, `f2fc9ca6` | POCO X6 Pro 2311DRK48I (developer device) | MT6897 | Mali-G615 MC6 | `0xb8a31030` | v11 | 6.1 custom / CSF 1.21 | beta.15 (beta.16 per CHANGELOG) | 17/17 | D3D9 cubes exit 0; games run (see PROGRESS) | None open in PanProbe; async submission is the next driver task | [v11](v11/README.md) |
| `ec20d5f3`, `45e92c09`, `1bc8a758`, `ff8db0c8`, `d7801456`, `f0583680`, `36cb2ca0`, `00a5888b`, `34239d24`, `30d35b5c`, `abd5de92`, `75fc84bf`, `426e3e4b`, `2161a553`, `9a5b73cb`, `0b225021`, `22f66bc0`, `9f9a941e`, `97589597`, `4d7b89b8`, `77b612ef`, `44601387`, `cddc1702` | POCO X6 Pro 5G 2311DRK48G (stock ROM) | MT6897 | Mali-G615 MC6 | `0xb8a31030` | v11 | 6.1.138 android14 / CSF, uAPI not logged | beta.14, beta.15 | 16/17 | D3D8, D3D9, D3D10 and D3D11 cubes exit 0 on x86, x64 and ARM64EC; FL11_1 | Gralloc mapper: SELinux denies `mapper/mediatek` (android/014 + wsi/017, unverified on this ROM) | [v11](v11/README.md) |
| `7962f613`, `54cdc0c7`, `96a133dc` | Infinix NOTE 50X 5G X6857 | MT6878 | Mali-G615 MC2 | `0xb8a31030` | v11 | 6.1.157 android14 / CSF, uAPI not logged | beta.13, beta.14, beta.16 (all imported .so) | 16/17 | Not tested | Gralloc mapper (android/014 + wsi/017) | [v11](v11/README.md) |
| `bd79c8af` | Xiaomi 24090RA29G | MT6878 | Mali-G615 MC2 | `0xb8a31030` | v11 | 6.1 android14 / CSF, uAPI not logged | beta.14 | 16/17 | Not tested | Gralloc mapper (android/014 + wsi/017) | [v11](v11/README.md) |
| `9641845b`, `a13ae15f` | Xiaomi 2406APNFAG | MT6897 | Mali-G615 MC6 | `0xb8a31030` | v11 | 6.1.138 android14 / CSF, uAPI not logged | beta.14 | No run | D3D8 cubes exit 0 (x86, x64) | None seen | [v11](v11/README.md) |
| `2fe57299` | Infinix GT 30 X6876 | MT6878 | Mali-G615 MC2 | `0xb8a31030` | v11 | 6.1.157 android14 / CSF, uAPI not logged | Pre-beta.13 imported .so | 0/1 (`swapchain_lifecycle` only) | Not tested | Gralloc mapper (android/014 + wsi/017); old driver | [v11](v11/README.md) |
| `01df95bb`, `652453a6` | LAVA LXX525 | MT6897 | Mali-G615 MC6 | `0xb8a31030` | v11 | 6.1.138 android14 / CSF, uAPI not logged | beta.15 | 16/17 | Not tested | Gralloc mapper (android/014 + wsi/017) | [v11](v11/README.md) |
| `d9f8487c` | Redmi Note 14 Pro 5G 24090RA29I | MT6878 | Mali-G615 MC2 | `0xb8a31030` | v11 | 6.1.138 android14 / CSF, uAPI not logged | beta.15 | No run | D3D8 x86 cube exit 0 | None seen | [v11](v11/README.md) |
| `94a3d66c`, `47011dc2`, `96d408f3`, `8400732a`, `da97c873`, `f774eb29` | POCO X7 Pro 2412DPC0AG | MT6899 | Mali-G720 MC7 | `0xc8700010` | v12 | 6.6.118 and 6.6.89 android15 (4 KiB pages) / CSF, uAPI not logged | beta.14, beta.15 | 14/17 | Not tested | v12+ viewport depth runs, `depthBounds`, `shaderOutputViewportIndex` (109) | [v12](v12/README.md) |
| `e74f908f` | Redmi 25060RK16C | MT6991 | Immortalis-G925 MC12 (PanVK: "Mali-G725 MC12") | `0xd8300015` | v13 | 6.6 android15 (4 KiB pages) / CSF, uAPI not logged | beta.15 | 14/17 | Not tested | Same three v12+ gates (109) | [v13](v13/README.md) |
| `feffc979`, `8a0edf91`, `3ca3549c`, `f05a5a17`, `9376b55c`, `45d6064d`, `58cc8090`, `a9324fc0`, `3d4bf8f3`, `7e543246`, `d73ee13a`, `1ba0b823` and `a5fa8644` (third-party driver) | Xiaomi 15T Pro 2506BPN68G | MT6991 | Immortalis-G925 MC12 (PanVK: "Mali-G725 MC12") | `0xd8300015` | v13 | 6.6.89 android15 (4 KiB pages) / CSF, uAPI not logged | beta.15 | 14/17 | D3D8, D3D9, D3D10 and D3D11 cubes exit 0; FL11_0. Core Keeper creates its D3D11 device and reaches Steamworks init | Three v12+ gates (109); FL11_0 because VPSA is off on v13+ (open question) | [v13](v13/README.md) |
| `8fd54bb9`, `d321b12a`, `703077ca`, `eb5ccca3`, `0a6b82e7`, `9aca5216`, `fa35ee7e`, `0cc289c7` | vivo V2515 | MT6993 | Mali-G1-Ultra MC12 | `0xe8800010` | v14 | 6.12.58 android16 (4 KiB pages) / CSF, uAPI not logged | beta.15 | No run | All 8 cubes exit 1: `vkCreateDevice` fails | `MEM_ALLOC_EX` ENOTTY (110, untested on v14) | [v14](v14/README.md) |
| `35d84e6a` | NLA-LX2 | MT6769V/CB | Mali-G52 MC2 | `0x74021000` | v7 (not a PanVK target) | 6.6 android15 (4 KiB pages) / vendor driver | Vendor system driver (r49p1) | 5/17 on the vendor driver | Not tested | PanVK not involved | None |
| `29cd37fb` | Xiaomi 25053PC47G | SM8735 | Adreno 825 | - | Not Mali | 6.6 android15 / Qualcomm kgsl | Qualcomm system driver | 0/1 | Not tested | PanVK not involved | None |

Totals by arch: v9 17 records (2 devices), v10 4 (3), v11 37 (8), v12 6 (1), v13 14 (2), v14 8 (1), v7 1 (1), non-Mali 1 (1).

## Per-device notes

- **Lenovo TB336FU (G57, v9):** The beta.17 candidate smoke on 2026-10-06 is not a tester upload. The developer ran it on this tablet. It scored 3/17 (`vertex_stores`, `vmr_secondary`, `swapchain_lifecycle` at 90.9 FPS). `vmr_secondary` only skipped: the log says `SKIP no 4x sample shading or VMR without attachments` with `vmr=0` on v9, and the runner reports a skip as PASS. So the real score is 2/17. `swapchain_lifecycle` passes through the new `cpu-linear-probe` path (9/9 LINEAR), because the vendor mapper still does not load in the app namespace. The first measured `TEXTURE_FEATURES[0]` is `0xf7fe03fe`: BC1 to BC3 are native and BC4 to BC7 are not. This confirms the partial-native inference behind cross-arch blocker 1. With 108, BC formats are exposed and `bc_decode` raw and copy decode pass for all 16 formats. Blit fails for all 16 (`raw=PASS copy=PASS blit=FAIL`). The JM replay fault in `gpu_prerast_slice` (atoms 35/36, event `0x58`) is unchanged.
- **POCO 21061110AG (G77, v9):** beta.17 still has no `PAN_PROD_ID(9, 0, 0)` row (`src/panfrost/model/pan_model.c:87-91` in the beta.17 candidate tree). With 118, a beta.17 run would at least print `panvk: Unknown gpu_id (0x90800011) ...` to logcat (not run on the G77 yet), but the device would still enumerate 0 GPUs.
- **Xiaomi 23054RA19C and vivo V2284A (G610 MC6, v10, 5.10):** Two different devices, both on 5.10 android12 kernels. Both stop at DXVK adapter selection, because `textureCompressionBC` is missing. The V2284A log has no `EXEC_INIT` warning, but `vkCreateDevice` was never reached, so the 5.10 queue-group, heap and EXEC paths are still untested.
- **Xiaomi 23090RA98I (G610 MC4, v10, 5.15):** The only v10 PanProbe run so far. Device creation, queues, tiler heaps and shaders work.
- **POCO X6 Pro 2311DRK48I (G615, v11):** The developer reference device (custom kernel and ROM). Release gates are measured here. It is not connected for the beta.17 candidate, so beta.17 has no G615 hardware result.
- **POCO X6 Pro 5G 2311DRK48G (G615, v11, stock):** The stock ROM blocks the `mapper/mediatek` lookup with SELinux, so `swapchain_lifecycle` fails (16/17). PanPlay cubes pass for every D3D version and CPU target (about 1130 to 1330 frames in 27 s). Two runs (`2161a553`, `0b225021`) hit the PanProbe 60 s `large_draw` timeout. Both ran with Mesa CS and shader tracing on (110-116 MB logs), and every `large_draw` case up to `tess_direct` had passed. This is the app's time limit under tracing, not a driver fault.
- **Infinix X6857 (G615 MC2, v11):** `96a133dc` is the first beta.16 tester upload (imported `.so`, driverInfo `PanVK-kbase beta.16`). It scores 16/17, again failing only `swapchain_lifecycle`.
- **Infinix X6876 (G615 MC2, v11):** Ran an old imported `.so` (driverInfo `Mesa 26.3.0-devel (git-5a07217f03)`, before beta.13). Only `swapchain_lifecycle` ran, and it failed the same way.
- **LAVA LXX525 and Redmi Note 14 Pro 5G 24090RA29I (G615, v11):** New stock devices. LXX525 gives 16/17 (mapper). 24090RA29I runs the D3D8 x86 cube to exit 0.
- **POCO X7 Pro 2412DPC0AG (G720, v12):** Six runs, all 14/17 with the same three failures. Two kernel builds were seen (6.6.118 and 6.6.89). This may be one device after an update, or two units. The uploads cannot tell them apart.
- **Xiaomi 15T Pro 2506BPN68G (G925, v13):** First v13 DXVK data. D3D10 and D3D11 cubes reach about 1360-1400 frames in 27 s. D3D8 and D3D9 cubes reach only about 420-450 frames in the same time. The G615 has no such gap. The cause is unknown. `7e543246` (D3D8 x86) was stopped by the user after 16 s and is inconclusive. `f05a5a17` and `45d6064d` have D1 metadata only (exit 0). `1ba0b823` and `a5fa8644` used a third-party driver ("PanVK-Mali-G925 0.13.0-dev-8gb-allocation"), not this project's build. That driver exposes `vertexPipelineStoresAndAtomics` and gets FL11_1, and it reports deviceID `0xd8300010`. That value differs from `0xd8300015` only in the low revision bits, so it is not a new GPU. One third-party run shows kernel 6.6.118.
- **vivo V2515 (G1-Ultra, v14):** No new data. Patch 110 adds the `MEM_ALLOC_EX` to `MEM_ALLOC` fallback and logs the kbase uAPI version. A rerun with beta.17 is the next step.
- **Issue-only devices (no upload):** Pixel 8 (Mali-G715 MC7, issue #7), Pixel 8 Pro (Mali-G715, issue #6), Poco X8 Pro (issue #4, vendor driver loaded instead of PanVK), a G610 in a Winlator fork (issue #5) and the GTA IV report (issue #2, device unknown). See [README.md](README.md#github-issues-cross-check).

## Devices still wanted

| Wanted | Why |
|---|---|
| POCO 21061110AG (G77) rerun | Only after a `PAN_PROD_ID(9, 0, 0)` model row lands. beta.17 does not add one. |
| G610 MC6 on a 5.10 kernel, PanProbe with beta.17 | Shows the 5.10 queue-group and heap layouts (110 logs them) and whether 108 clears DXVK adapter selection |
| G610 MC4 PanPlay cube with beta.17 | First DXVK device creation on v10 |
| Stock G615 (2311DRK48G, LXX525, X6857) PanProbe with beta.17 | Checks the `cpu-linear-probe` swapchain fallback on a vendor API 34 ROM where the mapper is SELinux-denied |
| G720 and G925 PanProbe with beta.17 | Checks 109 (`gs_viewport_depth`, `vs_viewport_index`, `depth_bounds`) |
| V2515 (G1-Ultra) PanProbe or PanPlay with beta.17 | Checks the 110 fallback and logs the v14 kbase uAPI version |
| Mali-G710, G510, G310 (v10) | gpu_id and kernel data; never seen |
| Mali-G620, Immortalis-G720 (v12) | Never seen |
| Mali-G68, G78 (v9) | Never seen; real gpu_ids unknown (a `(9, 2, 4)` "G68" row exists) |
| Mali-G625, plain Mali-G725 (v13) | Never seen; G625 has no model row |
| Mali-G1-Premium, G1-Pro (v14) | Never seen |
| Pixel 8 / 8 Pro (G715) PanProbe zip | Issues #6 and #7 have no upload |

Other missing GPU IDs are tracked in the "Model table status" section of [README.md](README.md#model-table-status).
