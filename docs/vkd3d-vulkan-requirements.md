# vkd3d (D3D12 on Vulkan) requirements and PanVK status

Date: 2026-10-06. Device data: Poco X6 Pro, Mali-G615 MC6 (v11, CSF), bundled PanVK (Vulkan 1.4.363), PanProbe run `20261006-201701` (`vkd3d_reqs` checker, `vkd3d_heap`, `vkd3d_timeline`).

Sources (shallow clones):

| Variant | Path | Revision | Role |
|---|---|---|---|
| Wine vkd3d (upstream) | `~/repos/vkd3d-wine`, tag worktree `/var/tmp/panvk/vkd3d-wine-1.18` | HEAD `11551ec` (2026-10-05); analysis pinned to tag `vkd3d-1.18` | **What PanPlay runs today** |
| vkd3d-proton | `~/repos/vkd3d-proton` | `2230755` (2026-10-06) | What Steam Proton and most Windows-on-Android launchers ship for D3D12 |
| ValveSoftware/vkd3d | `~/repos/vkd3d-valve` | `061858e` (2022-07-27, vkd3d 1.4 era) | Stale fork of Wine vkd3d; not used by Proton 11 or PanPlay |

Line references below are `libs/vkd3d/<file>:<line>` in the named tree.

## 1. Which vkd3d PanPlay uses

PanPlay (`apps/panvk-launcher`) bundles three components (`Contents.kt:71-97`): Proton 11.0-2 arm64ec (GameNative proton-wine, tag `proton-11.0-2-20260928`), FEXCore 2609.1 and DXVK 3.1.1.

- The DXVK `.wcp` only has `d3d8/9/10core/11/dxgi.dll`. No D3D12.
- The Proton `.wcp` (`/var/tmp/panvk/components/proton-11.0-2-arm64ec.wcp`, 2246 entries) has **no vkd3d-proton** (`d3d12.dll`/`d3d12core.dll` are Wine builtins, no `libvkd3d-proton*`).
- `lib/wine/aarch64-windows/d3d12.dll` imports `vkd3d_create_instance`, `vkd3d_create_device`, ... from `wined3d.dll`. `wined3d.dll` (6.3 MB) contains the strings `Build: vkd3d 1.18 (Wine bundled).` and `vkd3d-shader 1.18 (Wine bundled)`.

So **D3D12 in PanPlay = Wine's bundled upstream vkd3d 1.18**. The launcher already sets `VKD3D_LOG_FILE` (`Containers.kt:373`) but that only matters for vkd3d-proton. DXVK's `dxgi.dll` replaces Wine's `dxgi.dll` in the prefix; Wine's `d3d12.dll` still imports Wine `dxgi.dll` names, so D3D12 swapchains go through DXVK's DXGI. That pairing is untested (DXVK's DXGI only knows vkd3d-proton's `IDXGIVkSwapChainFactory`), see section 7.

The PanProbe checker therefore uses **Wine vkd3d 1.18 as the HARD list** and reports every vkd3d-proton hard requirement as a `proton:` soft item.

## 2. Wine vkd3d 1.18

### 2.1 Vulkan API version

| Item | Behavior | Source |
|---|---|---|
| Instance apiVersion | 1.0; 1.1 if `vkEnumerateInstanceVersion` reports >= 1.1 | device.c:656-667 |
| Device apiVersion | No minimum check | device.c:2315-2333, 957-968 |
| Shader target | Vulkan 1.1 SPIR-V iff instance is 1.1 | device.c:5603-5604 |

### 2.2 HARD (device creation fails)

