# Bachata S4 Vulkan requirements vs PanVK

Date: 2026-10-06. References:

- Bachata S4: `~/repos/Bachata-S4-Dev` at `15cc1f2` (shadPS4 fork, Android). `B` = `src/video_core/renderer_vulkan/vk_instance.cpp`, `BH` = `.../vk_instance.h`, `BP` = `.../vk_platform.cpp`, `R` = `src/shader_recompiler`.
- shadps4-arm64: `~/repos/shadps4-arm64` at `be6bc2e9`. `S` = `src/video_core/renderer_vulkan/vk_instance.cpp`.
- PanVK: beta.17 tree `/var/tmp/panvk/wt-beta17b` at `eda9ac0` (Mesa 26.3.0-devel). `P` = `src/panfrost/vulkan/panvk_vX_physical_device.c`.
- Device evidence: Poco X6 Pro, Mali-G615 MC6 (GPU ID `0xb8a31030`, Mesa `PAN_ARCH` 11), driver string `PanVK-kbase beta.17 (Mesa 26.3.0-devel (git-eda9ac0c6f))`, run of the new PanProbe tests `bachata_reqs` and `bachata_exec`.

## Overview

- Bachata needs **Vulkan 1.3** (`vk_platform.h:21` `TargetVulkanApiVersion = VK_API_VERSION_1_3`). The loader instance version is asserted at `BP:319-321`, the device version at `B:177`.
- Only three things are explicit hard gates in `CreateDevice` (`B:212`): `VK_KHR_swapchain` (`B:270`), `VK_EXT_robustness2` (`B:280`), and the robustness feature policy (`B:283-292`, `robust_image_policy.h`): `robustBufferAccess2` and `nullDescriptor` are required, and either `robustImageAccess2` or core `robustImageAccess`.
- Everything else is "enable if present", but many of those features are used **unconditionally** by the shader recompiler or the renderer, so they are hidden hard requirements. Vulkan 1.3 core makes most of them mandatory anyway.
- Ten extension feature structs are enabled with bits hard-coded to `true` when only the extension is present (`B:489-558`). If a driver advertises the extension without that bit, `vkCreateDevice` fails, and `CreateDevice()`'s `false` return is ignored by the constructor (`B:182`), so the emulator crashes later.
- **Result on G615 (v11): every hard requirement is met, and the exact Bachata device-creation chain succeeds.** Seven soft items are missing. On v10 (G710/G610/G510) `robustBufferAccess2` is not advertised (`P:651`, `PAN_ARCH >= 11`), so Bachata aborts at `B:292` on those GPUs.

## Hard requirements

"Source" is the Bachata location that makes the item fatal. PanVK status is for v11 (G615), checked on the device. v12/v13 use the same code paths (`PAN_ARCH >= 10/11` conditions), but they were not run on a device.

