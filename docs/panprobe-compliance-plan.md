# PanProbe compliance plan (DXVK 3.1.1 and Bachata S4)

Date: 2026-10-06. Inputs: [panprobe-dxvk-coverage.md](panprobe-dxvk-coverage.md) (56 DXVK hard requirements, gap matrix, top-10 missing tests), [bachata-s4-vulkan-requirements.md](bachata-s4-vulkan-requirements.md) (Bachata S4 hard/soft lists, PanVK matrix).

Paths: `M` = `apps/panvk-test/app/src/main/java/dev/zenithblue/panvktest/MainActivity.kt`, `I` = `.../InfoUi.kt`, `N` = `.../Native.kt`, `C` = `apps/panvk-test/app/src/main/cpp/CMakeLists.txt`.
Effort: S = under a day, M = 1-3 days, L = a week or more.

Status column: **DONE (phase 1)** = implemented in this change; **later** = planned.

**Status 2026-10-06:** Phases 1, 2, 3.1, 3.2 and 4 DONE; 3.3 BLOCKED on PROGRESS item 33. Device run on Poco X6 Pro (Mali-G615 MC6, v11, bundled beta.17c local candidate, PanProbe 1.2.3): 36-test suite **29 PASS / 7 FAIL / 0 SKIP**. Compliance: DXVK PASS (94 items, 65 GPU-tested), Bachata S4 PASS (114, 60), vkd3d PASS (100, 20). Autorun zip `panprobe-20261006-202914.zip` passes `tests/panprobe/verify_zip.py`. FAILs are driver bugs (PROGRESS items 34-39): `gs_viewport_depth`, `vs_viewport_index`, `depth_bounds`, `large_draw`, `vmr_secondary`, `depth_stencil` (viewport_zero_depth_range only), `gs_tess_primitive_id`.

## Phase 1: compliance evaluators, Info sections, logs and upload (implemented now)

| # | Item | Files | Test / output name | Pass criteria | Effort | Status |
|---|---|---|---|---|---|---|
| 1.1 | DXVK 3.1.1 compliance evaluator: all 56 hard requirements (API 1.3, graphics+compute queue, `maxPushConstantsSize >= 256`, 7 extension items incl. `VK_EXT_robustness2` robustBufferAccess2/nullDescriptor and depthClipEnable, 22 core, 2 Vulkan 1.1, 14 Vulkan 1.2, 8 Vulkan 1.3 features). Soft: D3D11 FL 11_0-12_1 gates, tiled resources tier 2, typed UAV load on the 18 formats via `VkFormatProperties3` `STORAGE_READ_WITHOUT_FORMAT`, D24S8/D32S8, D16S8, A8_UNORM, 27 optional features/extensions DXVK uses. Prints the max D3D11 feature level DXVK would expose. | `tests/dxvk/vulkan/dxvk-reqs/dxvk_reqs.c`, `C` | `libt_dxvk_reqs.so`, log `*-compliance-dxvk_reqs.log` | PASS = 56/56 hard available and `RESULT PASS`. Soft items never fail the report. | S | DONE (phase 1) |
| 1.2 | Bachata S4 evaluator: reuse `bachata_reqs` (hard: API, extensions, robustness policy, queue, features, extension-gated bits, limits, formats; soft list). It now also prints `HARD ok <item>` so the full itemized list is available, not only the gaps. | `tests/bachata/bachata_reqs.c` | `libt_bachata_reqs.so` (also still a suite test), log `*-compliance-bachata_reqs.log` | PASS = no `FAIL hard` line and `RESULT PASS` (includes the exact Bachata create-device chain). | S | DONE (phase 1) |
| 1.3 | Structured result over JNI: `Native.compliance(title, lib, driver, env, log)` runs the checker out of process through `Native.run` (driver crashes cannot kill the app) and returns JSON `{title, pass, status, hardMissing, softMissing, info[], items[{name, category hard/soft, status available/missing, note}]}`. `parseComplianceLog` parses `HARD ok`, `FAIL hard`, `SOFT ok`, `SOFT missing ... -- effect`, `INFO`, `RESULT`. | `N`, `M` (`runInfo`) | `compliance` array in the Vulkan info JSON | Both reports present after every Info load and every suite run. | S | DONE (phase 1) |
| 1.4 | Info page sections "DXVK compliance" and "Bachata S4 compliance": collapsible section cards in the existing style with a PASS/FAIL pill (`PASS · hard a/b · soft c/d`), INFO lines (feature level, typed UAV count), then four lists: Hard not available, Hard available, Soft not available (with the effect text), Soft available. | `I` (`VulkanInfoLazyList`, `SectionHeaderCard` tone, `ComplianceRow`) | - | Every checker item appears exactly once. | S | DONE (phase 1) |
| 1.5 | Log zip and cloud upload: each run folder gets `compliance.json` and `compliance-dxvk_reqs.log` / `compliance-bachata_reqs.log`; `summary.json` and the zip `manifest.json` carry a compact `compliance` verdict (title, pass, missingHard, missingSoft). The run folder already copies every suite test's log (incl. `bachata_reqs`, `bachata_exec`), and the zip already adds `logcat.txt` (own uid, so child test processes are included). The Vulkan info JSON (`vulkan-info.json`, `<run>/device_info.json`, "Share Vulkan JSON") carries `compliance: {dxvk: {...}, bachata_s4: {...}}` with every item; existing fields are unchanged. `summary.json` and `manifest.json` add `results[]` ({name, status PASS/FAIL/SKIP/CRASH/TIMEOUT, durationMs, log}), built from the run's results, so tests added to the suite list land in the zip with no zip or upload change. Local share and cloud upload both send the `buildRunZip` output. The `/record` call adds `extra_json.compliance` ({pass, missingHard} per checker, about 110 chars; the worker caps extra_json at 2048 and accepts any object, so no backend change). | `M` (`writeRun`, `complianceSummary`, `buildRunZip`) | zip entries `<run>/compliance.json`, `<run>/compliance-*.log` | Zip self-check (`verifyZip`) passes; entries present; upload succeeds. | S | DONE (phase 1) |