| # | Requirement | Source | G615 |
|---|---|---|---|
| H1 | `VK_KHR_maintenance1` advertised (always enabled by name) | device.c:73-78, 398-401 | ok |
| H2 | `VK_KHR_maintenance2` advertised | device.c:73-78 | ok |
| H3 | `VK_KHR_shader_draw_parameters` advertised | device.c:73-78 | ok |
| H4 | One queue family with GRAPHICS and COMPUTE | device.c:2153-2180 | ok |
| H5 | `shaderStorageImageWriteWithoutFormat` (internal UAV-clear pipelines use `StorageImageWriteWithoutFormat`; pipeline creation error aborts init) | device.c:3551-3553, state.c:4319-4333 | ok |
| H6 | Without `robustness2.nullDescriptor`: 16-byte buffers and 1x1 RGBA8 sampled+storage images for null resources | resource.c:4870-4945 | ok (nullDescriptor on) |
| H7 | Requested minimum FL <= computed max FL (games asking FL12_0 get `E_INVALIDARG`) | device.c:1890-1899 | **FL11_1 only** (see 2.4) |

Wine vkd3d 1.18 has no hard Vulkan 1.2/1.3 feature, BDA, timeline, sync2 or dynamic rendering requirement. Missing optional features degrade.

### 2.3 SOFT (fallback or lower caps)

| Item | Gate / effect | Source | G615 |
|---|---|---|---|
| FL11_0 checklist (20 bits): depthBiasClamp, depthClamp, drawIndirectFirstInstance, dualSrcBlend, fragmentStoresAndAtomics, fullDrawIndexUint32, geometryShader, imageCubeArray, independentBlend, multiDrawIndirect, multiViewport, occlusionQueryPrecise, pipelineStatisticsQuery, samplerAnisotropy, sampleRateShading, shaderClipDistance, shaderCullDistance, shaderImageGatherExtended, shaderStorageImageWriteWithoutFormat, tessellationShader | any missing: no FL11_1 promotion (FL11_0 is the floor) | device.c:1394-1433, 1452-1459 | all ok |
| Limit warnings: push constants >= 256, shared memory >= 32768, viewport bounds +-32768, viewportSubPixelBits >= 8, per-stage UBOs >= 15 | warning only | device.c:1388-1412 | **viewportSubPixelBits < 8** |
| FL11_1: logicOp, vertexPipelineStoresAndAtomics, per-stage SSBO >= 64, storage images >= 64 | FL11_0 cap | device.c:1454-1459 | ok |
| FL12_0: tiled tier >= 2 AND binding tier >= 2 AND TypedUAVLoadAdditionalFormats | tiled tier is **forced to 0** (device.c:1790-1795), so 12_0 is never automatic | device.c:1461-1466 | blocked by design |
| FL12_1: ROVs + conservative raster tier >= 1 | conservative raster always 0 | device.c:1468-1471, 1806-1807 | blocked by design |
| FL override | `VKD3D_CAPS_OVERRIDE=feature_level=12.0` (also 12.1/12.2, `resource_binding_tier=`) | device.c:1624-1662 | - |
| Shader model | fixed `min(request, 6_0)` | device.c:3637-3647 | 6.0 |
| Resource binding tier | samplers <= 16: tier 1; UBOs <= 14: tier 2; else 3 | device.c:1797-1802 | tier 3 |
| TypedUAVLoadAdditionalFormats | `shaderStorageImageReadWithoutFormat` + STORAGE_IMAGE on 15 formats (RGBA32/RGBA16 F/U/S, RGBA8 UN/U/S, R16 F/U/S, R8 UN/U/S) | device.c:1528-1554, 1804 | ok |
| Vulkan descriptor heaps | `VK_EXT_descriptor_indexing` + UAB on UBO, sampled image, storage image, uniform/storage texel buffer; else CPU "virtual heaps" | device.c:1954-1960 | ok (Vulkan heaps) |
| Heap sizes | `min(setUAB, perStageUAB - 32, 1000000)` per type; samplers also `<= 2048` | device.c:1489-1518 | 1000000 |
| robustBufferAccessUpdateAfterBind | false + UAB heaps: robustBufferAccess disabled | device.c:1937-1946 | ok |
| `VK_EXT_mutable_descriptor_type` | 1 mutable set instead of 5 typed sets | device.c:1860-1861 | ok |
| `VK_KHR_push_descriptor` | root descriptors | device.c:1962-1981 | ok |
| `VK_EXT_robustness2` nullDescriptor | real null descriptors | device.c:1854-1855 | ok |
| `VK_KHR_timeline_semaphore` | D3D12 fences on timeline semaphores, else VkFence + binary | device.c:1862-1863, command.c:1237 | ok |
| `VK_EXT_transform_feedback` | stream output PSOs return `E_NOTIMPL` without it | state.c:3452-3456 | ok |
| `VK_EXT_conditional_rendering` | `SetPredication` | command.c:5921-5924 | ok |
| `VK_EXT_vertex_attribute_divisor` spec >= 3 | instance step rates (else 1) | device.c:1871-1882 | ok |
| `VK_EXT_fragment_shader_interlock` pixel AND sample | ROVsSupported | device.c:1844-1848 | **missing** |
| `VK_EXT_depth_clip_enable` | DepthClipEnable | device.c:1852-1853 | ok |
| `VK_EXT_shader_stencil_export` | PSSpecifiedStencilRef | device.c:1886 | ok |
| `VK_EXT_shader_viewport_index_layer` (extension name only) | VP/RT array index from VS without GS emulation | device.c:1887-1888 | **missing** (PanVK lacks `shaderOutputViewportIndex`) |
| `VK_EXT_shader_demote_to_helper_invocation`, `VK_KHR_zero_initialize_workgroup_memory`, `VK_EXT_texel_buffer_alignment`, `VK_KHR_draw_indirect_count`, `VK_KHR_sampler_mirror_clamp_to_edge`, `VK_EXT_4444_formats`, `VK_EXT_calibrated_timestamps` | demote, zeroed groupshared, texel alignment, ExecuteIndirect count, MIRROR_ONCE, B4G4R4A4, clock calibration | device.c:1856-1867, 103 | ok |
| depthBounds | DepthBoundsTestSupported; PSOs using it fail | device.c:1824, state.c:3353-3357 | **missing** |
| shaderInt64 / shaderFloat64 | Int64ShaderOps / DoublePrecisionFloatShaderOps | device.c:1776, 1822 | int64 ok, **float64 missing** |
| WaveOps: subgroupSize >= 4, basic/vote/arith/ballot/shuffle/quad in CS+FS | WaveOps cap | device.c:1737-1744 | ok |
| Min precision / native 16-bit | always none in 1.18 | device.c:1778-1779, 1836-1838 | - |
| VRS, conservative raster, tiled resources | always 0 | device.c:3909-3912, 1790-1807 | - |

