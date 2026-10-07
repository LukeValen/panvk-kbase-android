# PanProbe vkd3d compliance plan

Date: 2026-10-06. Input: [vkd3d-vulkan-requirements.md](vkd3d-vulkan-requirements.md). Pattern: [panprobe-compliance-plan.md](panprobe-compliance-plan.md) (DXVK and Bachata S4).

Target: PanPlay's D3D12 = Wine-bundled vkd3d 1.18 (Proton 11.0-2 `wined3d.dll`). The checker uses its device-creation failures as HARD and lists every vkd3d-proton hard requirement as a `proton:` soft item.

Paths: `M` = `apps/panvk-test/app/src/main/java/dev/zenithblue/panvktest/MainActivity.kt`, `I` = `.../InfoUi.kt`, `N` = `.../Native.kt`, `S` = `.../Share.kt`, `C` = `apps/panvk-test/app/src/main/cpp/CMakeLists.txt`.
Effort: S = under a day, M = 1-3 days, L = a week or more. Status: **DONE** = implemented and run on G615 (run `20261006-201701`); **later** = planned.

## Phase 1: checker, Info section, zip and upload

| # | Item | Files | Output | Pass criteria | Effort | Status |
|---|---|---|---|---|---|---|
| 1.1 | `vkd3d_reqs` checker. HARD (Wine vkd3d 1.18): maintenance1, maintenance2, shader_draw_parameters, graphics+compute queue, shaderStorageImageWriteWithoutFormat, nullDescriptor or RGBA8 sampled+storage null resources. SOFT: 19-bit FL11_0 checklist, 5 limit warnings, FL11_1 bits, typed UAV additional formats, binding tier, Vulkan vs virtual heaps, 25 optional extensions/features; 23 `proton:` hard items (API 1.3, robustness2 x3, push descriptor, maintenance5/6, divisor, xfb queries, texel alignment, descriptor indexing bits, 1M UAB limits or descriptor buffer, timeline/BDA/sync2/dynamic rendering) and vkd3d-proton gates (tiled tier, typed UAV 18, conservative, ROV, SM6.0/6.2/6.6/6.7, mutable, descriptor buffer, depth bounds, VS viewport/layer, VRS, mesh, ray query, sampler feedback). INFO: `wine_feature_level`, `wine_shader_model`, `wine_binding_tier`, `wine_descriptor_heaps`, `uab_per_stage`, `proton_device_create`, `proton_feature_level`, `proton_shader_model`, `proton_binding_tier`, `proton_tiled_tier`/`conservative_tier`, `proton_descriptor_model`, `typed_uav_load`. Same line format as `dxvk_reqs` | `tests/vkd3d/vkd3d_reqs.c`, `C` | `libt_vkd3d_reqs.so`, log `*-compliance-vkd3d_reqs.log` | `RESULT PASS` = 6/6 hard | S | DONE |
| 1.2 | Register as third compliance checker: `COMPLIANCE_CHECKERS` entry `("vkd3d", "vkd3d compliance", "vkd3d_reqs")`. Everything else is data-driven: Info section (PASS/FAIL pill, INFO lines, four lists), `vulkan-info.json` `compliance.vkd3d` with all items, `<run>/compliance.json`, `<run>/compliance-vkd3d_reqs.log`, `summary.json`/`manifest.json` verdicts, autorun `COMPLIANCE` line, upload `extra_json.compliance.vkd3d` | `N` (one line); `I`, `M`, `S` unchanged | Info "vkd3d compliance"; zip entries | Every item once in the section; zip contains the log and the block | S | DONE |
| 1.3 | `extra_json` budget: three checkers' `{pass, missingHard}` = 231 chars on G615 (cap 2048; `S` drops name lists first if exceeded) | `S` | - | < 2048 | S | DONE (verified) |
| 1.4 | Screenshots of the vkd3d section | `apps/panvk-test/tests/results/compliance/05-vkd3d-expanded.png`, `06-vkd3d-lists.png`, `07-vkd3d-soft-missing.png` | - | - | S | DONE |

