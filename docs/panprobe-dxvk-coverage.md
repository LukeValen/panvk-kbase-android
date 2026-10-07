# PanProbe vs DXVK requirements: coverage review

Date: 2026-10-06. Reference: DXVK 3.1.1 (`/var/tmp/panvk/components-build/dxvk-src`, `meson.build:1`).
PanProbe = `apps/panvk-test`. Paths below use `M` = `apps/panvk-test/app/src/main/java/dev/zenithblue/panvktest/MainActivity.kt`, `C` = `apps/panvk-test/app/src/main/cpp`, `X` = DXVK `src/dxvk/dxvk_device_info.cpp`.

## Summary

- PanProbe runs 17 GPU tests (`M:95-111`, built by `C/CMakeLists.txt:28-43` plus `swapchain.c`) and one report-only capability dump (`vkinfo.c`).
- 16 of 17 tests validate GPU output (pixels or buffer contents). `swapchain_lifecycle` only checks that the API calls, fences and presents succeed.
- Of 56 hard DXVK device requirements, vkinfo reports 54 unconditionally (96%) and all 56 when the driver also advertises the `VK_KHR_robustness2` alias. That is reporting only: nothing in PanProbe gates on the DXVK list.
- 14 of 56 hard requirements (25%) are exercised by an execution test. All the DXVK binding-model requirements (Vulkan 1.2 descriptor indexing, BDA, scalar layout, 8/16/64-bit types) and the 1.3 shader features are query-only.
- Top 3 gaps: robustness2 (null descriptors and out-of-bounds access), blending (dualSrcBlend, independentBlend, logicOp), and the descriptor-indexing/BDA binding model that every DXVK shader uses.

## 1. PanProbe check inventory

Categories: **GPU** = renders or computes and validates output; **OK-only** = execution must succeed, output not inspected; **F/E/L/Q** = feature, extension, limit and format queries the test uses to gate or skip.

| # | Test (`M` line) | Category | What it verifies | Source |
|---|---|---|---|---|
| 1 | gpu_prerast_slice (95) | GPU | Direct, indexed, instanced, indirect, restart, replay and simultaneous-use draws. Checks one centre pixel per case. | `tests/dxvk/vulkan/gpu_prerast_slice.c:153,367,485,595` |
| 2 | clip_cull (96) | GPU+F+L | shaderClipDistance and shaderCullDistance with max clip/cull ≥ 8; clip/cull arrays and FS clip read, on triangles and lines | `tests/dxvk/vulkan/clip-cull/clip_cull.c:78,103,132` |
| 3 | multi_viewport (97) | GPU+F+L | multiViewport with maxViewports ≥ 16; static 16 and dynamic 4 viewports/scissors, partial updates | `tests/dxvk/vulkan/multi-viewport/multi_viewport.c:61,98,126` |
| 4 | fill_mode (98) | GPU+F | fillModeNonSolid line/point modes, culling, negative-height viewport, strips and fans; exact RGBA against reference | `tests/dxvk/vulkan/fill-mode/fill_mode.c:198,256,266` |
| 5 | bc_decode (99) | GPU+F+Q | textureCompressionBC, all 16 BC1-7 formats; format features (sampled, linear filter, blit, transfer); upload/decode/blit consistency. No golden CPU decoder. | `tests/dxvk/vulkan/bc/bc_decode.c:38,250,294,508,555` |
| 6 | geometry (100) | GPU+F | geometryShader: expansion, invocations, PrimitiveID, adjacency, Layer, all draw kinds | `tests/dxvk/vulkan/geometry/geometry.c:309,419,435` |
| 7 | tessellation (101) | GPU+F+E | tessellationShader with all domains, spacing and winding; optional GS, point size, pipeline stats, EDS2 patch control points | `tests/dxvk/vulkan/tessellation/tessellation.c:366,409,452,1113` |
| 8 | xfb (102) | GPU+E+F+L | VK_EXT_transform_feedback: captured data, streams, pause/resume, overflow, byte-count draws, queries; optional conditional rendering | `tests/dxvk/vulkan/xfb/xfb.c:293,1546,1566,1654` |
| 9 | pipeline_stats (103) | GPU+F | pipelineStatisticsQuery: IA, VS, GS, FS, CS and clipping counters, availability, GPU copies | `tests/dxvk/vulkan/pipeline_stats/pipeline_stats.c:26,75,221,273,294` |
| 10 | vertex_stores (104) | GPU+F | vertexPipelineStoresAndAtomics: VS SSBO stores and atomics, firstVertex/firstInstance, guard bytes | `tests/dxvk/vulkan/vertex-stores/vertex_stores.c:60,205,418,448` |
| 11 | gs_viewport_depth (105) | GPU+E+F+Q | GS ViewportIndex, depthClamp, optional depthClipEnable, D32_SFLOAT depth readback | `device/gs-viewport-depth.c:393,447,495,521,549` |
| 12 | vs_viewport_index (106) | GPU+E+F+Q | VS/TES ViewportIndex (Vulkan12.shaderOutputViewportIndex), depthClamp, optional depthClipEnable | `device/vs-viewport-index.c:638,695,743,763,810` |
| 13 | depth_bounds (107) | GPU+F+L+Q | depthBounds static and dynamic, EarlyFragmentTests, optional 4x MSAA, plus a timing check | `device/depth-bounds.c:408,527,574,584,728,762` |
| 14 | large_draw (108) | GPU+E+F | Large direct, indexed, indirect and count draws via XFB records; optional multiDrawIndirect, drawIndirectCount, primitives-generated query, conditional rendering | `tests/dxvk/vulkan/large-draw/large_draw.c:87,131,159,504,531` |
| 15 | vmr_secondary (109) | GPU+F+L | sampleRateShading, fragmentStoresAndAtomics, variableMultisampleRate, dynamicRendering with no attachments, secondaries, suspend/resume | `tests/dxvk/vulkan/vmr-secondary/vmr_secondary.c:48,197,227,275` |
| 16 | tess_cond_state (110) | GPU+F | Conditional rendering around tessellation draws, then state restore | `tests/dxvk/vulkan/tess-cond-state/tess_cond_state.c:27,93,128,154` |
| 17 | swapchain_lifecycle (111) | OK-only+Q+L | VK_KHR_swapchain on the Android surface: 300 presents, recreate, 10 create/destroy cycles. No pixel check. | `C/swapchain.c:419,496,633,745,809,1146` |
| - | vkinfo (report) | E+F+L+Q | All instance and device extensions; all core, 1.1, 1.2, 1.3 and 1.4 features; 264 extension feature structs keyed by extension name; 106 limits; sparse properties; memory; queues; 298 formats via `vkGetPhysicalDeviceFormatProperties` | `C/vkinfo.c:98,239,289,298-338,350,355,401`; `C/vkinfo_gen.h:3074,5825,5959` |
| - | InfoUi "core requirements" | F | Vulkan core-profile labels (9 base, +16 for 1.3, +40 for 1.4). This is not the DXVK list. | `InfoUi.kt:950,1025,1132` |