### 2.4 What G615 gets under Wine vkd3d 1.18

From `compliance-vkd3d_reqs.log`: **FL11_1, SM6.0, ResourceBindingTier 3, Vulkan (update-after-bind) heaps, typed UAV loads 18/18.** Games that call `D3D12CreateDevice(..., D3D_FEATURE_LEVEL_12_0, ...)` fail on every GPU under Wine vkd3d 1.18 unless `VKD3D_CAPS_OVERRIDE=feature_level=12.0` is set. Most D3D12 games need FL12_0+ and SM6.x DXIL features vkd3d-shader does not fully translate. Wine vkd3d is fine for D3D12 test apps and simple titles, not for modern games.

### 2.5 Formats and limits (Wine)

- D24_UNORM_S8_UINT without DS attachment support: remapped to D32_SFLOAT_S8_UINT (utils.c:362-383). G615 has D24S8.
- B8G8R8A8 typed UAV support always removed (device.c:3288-3304).
- 64 KB MSAA alignment probe: RGBA8 4x 1024x1025 (device.c:2241-2260).
- Root signatures: sets <= min(64, maxBoundDescriptorSets) (state.c:774-780); push constants beyond `maxPushConstantsSize` fail at layout creation.

### 2.6 Wine HEAD (after 1.18)