| # | Requirement | Why it is hard (source) | PanVK v11 (G615) | v10 |
|---|---|---|---|---|
| 1 | Device apiVersion >= 1.3 | `B:177` ASSERT, `BP:321` | Supported (1.4.363, `P:813`) | Supported |
| 2 | `VK_KHR_swapchain` | `B:270` ASSERT | Supported | Supported |
| 3 | `VK_EXT_robustness2` | `B:280` ASSERT | Supported (`P:214`) | Supported |
| 4 | `robustBufferAccess2` | `B:292`, `robust_image_policy.h` SelectMode | Supported (`P:651`) | **Missing** (`PAN_ARCH >= 11`) |
| 5 | `nullDescriptor` | `B:292` | Supported (`P:653`) | Supported |
| 6 | `robustImageAccess2` or core `robustImageAccess` | `B:292` (falls back to "restricted core" mode) | Partial: `robustImageAccess2=0` (`P:652`), core `robustImageAccess=1`, so Bachata runs in RestrictedCore mode | Same |
| 7 | Graphics queue | `B:403` returns false | Supported | Supported |
| 8 | `timelineSemaphore` | Unconditional, `vk_master_semaphore.cpp:17,25` ASSERT | Supported | Supported |
| 9 | `bufferDeviceAddress` | VMA BDA flag `B:806`, `buffer_cache/buffer.cpp:260` ASSERT | Supported | Supported |
| 10 | `shaderInt8`, `shaderInt16`, `shaderInt64` | SPIR-V capabilities emitted unconditionally (`R/backend/spirv/emit_spirv.cpp:268-270`) | Supported | Supported |
| 11 | `storageBuffer8BitAccess`, `storageBuffer16BitAccess` | Emitted for U8/U16 buffers without a check (`emit_spirv.cpp:273,276`) | Supported | Supported |
| 12 | `shaderBufferInt64Atomics`, `shaderSharedInt64Atomics` | ASSERT when a shader uses them (`emit_spirv.cpp:391-400`), no emulation | Supported (`PAN_ARCH >= 9`) | Supported |
| 13 | `shaderDemoteToHelperInvocation` | Emitted for every discard (`emit_spirv_special.cpp:88`, `emit_spirv.cpp:442`) | Supported | Supported |
| 14 | `synchronization2`, `dynamicRendering`, `maintenance4`, EDS1/EDS2 core state | Used unconditionally (`vk_graphics_pipeline.cpp:273`, `vk_scheduler.cpp:512`, dispatch audit `B:643-655`) | Supported | Supported |
| 15 | `dualSrcBlend`, `depthClamp`, `sampleRateShading`, `fillModeNonSolid`, `wideLines`, `depthBiasClamp`, `drawIndirectCount` | Consumers have no support guard (`vk_graphics_pipeline.cpp:502`, `vk_scheduler.cpp:541,650`, `vk_rasterizer.cpp:558`); pipeline creation ASSERTs at `vk_graphics_pipeline.cpp:582` | Supported | Supported |
| 16 | `shaderStorageImageReadWithoutFormat` / `WriteWithoutFormat` | Capability emitted (`emit_spirv.cpp:287`), but Bachata never enables the features (`B:426-458`) | Supported (advertised) | Supported |
| 17 | Extension-gated bits hard-coded `true`: `customBorderColors`, `customBorderColorWithoutFormat`, `depthClipControl`, `depthClipEnable`, `vertexInputDynamicState`, `fragmentShaderBarycentric`, `provokingVertexLast`, `vertexAttributeInstanceRateDivisor`, `maintenance8`, `attachmentFeedbackLoopLayout`, `attachmentFeedbackLoopDynamicState`, `minLod` | `B:489-558`; a false bit with the extension present makes `vkCreateDevice` fail | Supported: all set where the extension is advertised (`P:481,608,630,642`); `fragmentShaderBarycentric` is not advertised, so its struct is unlinked | Same |
| 18 | Storage image atomic formats (explicit whitelist) | `R/backend/spirv/spirv_emit_context.cpp:973-979` UNREACHABLE for unknown combinations | Supported (`R32_UINT` storage atomic OK on device) | Supported |
| 19 | Geometry and tessellation for auxiliary RectList/QuadList paths | `vk_graphics_pipeline.cpp:156` ASSERT outside the admitted fallbacks | Supported (`P:320-321`, `PAN_ARCH >= 10`) | Supported |
| 20 | Storage-image read with LOD | `emit_spirv_image.cpp:339` UNREACHABLE without `VK_AMD_shader_image_load_store_lod` | **Missing** (shader-dependent: only titles that read storage images with an explicit LOD) | Missing |

Only #4 (v10 only) and #20 (titles that use the path) block anything on PanVK. On G615 the `bachata_reqs` test printed `HARD missing=0` and `PASS create_device_bachata_chain mode=exact`.

## Soft requirements

All are enabled only when present (`B:299-386`). The effect column says what Bachata does without them.