Caveats:

- PASS comes from the exit status and log markers (`M:707,721`). A whole-test skip used to show as PASS: on the G57, `vmr_secondary` skipped and still reported PASS (`worklogs/g615-dxvk/PROGRESS.md:116`). The unreleased beta.17 branch adds SKIP status.
- vkinfo matches extension feature structs by name, and its table uses the KHR alias names `VK_KHR_robustness2`, `VK_KHR_line_rasterization` and `VK_KHR_vertex_attribute_divisor` (`vkinfo_gen.h:4499,4739`; `vkinfo.c:315`). DXVK queries the EXT names (`src/dxvk/dxvk_device_info.h:152,158`). On a driver that advertises only the EXT name, nullDescriptor and robustBufferAccess2 are never printed.
- vkinfo does not query `VkFormatProperties3`. The `STORAGE_READ/WRITE_WITHOUT_FORMAT` bits are therefore invisible, and DXVK uses them for the FL 12_0 typed-UAV-load gate.

## 2. DXVK 3.1.1 requirement list

**Hard requirements.** The adapter is rejected if any of these is missing (`X:747-769`, `ENABLE_FEATURE(..., true)` at `X:826-1062`):

- **API and device:** Vulkan 1.3 (`dxvk_instance.h:12`, `X:747`); a graphics+compute queue (`X:753`); maxPushConstantsSize ≥ 256 unless descriptor_heap is present (`X:767`).
- **Extensions:** VK_KHR_swapchain, VK_KHR_load_store_op_none, VK_KHR_maintenance5, VK_KHR_maintenance6, VK_EXT_depth_clip_enable (depthClipEnable), and VK_EXT_robustness2 (robustBufferAccess2 and nullDescriptor).
- **Core 1.0 (22):** depthBiasClamp, depthClamp, dualSrcBlend, fillModeNonSolid, fragmentStoresAndAtomics, fullDrawIndexUint32, geometryShader, imageCubeArray, independentBlend, multiDrawIndirect, multiViewport, occlusionQueryPrecise, robustBufferAccess, sampleRateShading, samplerAnisotropy, shaderClipDistance, shaderCullDistance, shaderImageGatherExtended, shaderInt16, shaderInt64, shaderSampledImageArrayDynamicIndexing, textureCompressionBC.
- **Vulkan 1.1 (2):** shaderDrawParameters, storageBuffer16BitAccess.
- **Vulkan 1.2 (14):** bufferDeviceAddress, descriptorIndexing, storageBuffer8BitAccess, descriptorBindingSampledImageUpdateAfterBind, descriptorBindingUpdateUnusedWhilePending, descriptorBindingPartiallyBound, hostQueryReset, runtimeDescriptorArray, samplerMirrorClampToEdge, scalarBlockLayout, shaderInt8, timelineSemaphore, uniformBufferStandardLayout, vulkanMemoryModel.
- **Vulkan 1.3 (8):** inlineUniformBlock, computeFullSubgroups, dynamicRendering, maintenance4, shaderDemoteToHelperInvocation, shaderZeroInitializeWorkgroupMemory, subgroupSizeControl, synchronization2.