`git diff vkd3d-1.18 HEAD`: adds `VK_KHR_shader_float_controls` and `VK_EXT_sampler_filter_minmax` queries (MinMaxFiltering), per-type heap counts (`max_cbv/srv/uav/sampler_descriptor_count`). No new hard requirement. A future Proton rebase picks this up.

## 3. vkd3d-proton (HEAD 2230755)

### 3.1 API version

`VKD3D_MIN_API_VERSION = VKD3D_MAX_API_VERSION = VK_API_VERSION_1_3` (include/vkd3d.h:53-54). Loader < 1.3: `E_INVALIDARG` (device.c:675); physical devices < 1.3 are skipped (device.c:2745). G615: 1.4, ok.

### 3.2 HARD (explicit `E_INVALIDARG` / `E_NOTIMPL` / `E_FAIL`)

| # | Requirement | Source | G615 |
|---|---|---|---|
| P1 | Vulkan 1.3 instance and device | device.c:675, 2745 | ok |
| P2 | `vertexAttributeInstanceRateDivisor` AND `...ZeroDivisor` (EXT struct, spec >= 3) | device.c:2321, 2495-2500 | ok |
| P3 | `transformFeedbackQueries` property | device.c:2502-2506 | ok |
| P4 | storage AND uniform texel buffer single-texel alignment (or offset alignment 1) | device.c:2508-2520 | ok |
| P5 | `samplerMirrorClampToEdge` | device.c:2633 | ok |
| P6 | `robustBufferAccess2` AND **`robustImageAccess2`** | device.c:2639-2644 | **robustImageAccess2 = false** |
| P7 | `nullDescriptor` | device.c:2646-2650 | ok (v10+) |
| P8 | `shaderDrawParameters` | device.c:2655 | ok |
| P9 | `VK_KHR_push_descriptor` | device.c:2661 | ok |
| P10 | `maintenance5` AND `maintenance6` | device.c:2667-2672 | ok |
| P11 | `descriptorIndexing`, `shaderStorageTexelBufferArrayNonUniformIndexing`, `shaderStorageImageArrayNonUniformIndexing`, `descriptorBindingVariableDescriptorCount` (`E_NOTIMPL`) | state.c:8350-8358 | ok |
| P12 | Without the descriptor-buffer path: `maxPerStageDescriptorUpdateAfterBind{SampledImages,StorageImages,StorageBuffers} >= 1,000,000` | state.c:8201-8212, `VKD3D_MIN_VIEW_DESCRIPTOR_COUNT` | ok (1048576 each) |
| P13 | Graphics+compute queue | device.c:3021, 3098 | ok |
| P14 | Used with no feature check (Vulkan errors at first use): timelineSemaphore (memory-transfer queue, memory.c:197), bufferDeviceAddress (resource.c:7448), synchronization2 (memory.c:368), dynamicRendering (command.c:4498), UAB + partially bound + variable count + update-unused-while-pending layouts (state.c:7512) | - | ok |
| P15 | Requested minimum FL <= reported max FL | device.c:10942 | FL11_1 cap |

`robustBufferAccessUpdateAfterBind = false` only warns (device.c:2619). There is no required-extension table: unsupported names are still enabled, so `vkCreateDevice` errors propagate.

**G615 result: vkd3d-proton device creation fails on exactly one item: `robustImageAccess2`** (PanVK `panvk_vX_physical_device.c:631` sets it false).

### 3.3 Feature level (device.c:9645-9680)