G615 result: **PASS, hard 6/6, soft 79/94**. Wine: FL11_1, SM6.0, tier 3, Vulkan heaps. vkd3d-proton: device creation fails (robustImageAccess2); otherwise FL11_1, SM6.0, tier 3, tiled 0, conservative 1, mutable sets.

## Phase 2: execution tests for the riskiest vkd3d features

Not duplicated by the DXVK/Bachata Phase 2 tests (`robustness2`, `blend`, `descriptor_model` (small-scale BDA/runtime array/UAB/partially bound/inline UBO/scalar), `occlusion_query`, `depth_stencil`, `sampler`, `shader_arith`, `draw_params`, `submit_stress`, `bachata_storage_fmtless`, `bachata_dynamic_render`). Registered with `add_vk_test` in `C` and `TestCase` in `M` after `bachata_exec`.

| # | Test | Source | Covers | Pass criteria | G615 | Effort | Status |
|---|---|---|---|---|---|---|---|
| 2.1 | `vkd3d_heap` | `tests/vkd3d/heap/vkd3d_heap.c` (+ `*.comp`, `build_spv.sh`, `vkd3d_heap_spv.h`) | vkd3d-proton bindless heap at full size: N = min(1,000,000, UAB limits) arrays with UPDATE_AFTER_BIND + PARTIALLY_BOUND + VARIABLE_DESCRIPTOR_COUNT + UPDATE_UNUSED_WHILE_PENDING. Cases `ssbo_heap` (8 non-uniform indices incl. N-1, descriptor rewritten after `vkEndCommandBuffer`), `texel_heap` (uniform + storage texel buffer heaps), `image_heap` (sampled images), `mutable_heap` (`VK_DESCRIPTOR_TYPE_MUTABLE_EXT` aliased as SSBO and texel buffer). Each case in a forked child with a 12 s watchdog | Exact values; reduced N (allocation retry) is FAIL | PASS, N=1000000, 466 ms | M | DONE |
| 2.2 | `vkd3d_timeline` | `tests/vkd3d/timeline/vkd3d_timeline.c` | D3D12 fence model: timeline semaphores + `vkQueueSubmit2`; host signal unblocks GPU wait, 32-batch wait-before-signal chain across 2 queues, 64-bit value jump (1e12), `WAIT_ANY`, binary + timeline mix (present path) | Counters/buffers exact; every wait bounded to 5 s | PASS, 2 queues, 277 ms | S | DONE |
| 2.3 | `vkd3d_typed_uav` | `tests/vkd3d/typed-uav/` | Typed UAV loads (`OpTypeImage Unknown` format) on all 18 D3D12 formats, RMW round trip | Exact per format | - | S | later (`bachata_storage_fmtless` covers 4 formats) |
| 2.4 | `vkd3d_robust_image` | extend `robustness2` (2.1 in the DXVK plan) | robustImageAccess2 OOB image/texel reads return 0 + alpha rules; run once PanVK exposes it | CTS-equivalent values | - | S (after driver P1) | later |
| 2.5 | `vkd3d_root_sig` | `tests/vkd3d/root-sig/` | push descriptors + 256-byte push constants + 64 root parameters (vkd3d root signature layout), maintenance5/6 `vkCmdPushDescriptorSet2`/`vkCmdBindDescriptorSets2` | Compute output exact | - | M | later |
| 2.6 | `vkd3d_sparse` | `tests/vkd3d/sparse/` | sparse residency buffer/2D image bind + `shaderResourceResidency` (tiled tier 2) | SKIP until driver exposes sparse residency | - | M | later |

## Phase 3: real D3D12 runs

| # | Item | Effort | Status |
|---|---|---|---|
| 3.1 | D3D12 sample (`dxcube`-style D3D12 build) in PanPlay with Wine vkd3d 1.18, with and without the DXVK component (dxgi interop risk) | M | later |
| 3.2 | vkd3d-proton `tests/d3d12` smoke subset (`tests/vkd3d/d3d12-smoke.list`, `scripts/vkd3d/run-d3d12-smoke.sh`) once robustImageAccess2 lands | S (after driver P1) | later |
| 3.3 | Show `wine_feature_level` / `proton_feature_level` in the Runs list and upload record | S | later |