## Phase 2: DXVK execution tests (top 10 from the coverage review)

Each test is a standalone C program registered with `add_vk_test` in `C` and a `TestCase` in `M` (suite list after `bachata_exec`). Pass criteria are pixel or buffer readbacks unless noted; `RESULT SKIP` when a gating feature is absent.

| # | Test name | Source | Covers | Pass criteria | Effort | Status (G615 beta.17c) |
|---|---|---|---|---|---|---|
| 2.1 | `robustness2` | `tests/dxvk/vulkan/robustness2/robustness2.c` | nullDescriptor for UBO, SSBO, sampled/storage image and vertex buffer; robustBufferAccess2 OOB loads (return 0) and stores (dropped) | All reads 0, guard bytes untouched, no DEVICE_LOST. `bachata_exec null_descriptor` covers the SSBO case today. Also fix vkinfo to query the EXT alias names. | M | DONE, PASS |
| 2.2 | `blend` | `tests/dxvk/vulkan/blend/blend.c` | dualSrcBlend, independentBlend over 4 MRTs, logicOp (FL11_1) | Exact RGBA per attachment vs CPU reference | M | DONE, PASS |
| 2.3 | `descriptor_model` | `tests/dxvk/vulkan/descriptor-model/descriptor_model.c` | BDA, runtimeDescriptorArray, update-after-bind, partially bound, inline uniform blocks, scalar layout | Compute output matches expected buffer | M | DONE, PASS |
| 2.4 | `occlusion_query` | `tests/dxvk/vulkan/occlusion-query/occlusion_query.c` | occlusionQueryPrecise, `vkCmdResetQueryPool`, `vkResetQueryPool` (hostQueryReset) | Exact sample counts for known coverage | S | DONE, PASS |
| 2.5 | `depth_stencil` | `tests/dxvk/vulkan/depth-stencil/depth_stencil.c` | D24S8 (D32S8 fallback), D16S8, stencil ops, depthBiasClamp, depth_bias_control | Depth/stencil readback matches reference | M | DONE, FAIL: `viewport_zero_depth_range` not flat (PROGRESS 35); d16s8 SKIP |
| 2.6 | `sampler` | `tests/dxvk/vulkan/sampler/sampler.c` | anisotropy, mirror-clamp-to-edge, custom border color, cube arrays, non-seamless cubes, gather with offsets | Sampled values within tolerance of CPU reference | M | DONE, PASS |
| 2.7 | `shader_arith` | `tests/dxvk/vulkan/shader-arith/shader_arith.c` | int8/16/64, 8/16-bit storage, demote, subgroupSizeControl/computeFullSubgroups, zero-init workgroup memory | Exact buffer results | M | DONE, PASS |
| 2.8 | `csf_event`, `gs_tess_primitive_id` | existing `device/csf-event-regression.c`, `device/gs-tess-primitive-id.c` | synchronization2 events (hard), tess-fed GS PrimitiveID | Existing PASS markers | S (wiring only) | DONE; `csf_event` PASS, `gs_tess_primitive_id` FAIL (PROGRESS 34) |
| 2.9 | `draw_params` | `tests/dxvk/vulkan/draw-params/draw_params.c` | maintenance5/6, LOAD/STORE_OP_NONE, vertex_attribute_divisor, BaseVertex/BaseInstance | Per-instance outputs exact | M | DONE, PASS |
| 2.10 | DXVK gate | done as 1.1 (`dxvk_reqs`); remaining: short `submit_stress` run as a stability check (`1000000 1 30`, 120 s app timeout) | `tests/dxvk/vulkan/submit_stress/submit_stress.c` | No DEVICE_LOST within 30 s | S | DONE, PASS (30.1 s) |