| FL | Condition | G615 |
|---|---|---|
| 11_0 | baseline | - |
| 11_1 | logicOp, vertexPipelineStoresAndAtomics, 64 per-stage SSBOs + storage images | ok |
| 12_0 | 11_1 + tiled tier >= 2 + binding tier >= 2 (always 3) + typed UAV loads on all 18 formats (`STORAGE_READ_WITHOUT_FORMAT`, device.c:9109-9133, 4425-4433) | **tiled tier 0** |
| 12_1 | 12_0 + ROVs (pixel+sample interlock, device.c:9270) + conservative raster tier >= 1 | ROVs missing; conservative tier 1 ok |
| 12_2 | 12_1 + SM >= 6.5 + VS viewport/layer + WaveOps + Int64 + depth bounds + copy-queue timestamps + fully typed casting + binding 3 + conservative 3 + tiled 3 + DXR 1.1 + VRS 2 + mesh 1 + sampler feedback 0.9 | far |

Tiled tier (device.c:8936-8960): none without sparseBinding, sparseResidencyAliased/Buffer/Image2D, standard 2D block shape and a sparse-binding queue; tier 1 without shaderResourceResidency/MinLod, non-resident strict, filterMinmaxSingleComponentFormats or with aligned mip size; tier 2 without Image3D/standard 3D shape; else tier 4. Conservative tier (device.c:8967): 1 without `degenerateTrianglesRasterized`, 2 without `fullyCoveredFragmentShaderInputVariable`, else 3.

`VKD3D_FEATURE_LEVEL=12_0` (device.c:9956-10012) forces typed loads, tiled/binding >= 2 and SM >= 6.0 after device creation; it cannot bypass P1-P14.

### 3.4 Shader model (device.c:9736-9940)

| SM | Needs | G615 |
|---|---|---|
| 5.1 | fallback | - |
| 6.0 | subgroupSize >= 4, basic/vote/arith/ballot/shuffle/quad in CS+FS, scalarBlockLayout or uniformBufferStandardLayout, shaderInt16 | ok |
| 6.2 -> 6.3 -> 6.5 | `denormBehaviorIndependence != NONE` and FP32 flush-to-zero and preserve (non-NVIDIA) | **missing** |
| 6.6 | 6.5 + computeDerivativeGroupLinear + shaderBufferInt64Atomics + shaderInt8 + fixed wave size or COMPUTE in requiredSubgroupSizeStages | bits present on v11, blocked by 6.2 |
| 6.7 -> 6.8 | shaderMaximalReconvergence + shaderQuadControl | present, blocked by 6.2 |
| 6.9 | micromap, maintenance9, long vectors >= 1024, invocation reorder | no |

`VKD3D_SHADER_MODEL=6_6` overrides. G615 gets **SM6.0** today; fixing float-controls denorm reporting alone lifts it to **SM6.8** (all other bits exist on v11).

### 3.5 Other caps

| Cap | Gate | G615 |
|---|---|---|
| ResourceBindingTier | always 3 (device.c:9267) | 3 |
| Descriptor model | `VK_EXT_descriptor_buffer` path needs descriptorBuffer + descriptorBufferPushDescriptors + UBO non-uniform indexing and `minStorageBufferOffsetAlignment` not in (4,16] (resource.c:11340-11390), plus a range of (1M+1) x mutable size; else descriptor sets (mutable single set when `mutableDescriptorType`) | no descriptor_buffer: mutable sets |
| VRS tier 1/2 | `VK_KHR_fragment_shading_rate` pipeline rate + 2x sample counts; tier 2 needs attachment+primitive rates (device.c:965-1088) | 0 |
| Mesh shader tier 1 | `VK_EXT_mesh_shader` mesh+task (device.c:9147) | 0 |
| Raytracing 1.0/1.1 | KHR acceleration_structure + ray_tracing_pipeline (+ ray_query for 1.1) and RT vertex formats (device.c:9029-9067) | 0 |
| Sampler feedback 0.9 | shaderInt64 + `shaderImageInt64Atomics` (device.c:9155) | ok (bits present) |
| Native 16-bit | float16+int16+16-bit SSBO access+FP16 denorm preserve+independence (device.c:9228) | blocked by denorm reporting |
| Double precision | shaderFloat64 + FP64 denorm preserve (device.c:9253) | no |
| Programmable sample positions | always 0 (device.c:9340) | - |
| Wave MMA | always 0 (device.c:9464) | - |

