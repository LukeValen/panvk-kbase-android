# DXVK D3D11 FL11_1 re-check (PROGRESS item 3)

Date: 2026-10-05. Static check (no G615 attached) plus existing device logs.

## DXVK version

Bundled: DXVK v3.1.1 (`apps/panvk-launcher/scripts/components.env`: `DXVK_TAG=v3.1.1`, commit `b1a1c99a`, package `3.1.1-1-arm64ec`). Source read from `/var/tmp/panvk/dxvk-clear-src` (tag v3.1.1).

## DXVK 3.1.1 feature-level logic

`src/d3d11/d3d11_device.cpp` `D3D11Device::GetMaxFeatureLevel`: `d3d11.maxFeatureLevel` in dxvk.conf wins (the launcher does not set it). Otherwise `src/d3d11/d3d11_features.cpp` `D3D11DeviceFeatures::GetMaxFeatureLevel` checks the enabled `VkDevice` features:

| FL | Requirement (DXVK 3.1.1) | Vulkan source |
|---|---|---|
| 11_0 | drawIndirectFirstInstance, fragmentStoresAndAtomics, multiDrawIndirect, tessellationShader | core features |
| 11_1 | `OutputMergerLogicOp` | `logicOp` |
| 11_1 | vertexPipelineStoresAndAtomics | core feature |
| 12_0 | TiledResourcesTier >= 2 | sparseBinding, sparseResidencyBuffer, sparseResidencyImage2D, sparseResidencyAliased, residencyStandard2DBlockShape, shaderResourceResidency, shaderResourceMinLod, samplerFilterMinmax, filterMinmaxSingleComponentFormats, residencyNonResidentStrict, !residencyAlignedMipSize |
| 12_0 | TypedUAVLoadAdditionalFormats | `STORAGE_READ_WITHOUT_FORMAT` (optimal or linear) on 18 formats: R32 F/U/S, RGBA32 F/U/S, RGBA16 F/U/S, RGBA8 UNORM/U/S, R16 F/U/S, R8 UNORM/U/S |
| 12_1 | ConservativeRasterizationTier >= 1 | `VK_EXT_conservative_rasterization` |
| 12_1 | ROVsSupported | `fragmentShaderPixelInterlock` (`VK_EXT_fragment_shader_interlock`) |

`logicOp` and `vertexPipelineStoresAndAtomics` are optional features in `dxvk_device_info.cpp` (enabled when the driver reports them). Device creation also needs the required list there, including `geometryShader`, `multiViewport`, `fillModeNonSolid`, `textureCompressionBC`, `shaderInt64`, `depthClamp`, `dualSrcBlend` and Vulkan 1.3 features.

## PanVK vs requirements

PanVK code: `src/panfrost/vulkan/panvk_vX_physical_device.c` in the combined tree `/var/tmp/panvk/wt-combined` (pin + series through csf-v11/108 + android/014). Tester dumps: `tmp/universal-logs/v11/*` (G615 MC2/MC6, beta.13-15), `v12/94a3d66c` (G720 MC7, beta.14), `v13/e74f908f` (G925 as "G725 MC12", beta.15). No v10 dump exists (G610 only enumerated).

| Requirement | v10 (code) | v11 (G615 dumps) | v12 (G720 dump) | v13/v14 | Status |
|---|---|---|---|---|---|
| drawIndirectFirstInstance | true | true | true | true | OK |
| fragmentStoresAndAtomics | true | true | true | true | OK |
| multiDrawIndirect | `PAN_ARCH >= 10` | true | true | true | OK |
| tessellationShader | `PAN_ARCH >= 10` | true | true | true | OK |
| logicOp | true | true | true | true | OK |
| vertexPipelineStoresAndAtomics | `10 <= PAN_ARCH < 13` | true | true | false (drirc `enable_vertex_pipeline_stores_atomics` only) | OK v10-v12; v13+ blocks 11_1 |
| sparse* (tiled resources tier 2) | `has_sparse = PAN_ARCH >= 10 && !kbase_node_path[0]` -> false on kbase | false | false | false | Blocks 12_0 (sparse NO-GO on kbase) |
| shaderResourceMinLod | false | - | - | false | Blocks tier 2 too |
| STORAGE_READ_WITHOUT_FORMAT on 18 formats | set for all storage-image formats except R64 | shaderStorageImageReadWithoutFormat true | true | true | Probably OK (not the blocker) |
| VK_EXT_conservative_rasterization | `PAN_ARCH >= 11` | present | present | present | OK (12_1 only) |
| fragmentShaderPixelInterlock | not exposed | absent | absent | absent | Blocks 12_1 |
| textureCompressionBC (device creation) | false before 108 on G610; 108 emulates | true | true | true | v10 needs 108 + tester rerun |

## Result

Expected max FL from DXVK 3.1.1:

- v9 JM: no D3D11 device (DXVK rejects; tessellation and the item 12 gaps). Would be 10_1 at best.
- v10 (G610): 11_1, once 108 lets DXVK create the device. Unproven on hardware.
- v11 (G615): **11_1, already seen on the device.** Launcher logs from 2026-10-03 (G615 MC6, beta.10+ ICD) print `D3D11InternalCreateDevice: Maximum supported feature level: D3D_FEATURE_LEVEL_11_1`: `apps/panvk-launcher/tests/results/miside/iter08/wine-run.log`, `shortcuts/logs-dxdraw_x64/wine-run.log`, `shortcuts/logs-DX10_EmptyProject/wine-run.log`. MiSide also logs `Using feature level D3D_FEATURE_LEVEL_11_1`. The old "FL 11_0 (0xb000)" in PROGRESS predates 078.
- v12 (G720): 11_1 (same bits as v11 in the beta.14 dump).
- v13/v14 (G925 etc.): 11_0. `vertexPipelineStoresAndAtomics` is off by default on `PAN_ARCH >= 13` (gpu_prerast path is v10-v12 only); the G925 beta.15 dump confirms false.

Blockers:
- 11_1 on v13+: vertexPipelineStoresAndAtomics. Needs the VS-store path validated on v13 (drirc opt-in exists).
- 12_0: tiled resources (sparse residency, `shaderResourceMinLod`). Sparse is NO-GO on kbase; out of scope.
- 12_1: `VK_EXT_fragment_shader_interlock` (ROVs). Not planned.

## G615 commands (when the device is back)

```sh
# 1. feature bits from the installed ICD (PanProbe/panvk-test info, or vulkaninfo in the chroot)
adb shell 'vulkaninfo --summary 2>/dev/null; vulkaninfo 2>/dev/null' | grep -E 'logicOp|vertexPipelineStoresAndAtomics|tessellationShader|multiDrawIndirect|drawIndirectFirstInstance|fragmentStoresAndAtomics' | sort -u
# 2. DXVK verdict: run any D3D11 shortcut in the launcher (DXVK_LOG_LEVEL=info is set by Containers.kt), then pull the newest session log
adb shell 'run-as dev.zenithblue.panvklauncher sh -c "cat \$(ls -td files/sessions/*/ | head -1)wine-run.log"' | grep -E 'Maximum supported feature level|Using feature level|Driver *:'
# Expect: Maximum supported feature level: D3D_FEATURE_LEVEL_11_1
```

Session logs live in the app's `files/sessions/<time>_<name>/` (`SessionLogs.kt`); `run-as` needs a debuggable build, otherwise use the in-app log export.