| Extension / feature | Bachata use | Effect when absent | PanVK v11 |
|---|---|---|---|
| `VK_KHR_push_descriptor` | `B:275` | Ordinary descriptor sets with a pool (`vk_pipeline_common.cpp:110`): extra CPU work and memory | Supported |
| `VK_EXT_vertex_attribute_divisor` | `B:279` | Divisor forced to 1 (`vk_graphics_pipeline.cpp:619`): wrong instanced step-rate fetch | Supported |
| `VK_EXT_extended_dynamic_state3` (`ColorWriteMask`) | `B:311` | Write masks baked into pipelines: more variants and compiles | Supported |
| `VK_EXT_vertex_input_dynamic_state` | `B:321` | Static vertex input plus dynamic stride: more pipeline variants | Supported |
| `VK_EXT_custom_border_color` | `B:318` | Border colour becomes opaque black (`texture_cache/sampler.cpp:22`) | Supported |
| `VK_EXT_depth_clip_control` | `B:319` | Extra depth remap in the vertex epilogue (`emit_spirv_special.cpp:79`) | Supported |
| `VK_EXT_depth_clip_enable` | `B:320` | Clip and clamp coupled through `depthClamp` (`vk_graphics_pipeline.cpp:231`) | Supported |
| `VK_EXT_primitive_topology_list_restart` | `B:322` | Restart disabled for list topologies (`vk_rasterizer.cpp:2411`): garbage primitives if a game uses it | Partial: `primitiveTopologyListRestart=1`, **`primitiveTopologyPatchListRestart=0`** (`P:622`) |
| `VK_AMD_shader_explicit_vertex_parameter` / `VK_KHR_fragment_shader_barycentric` | `B:331-336` | Flat values plus manual-interpolation varyings (`vector_interpolation.cpp:102`): extra varyings, unrecognised patterns are wrong | **Missing** |
| `VK_EXT_provoking_vertex` | `B:337` | First-vertex convention: wrong flat shading | Supported |
| `VK_EXT_shader_stencil_export` | `B:338` | Stencil reference store dropped | Supported |
| `VK_AMD_shader_image_load_store_lod` | `B:339` | Per-mip storage descriptors for writes (`emit_spirv_image.cpp:407`); storage reads with LOD are fatal (hard #20) | **Missing** |
| `VK_EXT_shader_atomic_float` | `B:344` | Recorded only, no consumer | Supported |
| `VK_EXT_shader_atomic_float2` (`Buffer/ImageFloat32AtomicMinMax`) | `B:345` | Integer-atomic emulation (`emit_spirv_atomic.cpp:342,440`); the image path does two atomics and is racy | **Missing** |
| `VK_KHR_workgroup_memory_explicit_layout` | `B:354` | 16/64-bit LDS split into 32-bit ops, mixed types spill to an SSBO | Supported |
| `VK_EXT_image_2d_view_of_3d` | `B:368` | No 2D views of 3D slices; view creation can ASSERT | Supported |
| `VK_EXT_image_view_min_lod` | `B:377` | Guest min LOD ignored | Supported |
| `VK_KHR_maintenance8` | `B:300` | Depth/colour copies go through a staging buffer (`vk_rasterizer.cpp:1080`) | Supported |
| `VK_EXT_attachment_feedback_loop_layout` + `_dynamic_state` | `B:301-309` | GENERAL layout for feedback loops, worse compression | Supported |
| `VK_EXT_depth_range_unrestricted` | `B:310` | Viewport depth clamped to 0..1 (`vk_rasterizer.cpp:2241`) | **Missing** |
| `VK_EXT_memory_budget` | `B:378` | Fixed GC thresholds; Bachata's PanVK compact profile loses live-budget GC | Supported |
| `VK_KHR_shader_clock` | `B:379` | No subgroup clock | Supported (kbase timestamp query) |
| `shaderFloat64` | core | FP64 lowered to FP32 (`R/recompiler.cpp:427`): precision loss | **Missing** (`P:363`) |
| `shaderFloat16` | 1.2 | Pack/unpack through FP32 | Supported |
| `depthBounds`, `logicOp` | core | Depth bounds test / logic op dropped | Supported |
| `samplerAnisotropy`, `pipelineStatisticsQuery`, `textureCompressionETC2` | core | Feature off; ETC2 is the BC transcode target | Supported |
| Required subgroup size 64 in compute | `BH:300` | Driver subgroup size used; no wave64 emulation, so GCN lane, ballot and lockstep semantics can break (`vk_compute_pipeline.cpp:34`) | **Missing**: Mali warp is 16 (`P:1171-1176`) |
| Fragment subgroup quad ops | `BH:410` | Derivative emulation for one title only | Supported |
| BC1-7 sampled | texture cache | GPU decode only in RestrictedCore mode, limited formats (`texture_cache/image.cpp:244`) | Supported (PanVK BC emulation) |

`bachata_reqs` on G615: `SOFT missing=7: primitiveTopologyPatchListRestart, VK_KHR_fragment_shader_barycentric, VK_AMD_shader_image_load_store_lod, VK_EXT_shader_atomic_float2, VK_EXT_depth_range_unrestricted, shaderFloat64, subgroup_wave64`.

## Formats

Bachata caches format properties for every guest surface and depth format plus `A2R10G10B10`, `B8G8R8A8` (UNORM/SRGB), `D24_UNORM_S8_UINT` and ETC2 RGBA8 (`B:60-90`). Its device-side fallback table is small: `D16S8 -> D24S8 -> D32S8` and `R8_SRGB -> R8_UNORM` (`B:1253`). Anything else returns the unsupported format and fails at image, view or pipeline creation (`B:1253-1270`). `B:1242` ORs buffer and optimal-tiling features, which can overstate image support.

| Format group | Needed features | PanVK v11 (device) |
|---|---|---|
| `B10G11R11_UFLOAT`, `A2B10G10R10_UNORM`, `A2R10G10B10_UNORM`, `R16G16B16A16_SFLOAT`, `R32G32B32A32_SFLOAT`, `R8G8B8A8_UNORM`, `B8G8R8A8_UNORM` | sampled + colour attachment | Supported |
| `R32_UINT`, `R32_SFLOAT`, `R8G8B8A8_UNORM`, `R16G16B16A16_SFLOAT`, `R32G32B32A32_SFLOAT` | storage image | Supported |
| `R32_UINT` | storage image atomic | Supported |
| `D32_SFLOAT`, `D16_UNORM`, one of `D24_UNORM_S8_UINT` / `D32_SFLOAT_S8_UINT` | depth/stencil attachment | Supported |
| BC1, BC3, BC4, BC5, BC6H, BC7 | sampled | Supported (emulated, `PANVK_DEBUG=no_bc_emul` disables) |
| ETC2 RGBA8 | sampled (BC transcode target) | Supported |

## Limits

| Limit | Bachata need | PanVK v11 (device) |
|---|---|---|
| `maxPushConstantsSize` | >= 120 (PushData 16 + 64 + 40 bytes, `R/resource.h:289`, `static_assert <= 128` at `:308`) | 256 |
| `minStorageBufferOffsetAlignment` | Offset remainder must be < 256 (`R/resource.h:304` ASSERT) | 4 |
| `minUniformBufferOffsetAlignment` | Same path | 16 |
| `maxPerStageDescriptorStorageBuffers` | 40 buffers per stage (`R/resource.h:17`) | 1048576 |
| `maxComputeSharedMemorySize` | Compared with the shader's LDS; larger LDS spills to an SSBO (`shared_memory_to_storage_pass.cpp:111`). PS4 LDS is 64 KiB | 32768 (spill path used above 32 KiB) |
| `maxSamplerAllocationCount` | GC at 3/4, 7/8, 15/16 of it (`texture_cache.cpp:111`) | 4294967295 |
| `maxComputeWorkGroupCount` | Tiling splits dispatches (`tile_manager.cpp:100`); guest and BC/ETC dispatches are not split | Not gated by the test |
| Subgroup size | 64 wanted, 64-bit ballots (`emit_spirv_warp.cpp:94`) | 16 |
| `maxBoundDescriptorSets` | Small (single set per stage) | 7 |

## Repository differences

| Area | Bachata-S4-Dev | shadps4-arm64 |
|---|---|---|
| API version | 1.3 (`vk_platform.h:21`) | 1.3 (same) |
| `VK_EXT_robustness2` | Hard, with feature policy (`B:280-292`) | Optional (`S:281`), bits requested if supported (`S:472`) |
| `VK_EXT_legacy_vertex_attributes` | Not enabled | Optional (`S:309,516`), disabled by name on Adreno 830/840 (`legacy_vertex_attributes.h:54`). PanVK does not advertise it |
| `VK_EXT_image_view_min_lod`, `VK_KHR_shader_clock` | Optional (`B:377,379`) | Not used |
| Extra core features | `pipelineStatisticsQuery`, `textureCompressionETC2`, `shaderSampledImageArrayDynamicIndexing` (`B:444-451`) | `uniformAndStorageBuffer16/8BitAccess` (`S:431,438`) |
| Subgroup 64 check | Also checks min size and stages (`BH:298`) | Only `subgroupSizeControl` and max size |
| Android surface | Native `VK_KHR_android_surface` path (`BP:74-80,193`) | Platform defined only, no Android surface branch |
| Driver loading | `platform/android/native_runtime.cpp:78-114`: direct ICD or adrenotools Turnip via environment, else `libvulkan.so`. `vulkan_provider.cpp:62-115`: SHA-256 verified sealed snapshot, `android_dlopen_ext`, `vk_icdGetInstanceProcAddr`, ICD interface 5-7. Layers are bypassed | Vulkan-Hpp `DynamicLoader` only (`S`-tree `vk_platform.cpp:275`) |
| PanVK admission | `platform/android/panvk_kbase_profile.cpp:66-117`: arch 10-13 CSF, 4 KiB pages, exact GPU/UAPI metadata, `/dev/mali0` queries; mismatch throws | None |
| PanVK workarounds | `eMesaPanvk` render-area clamp for attachment-less passes (`vk_scheduler.cpp:42`); no initial storage usage on PanVK colour images to keep AFBC (`texture_cache/image.cpp:46`); GPU ETC2 transcode gated on PanVK and title (`image.cpp:262`); compact 64 MiB VMA blocks and memory-budget GC for PanVK (`B:801-809`, `buffer_cache.cpp:69`) | None |
| Mali/Turnip shared workarounds | `BACHATA_MALI_GPU_OPT` scratch-ring reuse (`tile_manager.cpp:1002`), soft-fail on device loss in present (`vk_presenter.cpp:955`), skipped format/tool diagnostics under Box64/Turnip (`B:919,949`) | Same, at different lines |
| Driver blocklists | None by driver ID in either tree | None |

## PanVK compliance matrix

| Arch (GPUs) | Hard | Soft missing | Notes |
|---|---|---|---|
| v9 JM (G57/G77/G78) | **Fails**: `VK_EXT_robustness2` only for `PAN_ARCH >= 10` (`P:214`) | n/a | Bachata's PanVK admission also rejects JM |
| v10 (G710/G610/G510) | **Fails**: `robustBufferAccess2=0` (`P:651`) | 7 (as v11) | Fix item 1 below |
| v11 (G615/G715) | **Pass** (device-verified, exact create chain OK) | 7 | Primary target |
| v12 (G720/G620) | Expected pass (same code paths) | 7 expected | Not run; gpu_id gaps for some SKUs |
| v13 (G725/G625) | Expected pass | 7 expected | Not run |
| v14 (G1-Ultra) | Blocked before Vulkan (kbase `MEM_ALLOC_EX` ENOTTY) | n/a | Driver bring-up issue, not a Bachata requirement |

## What PanVK needs to be fully compliant for Bachata S4

Ordered by impact. S = days, M = 1-2 weeks, L = more than 2 weeks or research.

1. **`robustBufferAccess2` on v10** (M). The only hard blocker on any CSF GPU Bachata admits. Needs bounds-checked SSBO/UBO access on v10 (NIR robust-access lowering against descriptor sizes, or the v11 hardware path if v10 has it) plus CTS `robustness2` runs.
2. **`VK_AMD_shader_image_load_store_lod`** (M). Removes a fatal shader path (storage-image read with LOD, hard #20) and the per-mip descriptor fallback. PanVK already handles per-level storage views, so the work is SPIR-V/NIR plumbing of an explicit LOD into image load/store (to be confirmed against the Valhall image instructions). It is a vendor extension, so it stays in this fork.
3. **`VK_KHR_fragment_shader_barycentric`** (M-L). Bachata's manual-interpolation fallback only handles recognised patterns; other shaders render wrong. Needs per-vertex attribute access in the fragment shader (fetch the three vertices' varyings and the barycentrics).
4. **`VK_EXT_shader_atomic_float2` float32 min/max (buffer and image)** (M). The Bachata fallback for images does two atomics and is racy. A NIR lowering to a compare-exchange loop or sign-split integer min/max would be enough.
5. **`VK_EXT_depth_range_unrestricted`** (S-M). Games with depth outside 0..1 get clamped. Needs the viewport/depth clamp path to accept unrestricted values on CSF.
6. **`primitiveTopologyPatchListRestart`** (S-M). Patch-list restart with PanVK's tessellation emulation.
7. **`shaderFloat64`** (L). Bachata lowers FP64 to FP32, so this only matters for precision-sensitive titles. Would use Mesa's soft-fp64 lowering.
8. **Subgroup size 64** (L, likely not feasible). Mali warps are 16 wide. Wave64 semantics would need compiler-level emulation; better handled in Bachata's recompiler.
9. **`robustImageAccess2`** (M, optional). Bachata already runs in RestrictedCore mode without it; adding it switches to native robustness and enables its general BC decode path.
10. **`maxComputeSharedMemorySize` above 32 KiB** (L, hardware-bound). PS4 shaders using up to 64 KiB LDS spill to an SSBO, which is slower.

Bachata-side bugs found during the review (not driver work): `shaderStorageImageRead/WriteWithoutFormat` are not enabled although the capability is emitted (`B:426-458` vs `emit_spirv.cpp:287`); `Int8/Int16/Int64` capabilities are emitted unconditionally (`emit_spirv.cpp:268-270`); `CreateDevice()`'s return value is ignored (`B:182`).

## New PanProbe tests

Registered in `apps/panvk-test/app/src/main/java/dev/zenithblue/panvktest/MainActivity.kt` (after `tess_cond_state`) and `apps/panvk-test/app/src/main/cpp/CMakeLists.txt` (`add_vk_test`). PanProbe now runs 19 tests.

| Test | Source | What it checks | G615 beta.17 result |
|---|---|---|---|
| `bachata_reqs` | `tests/bachata/bachata_reqs.c` | Every hard requirement above (API version, extensions, robustness policy, queue, 1.1/1.2/1.3/core features, extension-gated hard-true bits, limits, formats via `VkFormatProperties3`), printing `FAIL hard <item>` per gap. A non-failing soft report (`SOFT ok` / `SOFT missing <item> -- <effect>`). Then it creates a device with Bachata's exact extension list and feature chain (`mode=exact`), or with `robustBufferAccess2` dropped when the driver lacks it (`mode=relaxed_no_robustBufferAccess2`) so v10 shows whether that bit is the only blocker. | PASS. `HARD missing=0`, `SOFT missing=7`, `PASS create_device_bachata_chain mode=exact` |
| `bachata_exec` | `tests/bachata/bachata_exec.c`, shaders `tests/bachata/*.comp`, SPIR-V in `bachata_exec_spv.h` (regenerate with `tests/bachata/build_spv.sh`) | Three compute executions of hard features PanProbe did not exercise: `int64_atomics` (64-bit buffer `atomicAdd`/`atomicMax` from 4096 invocations), `null_descriptor` (read through a `VK_NULL_HANDLE` storage-buffer descriptor must return 0, plus a `robustBufferAccess2` out-of-bounds read when supported), `bda_int64` (64-bit stores through a buffer device address from a push constant) | PASS: all three cases pass |