**D3D11 feature-level gates** (`src/d3d11/d3d11_features.cpp:186-215`):

| Level | Requires |
|---|---|
| 11_0 | drawIndirectFirstInstance, fragmentStoresAndAtomics, multiDrawIndirect, tessellationShader |
| 11_1 | logicOp, vertexPipelineStoresAndAtomics |
| 12_0 | Tiled resources tier 2: sparseBinding, sparseResidencyBuffer, sparseResidencyImage2D, sparseResidencyAliased, shaderResourceResidency, shaderResourceMinLod, samplerFilterMinmax, filterMinmaxSingleComponentFormats, and the standard2DBlockShape, nonResidentStrict and !alignedMipSize properties. Also typed UAV load on 18 formats (`STORAGE_READ_WITHOUT_FORMAT`). |
| 12_1 | VK_EXT_conservative_rasterization, fragmentShaderPixelInterlock |

**Optional, used when present** (`X:827-1091`): depthBounds, pipelineStatisticsQuery, tessellationShader, variableMultisampleRate, vertexPipelineStoresAndAtomics, wideLines, largePoints, shaderFloat64, sparse*, drawIndirectCount, shaderOutputViewportIndex/Layer, samplerFilterMinmax, shaderFloat16, robustImageAccess, VK_EXT_transform_feedback, VK_EXT_vertex_attribute_divisor (spec ≥ 3), VK_EXT_custom_border_color, VK_EXT_border_color_swizzle, VK_EXT_extended_dynamic_state3, VK_EXT_graphics_pipeline_library + VK_KHR_pipeline_library, VK_EXT_line_rasterization, VK_EXT_depth_bias_control, VK_EXT_non_seamless_cube_map, VK_EXT_memory_priority/budget, VK_EXT_multi_draw, VK_EXT_attachment_feedback_loop_layout, VK_EXT_shader_stencil_export, VK_EXT_sample_locations, VK_KHR_maintenance7-11, present_id/wait, swapchain_maintenance1, and others.

**Formats:**

- D24_UNORM_S8_UINT, falling back to D32_SFLOAT_S8_UINT (`src/dxgi/dxgi_format.cpp:864`).
- D16_UNORM_S8_UINT for D3D9 (`src/d3d9/d3d9_format.cpp:505`).
- A8_UNORM, falling back to R8 with a swizzle (`src/dxgi/dxgi_format.cpp:874`).
- BC1-7 (hard requirement through textureCompressionBC).
- The 18 typed-UAV formats (`src/d3d11/d3d11_features.cpp:353`).

## 3. Gap matrix

Legend: **EXEC** = exercised and validated by a PanProbe test; **QUERY** = only reported by vkinfo; **QUERY\*** = reported only if the KHR alias is advertised; **NONE** = not covered.

### Hard requirements (56)