### 3.6 Formats

Typed UAV load set (18): R32 F/U/S + RGBA32 F/U/S + RGBA16 F/U/S + RGBA8 UN/U/S + R16 F/U/S + R8 UN/U/S, each needs `STORAGE_READ_WITHOUT_FORMAT` (G615: 18/18). D24S8 fallback to D32S8 (utils.c:730-741). A8 emulated with R8 when native A8 lacks features (utils.c:789-806). DXR vertex formats need `ACCELERATION_STRUCTURE_VERTEX_BUFFER` (device.c:9010-9019).

## 4. Variant differences

| Topic | ValveSoftware/vkd3d (2022) | Wine vkd3d 1.18 | vkd3d-proton |
|---|---|---|---|
| Vulkan version | 1.0 instance (device.c:580) | 1.0, 1.1 if available | 1.3 required |
| Required extensions | maintenance1, shader_draw_parameters | maintenance1, maintenance2, shader_draw_parameters | none by table; ~14 explicit feature checks (3.2) |
| Max FL | 11_1 in practice (no tiled/ROV/conservative) | 11_1 automatic; override to 12_x | up to 12_2 by caps |
| Shader model | 5.1 fixed (device.c:3138) | 6.0 fixed | 5.1-6.9 by caps |
| Binding tier | sampler/UBO count thresholds | same thresholds | always 3 |
| Typed UAV additional | `shaderStorageImageExtendedFormats` | ReadWithoutFormat + 15 formats STORAGE_IMAGE | 18 formats READ_WITHOUT_FORMAT |
| Descriptor heaps | virtual / Vulkan sets | Vulkan UAB sets up to 1M or virtual heaps | 1M UAB sets (hard) or descriptor buffer |
| Fences | Vulkan fences | timeline semaphores if available | timeline semaphores + sync2 (assumed) |
| robustness2 | optional | optional | robustBufferAccess2 + robustImageAccess2 + nullDescriptor hard |

## 5. PanVK (G615, v11) matrix

| Requirement | Wine 1.18 | vkd3d-proton | PanVK G615 | Evidence |
|---|---|---|---|---|
| maintenance1/2, shader_draw_parameters | hard | hard (1.1 feature) | yes | checker |
| graphics+compute queue | hard | hard | yes (2 queues in family 0) | checker, `vkd3d_timeline` |
| shaderStorageImageWriteWithoutFormat | hard | used | yes | checker |
| robustImageAccess2 | - | **hard** | **no** | checker, `panvk_vX_physical_device.c:631` |
| robustBufferAccess2 / nullDescriptor | soft | hard | yes | checker |
| push_descriptor, maintenance5/6, mirror clamp, divisor zero, xfb queries, texel alignment | soft | hard | yes | checker |
| 1M UAB descriptors (sampled/storage image, SSBO) | soft (heap size) | hard | 1048576, **executes at 1M** | `vkd3d_heap` PASS (SSBO, texel, image, mutable heaps at N=1000000) |
| timeline semaphores, sync2 | soft | used | yes, **executes** | `vkd3d_timeline` PASS (2 queues, wait-before-signal, 64-bit values, wait-any, binary mix) |
| BDA, dynamic rendering | - | used | yes | `bachata_exec bda_int64`, DXVK suite |
| mutable descriptors | soft | soft | yes, executes | `vkd3d_heap mutable_heap` |
| descriptor_buffer | - | soft (perf) | no | checker |
| typed UAV loads (18) | FL12_0 | FL12_0 | 18/18 | checker |
| sparse residency / tiled tier 2 | forced 0 | FL12_0 | **no** (kbase: NO-GO, PROGRESS) | checker |
| fragment shader interlock (ROVs) | FL12_1 | FL12_1 | **no** | checker |
| conservative raster | n/a | FL12_1 | tier 1 | checker |
| float-controls denorm (SM6.2+) | - | SM6.2-6.8 | **no** | checker |
| depthBounds | PSO cap | FL12_2 | **no** | checker, `depth_bounds` FAIL |
| shaderFloat64 | cap | cap | no | checker |
| shaderOutputViewportIndex / `VK_EXT_shader_viewport_index_layer` | GS-free VP index | FL12_2 | **no** (layer yes) | checker |
| viewportSubPixelBits >= 8 | warning | - | no | checker |
| VRS / mesh / ray tracing | 0 | FL12_2 | no | checker |