## Phase 3: remaining Bachata S4 execution tests

`bachata_reqs` and `bachata_exec` (int64 atomics, null descriptor, BDA int64) already run. Remaining hard features without an execution test:

| # | Test name | Source | Covers | Pass criteria | Effort | Status |
|---|---|---|---|---|---|---|
| 3.1 | `bachata_storage_fmtless` | `tests/bachata/storage_fmtless.c` | shaderStorageImageRead/WriteWithoutFormat on R32/RGBA8/RGBA16F/RGBA32F | Read-modify-write round trip exact | S | DONE, PASS |
| 3.2 | `bachata_dynamic_render` | `tests/bachata/dynamic_render.c` | dynamicRendering + synchronization2 barriers + custom border color + depth clip control | Pixel readback | M | DONE, PASS |
| 3.3 | `bachata_robust_v10` | extend 2.1 | robustBufferAccess2 OOB on v10 once the driver exposes it (PROGRESS item 33) | Same as 2.1 | S (after driver work) | BLOCKED on PROGRESS item 33 |

## Phase 4: reporting polish

| # | Item | Files | Effort | Status |
|---|---|---|---|---|
| 4.1 | Show compliance PASS/FAIL in the Runs list and the upload record (`Share.kt buildUploadRecord`), so the D1 table can filter by it | `M`, `Share.kt` | S | DONE: Runs rows show `DXVK PASS · S4 PASS · vkd3d PASS` from `summary.json`; `extra_json.compliance` {pass, missingHard} per checker (missingHard dropped if over 2048 chars) |
| 4.2 | Mark items with an execution test (EXEC) vs query-only in the Info lists, using the gap matrix | `I`, `N` | S | DONE: static map `TESTED_BY` in `N`; every item has `tested_by` [tests] in the compliance JSON; Info rows show "GPU-tested: ..." or "reported only" |
| 4.3 | Add `dxvk_reqs` as a suite test once SKIP/FAIL semantics for "soft only" are agreed (today it runs with Info, not in the 19-test suite) | `M` | S | DONE: `dxvk_reqs` and `vkd3d_reqs` in the suite; PASS when all hard reqs are available (soft gaps still PASS), FAIL only on a `FAIL hard` line |