| Requirement | Coverage | Where / note |
|---|---|---|
| Vulkan 1.3 apiVersion | QUERY | vkinfo, InfoUi 1.3 labels |
| Graphics+compute queue | EXEC | all tests; pipeline_stats dispatches compute |
| maxPushConstantsSize ≥ 256 | QUERY | limits |
| VK_KHR_swapchain | EXEC (no pixel check) | swapchain_lifecycle |
| VK_KHR_load_store_op_none | QUERY | |
| VK_KHR_maintenance5 | QUERY | |
| VK_KHR_maintenance6 | QUERY | |
| VK_EXT_depth_clip_enable / depthClipEnable | EXEC (optional path) | gs_viewport_depth, vs_viewport_index |
| robustness2.robustBufferAccess2 | QUERY\* | no OOB test |
| robustness2.nullDescriptor | QUERY\* | no null-descriptor test |
| depthBiasClamp | QUERY | |
| depthClamp | EXEC | gs_viewport_depth, vs_viewport_index |
| dualSrcBlend | QUERY | |
| fillModeNonSolid | EXEC | fill_mode |
| fragmentStoresAndAtomics | EXEC | vmr_secondary |
| fullDrawIndexUint32 | QUERY | no index > 2^24 test |
| geometryShader | EXEC | geometry, gs_viewport_depth |
| imageCubeArray | QUERY | |
| independentBlend | QUERY | |
| multiDrawIndirect | EXEC | large_draw (optional path) |
| multiViewport | EXEC | multi_viewport |
| occlusionQueryPrecise | QUERY | no occlusion query test |
| robustBufferAccess | QUERY | |
| sampleRateShading | EXEC | vmr_secondary |
| samplerAnisotropy | QUERY | |
| shaderClipDistance | EXEC | clip_cull |
| shaderCullDistance | EXEC | clip_cull |
| shaderImageGatherExtended | QUERY | |
| shaderInt16 | QUERY | |
| shaderInt64 | QUERY | |
| shaderSampledImageArrayDynamicIndexing | QUERY | |
| textureCompressionBC | EXEC | bc_decode |
| shaderDrawParameters | QUERY | |
| storageBuffer16BitAccess | QUERY | |
| bufferDeviceAddress | QUERY | |
| descriptorIndexing (+ 3 UAB/partially-bound bits, runtimeDescriptorArray) | QUERY | 5 rows |
| storageBuffer8BitAccess | QUERY | |
| hostQueryReset | QUERY | |
| samplerMirrorClampToEdge | QUERY | |
| scalarBlockLayout | QUERY | |
| shaderInt8 | QUERY | |
| timelineSemaphore | QUERY | |
| uniformBufferStandardLayout | QUERY | |
| vulkanMemoryModel | QUERY | |
| inlineUniformBlock | QUERY | |
| computeFullSubgroups | QUERY | |
| dynamicRendering | EXEC | vmr_secondary |
| maintenance4 | QUERY | |
| shaderDemoteToHelperInvocation | QUERY | |
| shaderZeroInitializeWorkgroupMemory | QUERY | |
| subgroupSizeControl | QUERY | |
| synchronization2 | QUERY | `device/csf-event-regression.c` uses it but is not wired |

Totals: 56 hard requirements. 54 are QUERY or better unconditionally (96%), or 56 of 56 with the KHR alias. 14 are EXEC (25%).

### Feature-level gates and important optional features

| Requirement | Coverage | Where / note |
|---|---|---|
| drawIndirectFirstInstance (FL11_0) | QUERY | indirect draws exist but firstInstance ≠ 0 through indirect is not asserted |
| tessellationShader (FL11_0) | EXEC | tessellation, tess_cond_state |
| logicOp (FL11_1) | QUERY | |
| vertexPipelineStoresAndAtomics (FL11_1) | EXEC | vertex_stores |
| Sparse / tiled-resources tier 2 (FL12_0) | QUERY (partial) | filterMinmaxSingleComponentFormats is not printed (no Vulkan 1.2 property chain) |
| Typed UAV load formats (FL12_0) | NONE | needs `VkFormatProperties3` |
| Conservative raster / pixel interlock (FL12_1) | QUERY | |
| VK_EXT_transform_feedback | EXEC | xfb, large_draw |
| pipelineStatisticsQuery | EXEC | pipeline_stats |
| depthBounds | EXEC | depth_bounds |
| variableMultisampleRate | EXEC | vmr_secondary |
| drawIndirectCount | EXEC (optional) | large_draw |
| shaderOutputViewportIndex | EXEC | vs_viewport_index |
| shaderOutputLayer | QUERY | |
| EDS2 patchControlPoints | EXEC (optional) | tessellation |
| VK_EXT_vertex_attribute_divisor | QUERY\* | |
| VK_EXT_line_rasterization | QUERY\* | |
| VK_EXT_custom_border_color, border_color_swizzle | QUERY | |
| VK_EXT_extended_dynamic_state3 | QUERY | |
| VK_EXT_graphics_pipeline_library + KHR_pipeline_library | QUERY | |
| VK_EXT_depth_bias_control | QUERY | |
| VK_EXT_non_seamless_cube_map | QUERY | |
| VK_EXT_memory_priority / memory_budget | QUERY (extension listed, budget not read) | |
| VK_EXT_shader_stencil_export, sample_locations | QUERY | |
| wideLines, largePoints, shaderFloat64 | QUERY | |