Device verdict: **Wine vkd3d 1.18 (PanPlay): PASS, 6/6 hard, FL11_1, SM6.0, tier 3.** vkd3d-proton: device creation fails (`robustImageAccess2`). With that fixed it would report FL11_1, SM6.0, tier 3, tiled tier 0, conservative tier 1.

## 6. What PanVK needs for full vkd3d(-proton) compliance

Prioritized. S = under a day, M = 1-3 days, L = a week or more.

| P | Item | Unblocks | Effort |
|---|---|---|---|
| 1 | `robustImageAccess2` on v10+ (WIP `work/mesa-ria2` branch `dx-ria2`, null image descriptors via texture subdescriptor). Gate: CTS `dEQP-VK.robustness.robustness2.*` image/texel and `image_robustness.*` 0 fail | vkd3d-proton device creation (the only hard gap) | M |
| 2 | Float-controls: PanVK reports `denormBehaviorIndependence = NONE` on v9+ (`panvk_vX_physical_device.c:1069-1071`; FP32 FTZ and preserve are already true). Report `32_BIT_ONLY` or `ALL` once the compiler can set FP16 and FP32 denorm modes independently per shader | vkd3d-proton SM6.0 -> SM6.8 (6.6/6.7 bits already present) | M |
| 3 | `VK_EXT_fragment_shader_interlock` (pixel + sample) | ROVs, FL12_1 (both variants) | L |
| 4 | Sparse residency (buffer + image 2D, aliased, standard block shape, sparse queue) on CSF. Kbase sparse was judged NO-GO (`tests/dxvk-vkd3d/test_p22_kbase_sparse_feasibility.py`) | vkd3d-proton tiled tier 2 -> FL12_0 (most D3D12 games request FL12_0) | L |
| 5 | `depthBounds` | DepthBoundsTestSupported, FL12_2, DXVK soft gap | M |
| 6 | `shaderOutputViewportIndex` + advertise `VK_EXT_shader_viewport_index_layer` | VS viewport index without GS (Wine), FL12_2 | S-M |
| 7 | `VK_EXT_descriptor_buffer` (with push descriptors) | vkd3d-proton descriptor-buffer heap path (CPU copy cost) | L |
| 8 | `viewportSubPixelBits` >= 8 (check HW precision first) | Wine FL warning | S |
| 9 | `shaderFloat64` | double-precision ops (rare in games) | L |
| 10 | VRS (`VK_KHR_fragment_shading_rate`), mesh shaders, ray query | FL12_2 | L each |

Launcher side (not driver): ship a vkd3d-proton component for D3D12 (as other launchers do) once P1 lands; until then `VKD3D_CAPS_OVERRIDE=feature_level=12.0` is the only way FL12_0 games start on Wine vkd3d (with real feature gaps).

## 7. Open risks

- D3D12 present path: Wine `d3d12.dll` with DXVK's `dxgi.dll` in the same prefix is unverified. Test a D3D12 sample in PanPlay with and without the DXVK component.
- `tests/vkd3d/d3d12-smoke.list` + `scripts/vkd3d/run-d3d12-smoke.sh` (vkd3d-proton test suite in the chroot) has never had a device run; blocked by P1 for vkd3d-proton.