### Formats

| Format | Coverage | Note |
|---|---|---|
| BC1-7 | EXEC | bc_decode |
| D32_SFLOAT | EXEC | depth tests |
| D24_UNORM_S8_UINT, D32_SFLOAT_S8_UINT, D16_UNORM_S8_UINT | QUERY | no stencil test at all |
| A8_UNORM | QUERY | |
| 18 typed-UAV formats (storage read without format) | NONE | |
| R8G8B8A8_UNORM / SRGB render and present | EXEC | |

### Standalone tests in the repo that PanProbe does not run

| File | What it tests | DXVK relevance |
|---|---|---|
| `device/gs-tess-primitive-id.c` | Tess-fed GS PrimitiveIDIn = patch ID (gate 088) | D3D11 hull/domain + GS games |
| `device/tess-conditional-regression.c` | Tessellation inside conditional rendering, with counts (gate 087) | Overlaps tess_cond_state, but with stricter counting |
| `device/csf-event-regression.c` | synchronization2 events, host and device paths (gate 085) | Hard requirement synchronization2 |
| `tests/dxvk/vulkan/submit_stress/submit_stress.c` | Long submit stress, tiler-heap exhaustion hang | DEVICE_LOST in long game sessions |
| `tests/dxvk/vulkan/backport-ext/icc.c` | image_compression_control and AFBC images, texel readback | WSI and render target compression |
| `tests/dxvk/vulkan/backport-ext/wsi_ext.c` | incremental_present, swapchain colorspace | Optional DXVK WSI extensions |
| `tests/dxvk/native/common/dxvk_native_probe.cpp` | Real DXVK Native: D3D11 FL 11_1/11_0/10_1 creation and D3D9 draw with readback | End-to-end proof. glibc/SDL only, so it is not APK-portable as is. |
| `tests/dxvk/profile/test_dxvk_3_1_1_profiles.py` | Host-side check of a DXVK 3.1.1 requirement profile | Could become the source of a PanProbe "DXVK gate" |

## 4. Missing tests to add to PanProbe (priority order)

1. **robustness2 test.** Covers nullDescriptor (null UBO, SSBO, image and vertex buffer reads return 0) and robustBufferAccess2 OOB loads and stores. These are hard requirements and implement D3D's out-of-bounds rules; a failure means GPU faults or garbage in games. Also fix vkinfo to look up the EXT alias names.
2. **Blend test.** Covers dualSrcBlend, independentBlend across MRTs, and logicOp. The first two are hard requirements and logicOp gates FL 11_1. Currently untested.
3. **Descriptor model compute test.** Covers BDA, runtime descriptor arrays, update-after-bind, partially-bound, inline uniform blocks and scalar layout. Every DXVK 2.x/3.x shader uses this path.
4. **Occlusion query test.** Precise sample counts with vkCmdResetQueryPool and vkResetQueryPool (hostQueryReset). D3D9/11 predicates and occlusion culling rely on it.
5. **Depth/stencil test.** Covers D24S8 (and the D32S8 fallback) with stencil ops and readback, depthBiasClamp and depth_bias_control. There is no stencil coverage at all today.
6. **Sampler test.** Covers anisotropy, mirror-clamp-to-edge, custom border color, cube arrays, non-seamless cubes, and gather with offsets (shaderImageGatherExtended). These are hard requirements plus common D3D sampler states.
7. **Shader-arithmetic compute test.** Covers int8/16/64, 8/16-bit storage, demote, subgroupSizeControl/computeFullSubgroups and zero-init workgroup memory. These are hard 1.1-1.3 features DXVK's compiler emits.
8. **Wire in `csf-event-regression.c` and `gs-tess-primitive-id.c`.** They already exist, are device-validated, and cover synchronization2 (hard requirement) and a known tess+GS correctness gate.
9. **maintenance5/6 + load_store_op_none + vertex_attribute_divisor + shaderDrawParameters draw test.** Covers instanced vertex step rates, BaseVertex/BaseInstance, and LOAD/STORE_OP_NONE attachments. These are hard requirements DXVK uses on every frame.
10. **"DXVK 3.1.1 gate" report plus a FormatProperties3 query.** Check the 56 hard requirements, the FL 11_0-12_1 gates and the typed-UAV formats in vkinfo, and print the D3D11 feature level DXVK would expose. This is cheap and turns the query data into a pass/fail result. Also add a short `submit_stress` run as a stability check.
