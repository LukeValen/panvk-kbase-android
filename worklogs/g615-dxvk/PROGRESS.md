# G615 DXVK / vkd3d progress

Snapshot: 2026-10-06, after beta.16 (Mesa 5a07217f + csf-v11 up to 107; Mali v9 JM experimental since beta.14). The beta.17 candidate is built but unreleased and not hardware-verified on the G615 (see below). Device: Mali G615 (PAN_ARCH 11, gpu_id 0xb8a31030). Fallout 4 hangs before the driver loads; the root cause is two bugs in the bundled Proton arm64ec, not the driver (worklogs/driver-remaining/fo4-hang-before-driver.md).

Launcher scope correction (2026-10-02): ARM64EC DX8/9/10/11 clear + source
readback + X11 Present passes, including actual app path-only launches. Evidence:
`apps/panvk-launcher/tests/results/arm64ec-matrix/README.md` and sibling
`arm64ec-ui-d3dN/` bundles. SUPERSEDED (2026-10-03): normal no-readback
presentation now passes via DXVK `endCurrentPass(false)` fix (commit e738515);
see `apps/panvk-launcher/tests/results/final-discrimination/runtime-fix/README.md`.
Earlier black images were deferred DXVK clears, not a driver sync gap. Native profile/feature
results and earlier paired i686 runs do not establish real-game compatibility.
i686 WOW64 staging/SAME_VA failure was later fixed by 091 in beta.10; i686 games now run in PanPlay.

## Done and device-proven

| Area | Proof |
|---|---|
| DX0-DX4 base, contracts | host + device tests |
| DX5 GPU vertex shader (gpu_prerast) | 13/13 IDVS and prerast paths |
| BC1-7 GPU decode (default on) | CTS BC subset 1863 pass / 0 fail; copy_and_blit 9620 / 0 |
| Clip/cull distance, multiViewport, fillModeNonSolid | device matrices 0 fail |
| Zero-initialized memory | 408 pass / 0 fail |
| Tiler heap fix (043) | 380k render passes, 150k submits |
| Pipeline statistics queries (049-054) | CTS 14,098 pass / 0 fail |
| Upstream backports (incremental_present, swapchain_colorspace, image_compression_control) | device probes 0 fail |
| Tessellation + transform feedback integrated (`work/mesa-dxint` `dx-integrate` on e2fde360503, `csf-v11/065-068`) | matrices 0 fail (tess 24/24, xfb 17/17 incl. tes_capture); CTS tessellation 526/0, transform_feedback 15793/0 (133695 cases, 2 intermittent DeviceLost, pre-existing), geometry 189/0, conditional_rendering 922/0, statistics_query 15374/0, draw subset 3446/0; DXVK Native v3.1.1 FL 11_0 (`0xb000`) |
| `VK_EXT_memory_priority` + `VK_EXT_pageable_device_local_memory` (069) | CTS 224/0, 202/0; api.info 7799/0 |
| `alphaToOne` (070) | CTS 123/0 |
| `maxGeometryShaderInvocations` 64 (071) | geometry 193/0, instanced 20/0 |
| `VK_EXT_multi_draw` (072) | CTS 12704/0 |
| `VK_EXT_primitives_generated_query` (073) | CTS 75206/0 |
| `variableMultisampleRate` on v10+ (074) | No-attachment passes split the render context when the sample count changes. CTS variable_rate + mixed_attachment_samples 504/0; no-attachment / dynamic_rendering subset 1756/0. Limit: render contexts inherited by secondary or other command buffers are not split. |
| `SYNC_FD` export via kbase KCPU queue (075) | CQS wait then fence signal. Root cause: `/dev/sw_sync` is absent on the GKI kernel and the failed open was reported as out of host memory. api.external sync_fd + synchronization.cross_instance: 113 ResourceError -> 1996 pass / 0 fail. |
| Honour geometry shader viewport index on v10+ (076) | Root cause: a GS-written viewport index was dropped, so every primitive used viewport 0 / scissor 0. draw scissor tests 18 fail -> 88/88. |
| System scope for subqueue sync signals on kbase (077) | Root cause: a blocked CS sync wait was not re-evaluated after a sibling's CSG-scope signal. synchronization.signal_order 11-16 timeouts per run -> 1316 pass / 0 aborted. Likely also fixed the random DeviceLost in renderpasses.dynamic_rendering (2 per run -> 0 in 4 runs of 54367 results; link not proven; handoff `tmp/HANDOFF-devicelost.md`). |
| GS draw drop (046) | Already in the tree as patch 046 (same as dx7-prerast-fix `7b6da4ee60f`); geometry 193/0. |
| JICA98 0005 GPU semaphore waits | Superseded by 103: GPU waits are default since beta.15; `PANVK_KBASE_GPU_SEMAPHORE_WAITS=0` restores CPU waits. Earlier opt-in implementation had 3 review bugs (empty submit, same-subqueue skip, stale wait table); CTS with it off: 1881/0. |
| APK (`apps/panvk-test`, beta.6) | Info layout fix; honest conformance row ("Not Khronos-certified (driver reports 0.0.0.0)"); Vulkan 1.3 and 1.4 core required features both met; new swapchain_lifecycle test on the Android surface: 300 frames at 64-86 FPS, recreate + 120 frames at 60 FPS, 10x create/destroy in 1.66 s, no hang (2/2 runs, 10/10 tests pass). So the X11 present hang does not happen on the Android present path. |
| Repo cleanup (beta.6) | `.gitignore` junk removed, stale worktrees removed, `patchSeriesId` refreshed, `VALIDATION.json` points to DXVK evidence, `build-android.sh` picks matching host tools, `tests/dxvk-vkd3d` read `PANVK_MESA`, all 15 tests pass. |
| Regression on the beta.6 build | geometry, tessellation, transform_feedback.simple, multisample variable_rate 0 fail; draw 1/10 sample 1 fail (already failing before). |
| `vertexPipelineStoresAndAtomics` on v10-v12 (078, 080) | VS with SSBO stores/atomics runs on gpu_prerast; prerast arena allocated when the feature is on. CTS `atomic_operations *_vertex*` 66/0 (was 1/65 on WIP). APK `vertex_stores` test passes. Prerequisite for FL11_1. |
| Point-mode TES `gl_PointSize` (079) | Fixed; 315 tessellation cases moved NotSupported -> Pass. |
| IDVS flags from the bound VS variant (081) | Fixed. |
| Tessellation DeviceLost (082) | Root cause: on kbase each subqueue is its own CSG; an evicted waiting group is only re-checked if its sync word is in CSF event memory. Prerast arena and tess sync words moved to CSF event memory. Regression 12,132 cases: 7940 pass / 0 fail / 0 DeviceLost (beta.6: 7619 / 5 / 1). 5,613-case atomics/memory model/signal_order list: 3775/0. |
| X11 WSI in the Android ICD (083, 084) | `VK_KHR_xlib_surface` + `VK_KHR_xcb_surface`; X11/XCB libs dlopened from the caller's path. Software present (`PutImage`, no DRI3/MIT-SHM). 084: present id advances; `vkWaitForPresentKHR` timeout returns `VK_TIMEOUT`, not DeviceLost. Termux:X11: 1500 frames at ~343 fps, Xlib + XCB resize pass. |
| APK | 17/17 tests pass after 096 (`autorun all` x2 and UI Run all), incl. `gs_viewport_depth`, `vs_viewport_index`, `depth_bounds`, `large_draw`, `vmr_secondary`, `tess_cond_state`. |
| Placed maps / i686 (091, beta.10) | dma-heap zero-copy maps replace beta.9 word-wise shadow merges; NFS Most Wanted 0.5 -> 39-66 fps, clean HUD/text; i686 D3D8-11 cubes 46-47 fps; memory/map_placed/basic sync CTS 4520/0/13 NotSupported. History: 32-bit blank draws / SAME_VA map failure fixed; `worklogs/driver-remaining/091-placed-dma-heap.md`. |
| Tessellation, prerast, VMR (093-096, beta.10) | State emission outside conditional rendering (093); GPU-chunked direct/indirect prerast draws (094); attachment-less VMR secondaries (095); parallel restart-strip planner (096). CTS 17067/0; APK 17/17. |
| GS-selected viewport depth (089) | Depth clamp/clip emitted as ordered viewport runs, device-verified (`worklogs/driver-remaining/089-device-verification.md`); fixes 076's union-of-depth-ranges bug. |
| VS/TES viewport index (090) and depthBounds (092) | Implemented on v10/v11, device-verified on G615, including 4x MSAA and no-FS depth bounds; `worklogs/driver-remaining/090-vs-viewport-index.md`, `092-depth-bounds.md`. |
| NFS memory / heap ops (097-098, beta.11) | Device-wide TLS and grow-on-fault prerast arenas (097); only VERTEX_TILER_STARTED heap ops on kbase (098). NFS memory blow-up and DEVICE_LOST fixed; gameplay RSS flat 2.0-2.1 GB over 10 minutes. Arena growth superseded by 100. |
| Universal ICD / driverInfo (beta.12-13, 099) | Android v10-v12 ICD in beta.12, Android + glibc v10-v14 in beta.13; 099 adds release tag to driverInfo. CS register fallback: 96 on v10/v11, 128 on v12+. G615 validated; other arches were built but untested at release. |
| Up-front prerast arenas (100, beta.13) | Fixes `gs_viewport_depth` case A (6/6); shared TLS stays. 160 MiB per VkDevice; PanProbe 17/17 x3. |
| Experimental v9 JM (beta.14, jm-v9 001-003) | JM kbase atom submission + v9 backend shipped; G57 tablet PanProbe 1/17, unchanged in beta.15/16. |
| CSF trace buffers (101, 105, beta.15) | 101 maps tracebuf at kbase-assigned VA; 105 resets CS trace buffers per submit. PanProbe passes with Mesa debug env; large trace logs can exhaust app memory. |
| Submit waits / heap creation (102-104, beta.15) | 102 renews tiler heap without graphics drain; 103 waits same-queue semaphores on GPU by default (`PANVK_KBASE_GPU_SEMAPHORE_WAITS=0` restores CPU waits); 104 creates next heap on a worker thread. NFS 24 -> 38-40 fps; PanProbe 17/17. |
| Software WSI / retired heaps (106-107, beta.16) | 106 moves present fence wait to X11 present thread (submit thread blocked 47% -> 0.2%); 107 allows up to three retired tiler heaps in flight. NFS race HUD 85.5 -> 90.6 fps, p99 22 -> 16 ms (second run 22 ms); PanProbe 17/17; CTS sync + memory gate: 56 known failures, same as beta.15. |

### After beta.8 (085-092 released in beta.9; 093-096 released in beta.10; reviewed 2026-10-03)

| Patch | Change | Device proof | Review |
|---|---|---|---|
| 085 | Descriptor-ring and `VkEvent` sync words in CSF event memory, plus host notification | Host/GPU event replay passes; image readbacks are correct. The descriptor-ring wrap is claimed but not visible in the logs. | OK, minor issues: no v10-v12 gate (also reaches v13/v14), and `SetEvent`/`ResetEvent` can return `VK_ERROR_DEVICE_LOST`, which the spec doesn't allow there. Recovery of an evicted CSG via notification is unproven. |
| 087 | Conditional rendering honoured in the tessellation compute loop; replay-safe scratch | Predicates 0,1,0 give counters 3,9,3, and inverted 9,3,9 (direct, indirect and inherited); confirmed in `run.log` | Was BLOCKING (state emission inside the GPU `cs_if` while dirty flags were cleared at record time, so a false predicate left the next draw with stale FS/depth/query state). Fixed by 093; proven by APK `tess_cond_state`. |
| 093 | Tessellation state emission no longer inside the conditional-rendering `cs_if` (fixes the 087 blocker) | CTS full list 17067 pass / 0 fail. APK `tess_cond_state` fails on 092 (chroot 3/3, APK 2/2) and passes with 093 (chroot 3/3, APK 2/2). Worklog `worklogs/driver-remaining/093-tess-state-emission.md` | Fixed. Lowering jobs and primitives-generated of a skipped conditional tess/chunked draw still run (pre-existing). |
| 094 | prerast draws chunked on the GPU, direct and indirect (no 65536-invocation cap) | APK `large_draw` 14 cases (XFB, GS, tess, restart strips, indirect, count, multi-indirect), CTS 17067 / 0. Worklog `worklogs/driver-remaining/094-prerast-chunking.md` | Restart-strip flake fixed by 096. |
| 095 | Attachment-less secondaries carry their sample count (VMR) | APK `vmr_secondary` 8/8, CTS 17067 / 0. Worklog `worklogs/driver-remaining/095-secondary-vmr.md` | Mixed counts in one secondary, cross-cmdbuf resume. |
| 096 | Restart-strip chunk planner scans with a 256-invocation workgroup (fixes the 094 flake) | chroot restart cases 250/250 (095: 6 of 127 failed), full `large_draw` 20/20, APK `large_draw` 12/12, `autorun all` 17/17 x2, UI Run all 17/17 (`validation/driver-remaining/096-device/`), CTS 36144-case list 17067 / 0 / 0 DeviceLost. Worklog `worklogs/driver-remaining/096-parallel-chunk-planner.md` | Root cause: long single-invocation planner job while the vertex/tiler CSG waits across CSGs. The kbase/firmware behaviour itself stays. |
| 088 | TES patch IDs passed to GS `PrimitiveIdIn`; loads from invalid invocations guarded | Seven readback `.bin` files decode to IDs 0-599. The 600-patch arena crossing fits the data but isn't logged. | OK, minor issue: the `PAN_ARCH >= 10` guard also covers v13/v14. |

Checks:
- Series: all 95 committed patches (001-096, no 086 or 091) apply cleanly on a fresh pin with no fuzz (CI run 37112739575 green). 086 was removed as unsafe. 091 is now `allocate-placeable-host-memory-from-the-dma-heap` (beta.10; the earlier `sync-placed-map-shadows` 091 was dropped). 075 was regenerated against the shadow `kbase_kmod.c` (commit 848ca8c).
- Device ICD: SHA256 `0457150b...34b98dc4` and BuildID `2afe54d4...0f7d` match the claims.
- CTS with that ICD: 438 cases (tessellation primitive_discard, sync basic events, conditional_rendering draw): 433 pass / 0 fail / 5 NotSupported. These cases don't exercise the 087 bug.
- 089 (depth clamp/clip per GS-selected viewport) is in the series (`patches/csf-v11/089-*.patch`) and verified on the G615 (`worklogs/driver-remaining/089-device-verification.md`). No CTS run.
- Review files: `tmp/review-085-088/`.

Released: beta.6 (up to 077), beta.7 (up to 082), beta.8 (up to 084), beta.9 (up to 092, tag `g615-v11-csf-v0.1.0-beta.9`), beta.10 (up to 096 including the new 091 dma-heap placed maps, tag `g615-v11-csf-v0.1.0-beta.10`), beta.11 (up to 098: device-wide TLS and grow-on-fault prerast arenas (097), only VERTEX_TILER_STARTED heap ops on kbase (098); fixes NFS:MW memory blow-up and DEVICE_LOST; tag `g615-v11-csf-v0.1.0-beta.11`, bundled in PanPlay 1.0.3), beta.12 (universal v10/v11/v12 Android ICD, v10/v12 built but untested; kbase CS register-count fallback; tag `g615-v11-csf-v0.1.0-beta.12`), beta.15 (prerelease, up to 105: heap renewal without CPU drain (102), GPU-side same-queue semaphore waits (103), worker-thread heap creation (104), kbase trace reset (105), tracebuf VA fix (101); NFS MW 24 -> 38-40 fps; tag `g615-v11-csf-v0.1.0-beta.15`), beta.14 (prerelease, experimental v9 JM; tag `g615-v11-csf-v0.1.0-beta.14`), beta.13 (prerelease, up to 100: universal v10-v14 Android and glibc ICDs; only G615 v11 tested; release name in driverInfo (099); up-front prerast arenas fix gs_viewport_depth case A (100), shared TLS stays; v12+ CS register fallback is 128; PanProbe 17/17 x3, CTS memory + sync 4520/0/13 NotSupported, CTS geometry + clipping + viewport: geometry 195 pass / 0 fail / 4 NotSupported; pipeline and dynamic-state viewport cases 177 pass / 0 fail; clipping 180 pass / 128 fail; draw `shader_viewport_index` 328 pass / 60 fail / 6 NotSupported. All 188 failures (user clip/cull distances through GS or tessellation, and `shader_viewport_index.fragment_shader_2..16`) fail the same way on the beta.10 and beta.12 binaries, so they are old bugs, not regressions.; PanPlay game tests skipped, user tests PanPlay; tag `g615-v11-csf-v0.1.0-beta.13`), beta.16 (prerelease, up to 107: software WSI present fence wait on the X11 present thread (106), up to three retired tiler heaps in flight (107); NFS race HUD 85.5 -> 90.6 fps, p99 22 -> 16 ms; PanProbe 17/17; CTS sync + memory gate has the same 56 known failures as beta.15; tag `g615-v11-csf-v0.1.0-beta.16`). See `CHANGELOG.md`.
Details: `validation/g615-v11-csf/dxvk/DX9-TRANSFORM-FEEDBACK.md`, `DX10-TESSELLATION.md`, `tmp/HANDOFF-devicelost.md`.

### Unreleased: beta.17 candidate (2026-10-06)

**Status: built, unreleased and uncommitted. beta.17b run on the G615 2026-10-06 (PanProbe 16/17, CTS no new failures; see the beta.17b bullet).** The only hardware run is a smoke on the G57 tablet (v9 JM): PanProbe 3/17 reported, 2/17 real, because `vmr_secondary` is a skip that the runner reports as PASS.

- **beta.17b rebuild (2026-10-06, supersedes the beta.17 binaries below; not hardware-tested):**
  - 116 reworked (file name kept): the db_off FPK/coverage/early-ZS overrides are dropped. With the test off, the bound lowered FS still writes SampleMask, so the DCD now uses that shader's real info (writes_coverage, no FPK). Correctness over the FPK win. Kept: the db_on EarlyFragmentTests FORCE_LATE ZS update and `depth_bounds_app_mask`.
  - New csf-v11/119: `pan_enable_vertex_pipeline_stores_atomics` now defaults to true on v10-v14, so DXVK gets FL11_1. v10-v12 use the 078 gpu_prerast route. v13+ use the upstream IDVS path (no speculative invalid-index fetches; `bifrost_nir_lower_vs_atomics`). Setting `=false` in drirc or the environment turns it off on every arch. v9 JM stays off. v13/v14 are untested (worklist item 23).
  - 117 launcher: ShmPutImage now reads the offset and bounds-checks offset+image size against the segment with 64-bit math (BadValue on overflow). The driver sends offset 0 with the full stride x height image, which is correct.
  - PanProbe: a whole-test skip prints `RESULT SKIP` and shows as SKIP (warn tone) in the UI, in the autorun line (`pass= fail= skip=`), and in summary.json and the upload manifest (per-test status and counts). The run history shows pass/fail/skip.
  - Worktree `/var/tmp/panvk/wt-beta17b`: fresh 169d6a0, 125 patches applied strictly (csf/007 `--recount`, as before).
  - Android ICD `/var/tmp/panvk/dist-beta17b/libvulkan_panfrost.so`: SHA256 `47d100da4bfb001b8a02d7d07551109dbcf1a4b9c6f5e0bf24f9dca76790edf2`, BuildID `2eced92eedb543d15585840974a7bc310bb9abeb`. glibc ICD `dist-beta17b/glibc/`: SHA256 `2b691f6531b3529647b8b9283ee32c8fc315efa72da0fdc392fdcc4503a4e9f5`, BuildID `6fa36671cdb74899e60d428da7423a0d3e28f620`. validate-binary PASS for both.
  - Host tests: `vp_runs ok`, `layout selection OK`, x11-shm ShmPutImage 3443-4029 fps at 720p and 882-941 fps at 1080p, readback ok, and the fallback reads back ok.
  - APKs in `/var/tmp/panvk/apk-beta17b/` (debug builds): `panprobe-beta17b.apk` and `panplay-beta17b.apk`, both with the beta.17b ICD bundled (same BuildID). PanPlay was built with `bundled-driver.json` set to beta.17b; the json was restored to beta.15 afterwards.
  - adb `192.168.1.34:35419`: Poco X6 Pro (2311DRK48I, duchamp, mt6897), Mali-G615 MC6 r1p3.
  - **beta.17d (2026-10-06, G615, supersedes 17b; unreleased, uncommitted):** csf-v11/120 builds a strip table for GS draws with primitive restart (the GS kernel walked back to its strip start and counted the primitives before it: quadratic, ~2.5 s per chunk loop, kbase killed the waiting VT group, 0x41/0x72; the `gs_restart_*` blocker below). csf-v11/121 keeps minDepth == maxDepth depth exact (item 35). 099 driverInfo `beta.17`. Android `dist-beta17d` sha `5d9fdb88…` BuildID `313addcb…`, glibc `e93f7ccc…`. PanProbe 36-test 1.2.3: 36/36 x3 (autorun x2 + zip run), `verify_zip.py` OK, DXVK / S4 / vkd3d compliance pass; `large_draw` 27/27; no CSF fault. CTS 52725: no new failure (17c identical to 17b; 17d one flaky sync case passes; dEQP deltazero x4 fail by 121, outside the gates). Installed PanProbe + PanPlay 1.2.3 debug with 17d bundled, PanPlay driver = 17d. [Worklog](../driver-remaining/120-gs-restart-strip-table.md), [validation](beta17-device-validation.md).
  - **G615 run (2026-10-06, PanProbe + CTS only; PanPlay not run, user tests it):** PanProbe 16/17 0 skip (autorun x6, UI x1, on the debug beta17b APK and on 1.2.3). Only `large_draw` fails: the new 112 cases `gs_restart_direct/indirect` fault the CSG (exception 0x41/0x72) and DEVICE_LOST the rest. beta.16 fails the same way with the new test, so it is an old GS-on-restart-strip bug, not a regression; every other large_draw case passes per case in chroot (beta.16 fails fan_direct/fan_indexed/cond_skip_indirect, fixed by 112). vmr_secondary 13/13, vs_viewport_index G pass, depth_bounds H/I pass. 110/118/014 lines all present (CSF 1.21, layouts 32/16, ALLOC_EX, gpu_id, BC on, aimapper mediatek). Zip 76 KB, manifest app 1.2.3, deviceFacts complete. CTS 52725 cases: 32338 / 116 fail / 20271 NS; sync + memory gate the same 56 failures as beta.16; BC, fans, depth_bounds 0 fail; shader_viewport_index the known 60. `driverInfo` still says beta.16. Installed: PanProbe 1.2.3 (7) + PanPlay 1.2.3 (10), both debug with beta.17b bundled, PanPlay driver pref beta.17b. Not done: cube FL/SHM, NFS A/B. Release blocker: fix or gate `gs_restart_*`. [Worklog](beta17-device-validation.md).

- **Full beta.17 candidate build:**
  - Worktree `/var/tmp/panvk/wt-beta17`: fresh from the pinned base 169d6a0 (= 5a07217f), `scripts/apply-patches.sh --profile g615-v11-csf` applied 124 patches, log in `/var/tmp/panvk/beta17-apply.log`. Every patch applies strictly with no fuzz. Only the old csf/007 needs `--recount` (pre-existing). No order conflicts: 114-116 apply with 110-112/117/118, and 118 is rebased onto 110.
  - Fresh-pin recheck (2026-10-06, review): a new pin of 169d6a0 in `tmp/fresh-pin-20261006/` gives `OK applied=124 profile=g615-v11-csf`, rc 0, no fuzz. The only warning is trailing whitespace in `jm-v9/002` line 1665 (pre-existing). The `--recount` claim for csf/007 was not rechecked separately.
  - New on top of beta.16 (all untracked in git):
    - csf-v11: 108, 109, 110, 111, 112, 114, 115, 116, 117, 118. There is no 113 (no patch needed).
    - android/014 (android/015 is older and already committed).
    - wsi/017.
    - jm-v9: 004 and 005.
  - 117 change: the per-image memfd (`os_create_anonymous_file` = `memfd_create(MFD_ALLOW_SEALING)` + `ftruncate`) now gets `F_SEAL_SHRINK|F_SEAL_GROW` before `xcb_shm_attach_fd`. A seal failure is ignored and logged with `mesa_logd`. This closes the SIGBUS gap noted in the 117 worklog. The patch was regenerated from `/var/tmp/panvk/wt-117`.
  - Android ICD (v6, v7, v9 JM, v10-v14): `/var/tmp/panvk/dist-beta17/libvulkan_panfrost.so`, SHA256 `144e67cc3f41a4cf8575990bedd67a3fc3f0b9aba771f1f9dccbdbcd16c82e01`, BuildID `79863519ff5b2a77cdf9c53b849ef7a06ccaac0b`. validate-binary PASS; NEEDED is the same as dist-final2.
  - glibc ICD (host clang cross-build, `/var/tmp/panvk/v9/glibc.cross.ini`; not run on a device): `/var/tmp/panvk/dist-beta17/glibc/libvulkan_panfrost.so`, SHA256 `74f2cf146434475ca38e34b6bca388d578318f564b6118c17183b48c050fef94`, BuildID `6c60858d639826a886da427109732611947f64fe`. validate-binary PASS.
  - Host tests:
    - `vp_runs ok`.
    - The 110 layout assert test (`/var/tmp/panvk/wt110-test/t.c` against the wt-beta17 headers): `layout selection OK`.
    - x11-shm (`tests/x11-shm`, now seals and asserts that a shrink fails) under headless Xwayland: ShmPutImage 3836-4364 fps at 720p and 879-911 fps at 1080p, readback ok. The `-extension MIT-SHM` fallback also reads back ok.
  - APKs in `/var/tmp/panvk/apk-beta17/`:
    - PanProbe `panprobe-beta17.apk`, built with `-PpanvkSo=dist-beta17`. AGP strips the bundled .so; the BuildID is the same.
    - PanPlay `panplay.apk`: built from the dirty main tree; its bundled driver is still the pinned release.
  - TB336FU G57 (v9 JM) smoke, done under `tablet.lock`:
    - PanProbe bundled `autorun all --ez zip true`: **3/17 reported, 2/17 real** (vertex_stores, swapchain_lifecycle 90.9 FPS). `vmr_secondary` logs `FEATURE ... vmr=0`, `SKIP no 4x sample shading or VMR without attachments`, then `RESULT PASS`: a skip, not a fix.
    - bc_decode: `textureCompressionBC=1`, all 16 formats raw and copy PASS, blit FAIL (`BC_DEVICE_FAILS=16`; known v9 sampling issue). `gpu_prerast_slice` replay fault (JM atoms 35/36, event `0x58`) unchanged.
    - The vendor mapper still does not load in the app (`mapper load failed (2 candidate names, first=mediatek)`, mapper@4.0 impl not accessible for the clns namespace, isDeclared SELinux-denied for `mapper/common` and `mapper/mediatek`); the swapchain passes only through `cpu-linear-probe`.
    - logcat:
      - `backend=cpu-linear-probe` 9/9 LINEAR.
      - `EXEC_INIT failed` 0.
      - `kbase: JM driver, uAPI version 11.38, page_size=4096`.
      - 118 lines: `gpu_id 0x90930010 ... arch v9 model Mali-G57 texture_features 0xf7fe03fe ...` and `BC emulation on (native compressed mask 0xf7fe03fe)`.
      - No crash.
    - The zip was built locally (59.6 KB). It contains `logcat.txt` and the manifest `deviceFacts` (kbaseUapi, gpuId, textureFeatures, bcEmulation, pageSize, mapperLibs, driverLines).
    - deqp `memory.allocation.basic.*`: 102/102.
    - PanPlay D3D11 cube (x86_64) with this ICD as an imported driver:
      - The session logs are complete (dxvk d3d11/dxgi, mesa-panvk, device-facts, xserver, logcat). The Mesa kbase and 118 lines are present and there is no crash.
      - DXVK rejects v9 (`Device does not support Vulkan 1.3`), so nothing presents. As a result the X11 SHM path cannot be reached on the tablet, and `xserver.log` has 0 shm lines.
    - Restore, all sha-checked:
      - Both original APKs reinstalled.
      - PanProbe imported driver c0e549..., selection BUNDLED.
      - Launcher prefs: driver beta.15.
      - `/data/local/tmp/v9cts` driver 8a2849....
      - The temporary driver and shortcut were removed.
    - Artifacts: `/var/tmp/panvk/apk-beta17/` (autorun.txt, logcats, zip, panplay-logs).
  - G615 checklist (not connected): run each worklog's G615 section with this ICD:
    - [108](../driver-remaining/108-bc-emulation-decision.md)
    - [109](../driver-remaining/109-v12-viewport-depth.md)
    - [110](../driver-remaining/110-kbase-csf-uapi-layouts.md), [110 exec](../driver-remaining/110-exec-init-order.md)
    - [111](../driver-remaining/111-event-cleanups.md)
    - [112](../driver-remaining/112-prerast-gaps.md), [113](../driver-remaining/113-tess-pg-count.md)
    - [114](../driver-remaining/114-secondary-vmr-segments.md)
    - [115](../driver-remaining/115-viewport-run-primitive-id.md)
    - [116](../driver-remaining/116-depth-bounds-fpk.md)
    - [117](../driver-remaining/117-x11-shm-present.md)
    - [014](../driver-remaining/014-vendor-neutral-mapper.md)
    - Gate after all of them: PanProbe 17/17, CTS sync + memory has only the 56 known failures, NFS HUD at least 90 fps.
  - Earlier partial builds (`dist-final`, `dist-final2`, `dist-112`, `dist-117`, `dist-110`, `dist-114`) are superseded: every patch they carried is in the full candidate. Their G57 results (1/17 with dist-final; 2/17 with dist-final2, the first `cpu-linear-probe` pass at 90.6 FPS) and per-patch to-do lists are in items 4-10, 13-20 and the worklogs.

#### beta.17 review (2026-10-06)

Patch review, claim check and fresh-pin check, done directly (no model bridge). OK = no issue found; minor = documented limit or follow-up. **No blocking findings.** Follow-ups are items 21-27.

| Patch | Grade | Findings |
|---|---|---|
| 108 | OK | Emulates unless all 10 BC formats are native. The G57 mask `0xf7fe03fe` (BC1-BC3 native) proves the partial case. Minor: `PANVK_DEBUG=no_bc_emul` sends BC to the native path even when unsupported (debug only). |
| 109 | OK | VIEWPORT_LOW words 2/3 are min/max depth in `prepare_vp`. Minor: no v12-v14 hardware run; the 092 LD_TILE depth-bounds emulation is unvalidated on v12+. |
| 110 + jm-v9/005 | OK | Version-ordered layouts with EINVAL/ENOTTY fallback; G615 (1.21) keeps 32-byte and G720 (>= 1.25) 112-byte as first try; heap init 16 then 24 bytes. `KBASE_MMAP_HANDLE` scales the 4K-encoded handles; USER_IO offsets use page_size. The ALLOC_EX -> ALLOC fallback is only taken before the first success; races are benign. Minor: 16 KiB pages untested anywhere; single-page mmaps keep 4096 and rely on mmap rounding; `alias_create` asserts size % page_size (ring sizes satisfy it). |
| jm-v9/004 | OK | EXEC_INIT before JIT, 4 GiB zone. Minor: EPERM/EINVAL now only `mesa_logd`, which also hides real failures; not run on CSF hardware. |
| jm-v9/005 | OK | Also moves a misplaced jm-v9/001 hunk: the atoms mutex destroy had landed in the CSF csif error path because 110 shifted offsets. Series-order fragility: `git apply` accepts offsets silently. |
| 111 | OK | `SetEvent`/`ResetEvent` no longer return DEVICE_LOST. |
| 112 | minor | Fan window math (`s += win - 1`, shared hub, `prim_base = s - 1`) and index offsets match the chunk code. The conditional skip zeroes the `cd.in_draw` copy, not app memory; registers do not collide. Minor: CPU instance splits (> 2048 instances) and per-instance fan windows shift `gl_BaseInstance` (pre-existing, now also hit by fans); indexed fans with restart and indirect fans over the arena still unhandled. |
| 114 | minor | Segment chunks stay valid (`cs_builder_fini` does not free GPU chunks); `CmdExecuteCommands` order is right; the 086 failure modes are avoided. Minor: u_trace of earlier segments is not appended; more `cs_call`s per secondary. |
| 115 | minor | Documented limits only: IDs wrap past 65535, LINE/POINT polygon mode gives 0, GPU chunks restart the ID. Record masking is safe (records < 65536). |
| 116 | minor | Blob serialize/deserialize order matches. With the test off the lowered FS still writes SampleMask (`SampleMaskIn & app mask`) while the DCD claims no coverage write and allows FPK. Hardware behaviour is unverified: needs the G615 `depth_bounds` H/I cases and CTS. The EFT FORCE_LATE path looks right; side effects of out-of-bounds EFT fragments still run (documented). |
| 117 | minor | The seal works (the memfd is `MFD_ALLOW_SEALING`, Mesa `src/util/anon_file.c:194/198`); GetGeometry after ShmPutImage orders segment reuse; pacing is fine. Minor: damage rects with negative x/y are skipped, not clipped. Launcher side (pre-existing, now reachable with memfd segments): the ShmPutImage handler ignores `offset` and does not bounds-check the source rect against the segment size; AttachFd detaches an existing xid without an owner check. The uncommitted `Containers.kt` diff also carries unrelated process-kill / `wineserver -w` lifecycle changes from another session. |
| 118 | OK | Logging only. |
| android/014 + wsi/017 | minor | The probe writes only driver-owned AHBs; every error path clears the owned pointer. Minor: the HIDL mapper4 raw-vtable backend (slots 14/15/23) is unreachable from app namespaces and carries ABI risk. On the stock G615 (vendor API 34; Android's own Gralloc5 fails to load `mapper.mediatek.so` in the app) success depends on gralloc giving linear RGBA buffers: unverified. |


## What's left for DXVK (driver)

3. DXVK FL11_1 re-check: **done (static + existing G615 logs).** DXVK 3.1.1 needs `logicOp` + `vertexPipelineStoresAndAtomics` for 11_1; both are on for v10-v12. G615 launcher logs (2026-10-03) already show `Maximum supported feature level: D3D_FEATURE_LEVEL_11_1`; the FL 11_0 in the done table predates 078. Expected: v10 11_1 (after 108), v11/v12 11_1, v13/v14 11_0 (VPSA is drirc opt-in). 12_0 is blocked by sparse/tiled resources (NO-GO on kbase), 12_1 by fragment shader interlock. [Worklog](../driver-remaining/fl11_1-recheck.md).
  - Tester data (2026-10-06) confirms it: stock G615 cubes get FL11_1, v13 Immortalis-G925 cubes and Core Keeper get FL11_0 (`vertexPipelineStoresAndAtomics : 0`). Open question in item 23.
4. `shaderOutputViewportIndex`: v12+ bit on with run splitting since 109 (untested on hardware; see item 16). **FS `gl_PrimitiveID` across viewport runs: csf-v11/115, in the beta.17 candidate (unreleased, not run on G615).** The run builder packs the per-draw primitive ID into index bits 16+, and the passthrough VS outputs it. Limits: IDs wrap past 65535, polygon-mode LINE/POINT gets 0, GPU-chunked draws restart the ID per chunk. [Worklog](../driver-remaining/115-viewport-run-primitive-id.md).
5. `depthBounds`: v12+ bit on since 109 (untested on hardware; see item 16). **csf-v11/116, in the beta.17 candidate (unreleased, not run on G615).** Review: the FPK/coverage mismatch needs G615 proof (item 21). A lowered FS keeps the app's FPK, coverage and early-ZS LUT while the test is off (dynamic enable, DXVK). With EarlyFragmentTests and the test on, the ZS update moves after the FS, so the test reads the stored depth. Limits: an EFT FS that also discards, writes a mask or uses a2c keeps the 092 behaviour; side effects of out-of-bounds EFT fragments still run. [Worklog](../driver-remaining/116-depth-bounds-fpk.md).
6. Prerast limits: **csf-v11/112, in the beta.17 candidate (unreleased, not run on G615).** Direct fans over the cap are split (hub + window, exact prim IDs). Fan and tess instance grids are capped at 2048. Chunked draws skipped by conditional rendering no longer lower or count PG. The rasterizer-discard and GS-primitive-ID items were checked: they are not gaps (XFB/PG/pstats run on the compute side, prim IDs are exact), and they are now tested. Still open: indexed fans with restart and indirect fans over the cap; CPU instance splits shift `gl_BaseInstance`/divisors (pre-existing). [Worklog](../driver-remaining/112-prerast-gaps.md).
7. `variableMultisampleRate`: **csf-v11/114, in the beta.17 candidate (unreleased, not run on G615).** Attachment-less secondaries cut their CS streams per sample count change. The primary splits its render context between the segments, so the secondary does no context work (the 086 blockers do not apply). An attachment-less pass that suspends ends its context, and the resumed part starts a new one. The unsafe 086 stays removed. [Worklog](../driver-remaining/114-secondary-vmr-segments.md).
8. Exact tessellation primitives-generated count: **already exact (113 check, no patch; nothing to ship).** count/vpp per aligned range, conditional skip zeroed by 087. APK `large_draw` now checks PG for isoline, triangle and instanced tess and for the cond-skip case (untested on G615). [Worklog](../driver-remaining/113-tess-pg-count.md).
9. X11 present: DRI3 / MIT-SHM path instead of CPU `PutImage`; FIFO is not vsync-paced. Present fence wait moved off the submit thread in 106. **csf-v11/117, in the beta.17 candidate (unreleased, not run on G615; the G57 cannot reach it because DXVK rejects v9). Launcher review findings in item 22.** ShmPutImage from memfd segments via MIT-SHM 1.2 AttachFd (falls back to PutImage), sw FIFO paced to `MESA_VK_X11_SW_REFRESH_HZ` (launcher sets display max refresh). Launcher X server gains MIT-SHM 1.2 AttachFd. DRI3 not done (builtin server has none). Host Xwayland: ShmPutImage 2.4-2.5x PutImage at 720p, 1.5-1.7x at 1080p. See `worklogs/driver-remaining/117-x11-shm-present.md`.
10. Optional cleanups: **csf-v11/111, in the beta.17 candidate (unreleased).** `SetEvent`/`ResetEvent` no longer return `DEVICE_LOST`: a failed CS_EVENT_SIGNAL is logged and the event word is already written. 085/087/088 are not gated, and the Sol review agrees:
  - They use only the kbase ABI, cs_builder ops whose encodings match in v12-v14 genxml, and NIR.
  - Tessellation, GS and conditional rendering are exposed on v10-v14.
  - [Worklog](../driver-remaining/111-event-cleanups.md).
11. **Next driver task: asynchronous kbase queue submission. STATUS: NOT STARTED.**
  - Beta.15/16 removed the CPU waits around submit (102-107); NFS is now bound by the game main thread / wineserver IPC. The ring kick already returns immediately; asynchronous submission work remains to be scoped.
12. **Mali v9 (Valhall JM, G57/G68/G77/G78): experimental since beta.14, finish to PanProbe 17/17.**
  - G57 MC2 tablet (kbase JM uAPI 11.38, measured by the beta.17 candidate; gpu_id 0x90930010; `TEXTURE_FEATURES[0] = 0xf7fe03fe`, BC1-BC3 native): PanProbe 1/17 on beta.14/15/16; beta.17 candidate 2/17 real (adds `swapchain_lifecycle` via `cpu-linear-probe`; `vmr_secondary` only skips, item 27). DXVK rejects Vulkan 1.1; needs Vulkan 1.3 + GS, multiViewport, fillModeNonSolid and BC. Details: [docs/universal/v9-jm/README.md](../../docs/universal/v9-jm/README.md); earlier tests: `worklogs/driver-remaining/101-v9-jm-test-driver.md`, `validation/v9-jm/` (CTS smoke 4/6, mapping 102/102, basic sync 21 pass / 8 NotSupported, simple draw 4/4).
  - JM replay: DATA_INVALID_FAULT (event 0x58) in `gpu_prerast_slice` "replay" (same command buffer submitted twice). Suspected GPU-written malloc-job payload not restored; 4-8 h diagnosis + 1-2 days fix. VMR secondaries count only sample 0, not samples 1-3; update `fb.nr_samples` / `evaluate_per_sample` (1-2 days + mixed-rate validation).
  - Feature work: audit/report Vulkan 1.3 (1-3 days); port compute pre-raster to JM job chains for GS, vertex stores, multi-viewport, clip/cull, fill mode and large draws (15-30 working days); tessellation (5-15 days), XFB (3-7 days), pipeline stats, depth bounds and tess cond state still open. Earlier phase estimates were too small; timestamps report zero valid bits, and API 1.1 hides driverInfo properties.
  - G77 MC9 phone (POCO 21061110AG, MT6891, 4.14 custom kernel, Android 13, gpu_id 0x90800011; records `c853b7b6`/`40359b15`, direct tester zips): PanProbe 0/17 on beta.15. kbase opens, but `PAN_PROD_ID(9, 0, 0)` has no model row, so no physical device is created (item 19; still open in beta.17).
  - BC, mapper and EXEC_INIT gaps are items 14, 15 and 17 (all patched in the beta.17 candidate; BC blit on JM still fails). Risks: GPU-side indirect job patching, replay correctness, fork maturity and one test device; full PanProbe 17/17 remains the gate.
13. **Upload zip log gaps (apps, next app release).** **Status: csf-v11/118 (driver) + uncommitted app changes, in the beta.17 candidate; unreleased.** Verified on the G57 tablet only: the zip carries kbase uAPI (JM 11.38), gpu_id, TEXTURE_FEATURES, BC decision, mapper libs, run-window logcat and DXVK logs. G615 not rechecked; the worklog's G615 checklist has not run. [Worklog](../apps/upload-logs.md). (Found 2026-10-05 by checking upload ids 28/29, 1.2.2.)
  - Done in code: run-window logcat (all buffers), DXVK logs via `DXVK_LOG_PATH`, head+tail log caps (4 MiB), `deviceFacts` in the manifest, and the `bc_decode` no-device SIGBUS/SIGSEGV fix (not rerun on a no-device phone).
  - Still open: record which Mesa debug variables were set (item 30); report skipped tests as SKIP, not PASS (item 27).

New driver gaps from tester data ([docs/universal/README.md](../../docs/universal/README.md)); file:line references below use the beta.16 Mesa tree.

14. **BC emulation decision (v9/v10).** Native BC1 reported by kbase disables emulation, leaving `textureCompressionBC=false`; DXVK rejects G610. The partial native mask is inferred (uploads omit `TEXTURE_FEATURES`).
  - `src/panfrost/vulkan/panvk_physical_device.c:1815`, `:1837`; `src/panfrost/vulkan/panvk_vX_physical_device.c:294`, `:338`. Emulate unless full native BC mask is present; effort 4-8 h incl. validation. [Index](../../docs/universal/README.md).
  - **Status: patched, unreleased.** `patches/csf-v11/108` emulates unless all ten BC formats are native. G57 deqp: `textureCompressionBC` 0 -> 1, format_properties.bc* 16/16. v10 G610 needs tester rerun. v9 compressed-texture image checks fail for native ETC2/ASTC too (separate issue). [Worklog](../driver-remaining/108-bc-emulation-decision.md).
  - 2026-10-06: the beta.17 candidate measured the G57 mask, `TEXTURE_FEATURES[0] = 0xf7fe03fe` (BC1-BC3 native, BC4-BC7 not), which proves the partial-native inference. A third G610 device now hits this: vivo V2284A (MT6896Z/CZA, 5.10.233, `5b799ebb`, beta.15), user D3D game exit 3 after DXVK skips the adapter.
15. **Vendor-neutral gralloc mapper.** Hard-coded MediaTek stable-C mapper5 causes `vkCreateSwapchainKHR` / AHB import `VK_ERROR_INVALID_EXTERNAL_HANDLE` on stock ROMs and vendor API < 34; Pixel/Tensor reports are consistent with this gap (cause not yet proven).
  - `src/util/u_gralloc/u_gralloc_fallback.c:83`, `:427`; `patches/android/013-vendor-mapper-metadata.patch`; `src/vulkan/runtime/vk_android.c:152`. Vendor-neutral discovery + mapper4: 24-48 h; quick configurable mapper names: 4-8 h (does not cover mapper4-only vendors). [Index](../../docs/universal/README.md).
  - **Status: patched, unreleased.** `patches/android/014` (renamed from 109) adds:
    - AIMapper v5 discovery by name. Names come from `PANVK_MAPPER_NAMES`, then a scan of the vendor/odm hw dirs, then `ro.hardware.gralloc`, then `mediatek`.
    - A HIDL IMapper@4.0 backend that calls the impl through its raw aarch64 ABI. libhidlbase is not usable from the NDK or an app namespace.
    - Guessed layouts are still refused.
    - Revision (2026-10-05): inside a real app the vendor mapper never loads on vendor API < 34 (clns-N namespace blocks libvndksupport and the vendor impl; exported `sphal` namespace not reachable; isDeclared SELinux-denied). New fallback `cpu-linear-probe` in `vk_android_get_ahb_layout`, only for AHBs the driver just allocated (WSI swapchain marks them, new `wsi/017`) with CPU read usage and 32bpp single plane. It writes cookies through its own dma-buf mmap and reads them back through `AHardwareBuffer_lock`. Only a 9/9 match gives LINEAR; anything else is still refused.
  - u_gralloc test: G57 tablet (vendor API 33, Arm gralloc4, AFBC) and SM8650 QTI mapper4 return full metadata (standalone binary only).
  - In-app test, TB336FU panvk-test (imported ICD `dist-final2`): `backend=cpu-linear-probe 1520x320 ... match=9/9 -> LINEAR`; `swapchain_lifecycle` PASS (90.6 FPS); `autorun all` 2/17 (was 1/17). Same result with the full beta.17 candidate (90.9 FPS). Dev G615 (AIMapper path) not re-run; that path is unchanged. [Worklog](../driver-remaining/014-vendor-neutral-mapper.md).
  - Tester data (2026-10-06): every stock G615 (2311DRK48G, LAVA LXX525, Infinix X6857 incl. the first beta.16 upload `96a133dc`, Infinix X6876) still fails `swapchain_lifecycle` on beta.15/16. The stock logs show `isDeclared for mapper/mediatek ... SELinux denied` and `Gralloc5: Failed to load mapper.mediatek.so`: Android's own client cannot load the mapper in the app either. So on these ROMs only `cpu-linear-probe` can help, and only if gralloc hands out linear RGBA. Unverified.
16. **v12+ viewport depth and feature gates.** `depthBounds` and `shaderOutputViewportIndex` require `PAN_ARCH < 12`; per-viewport depth runs are absent. G720 MC7 PanProbe 14/17 in all six runs (beta.14 and five beta.15 runs on 2026-10-05/06; fails gs_viewport_depth, vs_viewport_index, depth_bounds); v13 Immortalis-G925 MC12 on two devices (`e74f908f`, `feffc979`, beta.15) 14/17 with the same three fails. v14 G1-Ultra has not reached these tests (item 20).
  - `src/panfrost/vulkan/panvk_vX_physical_device.c:330`, `:443`; `src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c:4388`, `:4502`, `:4709`. Effort: depth runs 16-32 h + viewport index 4-8 h + depth bounds 8-16 h. [Index](../../docs/universal/README.md).
  - **Status: patched, unreleased, untested on v12/v13.** `patches/csf-v11/109` writes each viewport run's depth clamp to the v12+ VIEWPORT_LOW pair (SR 46/47 = Viewport Min/Max Depth) instead of LOW/HIGH_DEPTH_CLAMP, enables the run split on v12-v14, and reports `depthBounds` and `shaderOutputViewportIndex` on v10+. Universal ICD (v10-v14) builds; v10/v11 object code unchanged except `__LINE__` constants; Sol review: no blocking issues. Tester gate: PanProbe `gs_viewport_depth`, `vs_viewport_index`, `depth_bounds` (plus `multi_viewport`, `geometry`, `tessellation`) on G720 and G925. [Worklog](../driver-remaining/109-v12-viewport-depth.md).
17. **EXEC_INIT ordering.** Status: FIXED, unreleased (`patches/jm-v9/004-init-exec-va-before-jit.patch`, 2026-10-05).
  - Cause (kbase source): JM and pre-1.9 CSF return -EPERM from `kbase_region_tracker_init_exec` once `kctx->jit_va` is set. CSF >= 1.9 makes EXEC_INIT a no-op, so G615 and G720 are unchanged.
  - Fix: EXEC_INIT now runs before JIT_INIT, and JM gets the full 4G zone (the 1024-page request was dropped).
  - G57 tablet: the EPERM warning is gone (0/18 logs), GPU_EX BOs now land in EXEC_VA, PanProbe stays at 1/17, `memory.allocation.basic` 102/102, `api.smoke` no worse. Same with the full beta.17 candidate (no `EXEC_INIT failed`).
  - v10 not run. The new 5.10 G610 log (V2284A, beta.15) has no EXEC_INIT warning, but it never reached `vkCreateDevice`.
  - Details: `worklogs/driver-remaining/110-exec-init-order.md`.
  - [Index](../../docs/universal/README.md) §3.
18. **Exercise v10 old-CSF device / queue / heap paths.** Partly answered. A G610 MC4 on 5.15 android13 (`854bef5b`, beta.15) runs PanProbe 15/17: `vkCreateDevice`, queue groups, tiler heaps and shaders work, with no EXEC_INIT warning (so its CSF uAPI is likely >= 1.9; not logged). The only failures are bc_decode (item 14) and swapchain mapper (item 15). Still open: both 5.10 G610 MC6 devices (23054RA19C, and V2284A new on 2026-10-06) have only PanPlay data that stops at DXVK adapter selection; they need a PanProbe run with logcat.
  - Check 112-byte / 32-byte / missing 40-byte queue-group layouts, legacy heap init and EXEC zone (`src/panfrost/lib/kmod/kbase_kmod.c:1391-1410`). [Index](../../docs/universal/README.md).
  - **Status: patched, unreleased (csf-v11/110).** All three group-create layouts and both heap-init layouts are tried in a version-defined order; the first one the kernel accepts is logged once.
  - Tester check (G610 PanProbe logcat):
    - `kbase: CSF driver, uAPI version 1.x, page_size=4096`
    - `kbase: queue_group_create layout=32 bytes`
    - `kbase: tiler_heap_init layout=16 bytes`
    - `kbase: mem_alloc via ALLOC_EX`
    - no `layout=... failed`
  - [Worklog](../driver-remaining/110-kbase-csf-uapi-layouts.md).
19. **Mali-G77 gpu_id missing from model table (v9).** `0x90800011` -> `PAN_PROD_ID(9, 0, 0)`. The v9 rows are only `(9,0,1)`/`(9,0,3)` G57 and `(9,2,4)` G68. The device is skipped as INCOMPATIBLE_DRIVER, and the "Unknown gpu_id" text is suppressed in release builds. PanProbe shows only "FAIL no Mali" (0/17, beta.15).
  - `src/panfrost/model/pan_model.c:87-91`, `:148`; `src/panfrost/vulkan/panvk_physical_device.c:1345`; `src/panfrost/vulkan/panvk_instance.c:227`; `src/vulkan/runtime/vk_log.c:114`. Add a G77 row (tile-buffer sizes and rates to verify; G57 values as a first guess) and a `mesa_logw` for unknown gpu_id: 1-2 h + tester rerun. After that the G77 reaches the item 12 v9 limits. App side: PanProbe `bc_decode` SIGSEGV with no device (item 13). [Index](../../docs/universal/README.md).
  - **Status: NOT fixed in the beta.17 candidate.** The wt-beta17 model table still has only `(9,0,1)`/`(9,0,3)` G57 and `(9,2,4)` G68 (`pan_model.c:87-91`). 118 adds the log (`mesa_loge("panvk: Unknown gpu_id ...")`, `panvk_physical_device.c:1356` in wt-beta17), so a rerun would show the reason, but the G77 still gets 0 devices. No new G77 data.
20. **v14 `KBASE_IOCTL_MEM_ALLOC_EX` returns ENOTTY.** Mali-G1-Ultra MC12 (vivo V2515, MT6993, `0xe8800010` -> G1-Ultra row, 6.12 android16 with 4 KiB pages), 8 PanPlay runs on beta.15. The device enumerates and DXVK sees `textureCompressionBC = 1`. The first BO allocation in `vkCreateDevice` then fails (`kbase: KBASE_IOCTL_MEM_ALLOC_EX failed: Inappropriate ioctl for device`), so DXVK gets `VK_ERROR_OUT_OF_DEVICE_MEMORY`, also in safe mode. No queue, heap or shader has run on v14.
  - `src/panfrost/lib/kmod/kbase_kmod.c:1909-1927` uses EX for every CSF uAPI >= 1.9 with no fallback. ENOTTY means this kernel does not recognise ioctl 59 at that size. The kbase uAPI version is only a debug log (`kbase_kmod.c:1344`), so the exact ABI change is unknown. `pan_kmod.h:605-612` also accepts unknown higher major versions.
  - Fix: retry ENOTTY once with the legacy `KBASE_IOCTL_MEM_ALLOC` (`kbase_kmod.c:1932`; no fixed-VA flags are used), cache per device, and log the uAPI version with `mesa_logi`. [v14 doc](../../docs/universal/v14/README.md).
  - **Status: patched, unreleased (csf-v11/110), untested on v14.**
    - Public kbase (CSF 1.10-1.31) has `MEM_ALLOC_EX` at nr 59 with 64 bytes and no number change, so this kernel is newer or vendor-modified (exact cause unknown).
    - Public kernels implement `MEM_ALLOC` as ALLOC_EX with `fixed_address = 0`, so the fallback builds the same request.
    - On ENOTTY/EINVAL before any allocation has succeeded, the driver falls back and sticks to the first path that works.
    - Tester check (G1-Ultra PanPlay/PanProbe logcat): `kbase: CSF driver, uAPI version X.Y`, `kbase: KBASE_IOCTL_MEM_ALLOC_EX failed: ... trying KBASE_IOCTL_MEM_ALLOC`, `kbase: mem_alloc via ALLOC`, then the queue_group_create/tiler_heap_init layout lines.
    - If group create or heap init also fail with ENOTTY, the kernel's CSF uAPI differs more widely; send the version line.
    - No new v14 data on 2026-10-06.

beta.17 review follow-ups (2026-10-06; details in the review table above). None blocks the release; each needs a check or a small fix.

21. **116 FPK/coverage mismatch.** With the depth bounds test off, the lowered FS still writes SampleMask while the DCD says no coverage write and allows FPK. Prove on the G615 (`depth_bounds` H/I, CTS `pipeline.*depth_bounds*`) before release; if FPK drops samples, clear FPK when the lowered FS writes the mask.
22. **117 launcher MIT-SHM hardening (app side, pre-existing, now reachable).** The ShmPutImage handler ignores `offset` and does not bounds-check the source rect against the segment size; AttachFd detaches an existing xid without an owner check. Negative-origin damage rects are skipped, not clipped (driver side, minor). Also split the unrelated process-kill / `wineserver -w` changes out of the `Containers.kt` diff before committing.
23. **VPSA / FL11_1 on v13 and v14 (open question).** `vertexPipelineStoresAndAtomics` is a drirc opt-in on v13+ (`panvk_vX_physical_device.c:345-348` in wt-beta17), so DXVK caps v13 at FL11_0, confirmed by tester data. A third-party driver on the same G925 exposes VPSA and gets FL11_1, but that does not prove it is correct through PanVK's v13 IDVS path. Next: a v13 PanProbe `vertex_stores` run plus CTS vertex atomics with `enable_vertex_pipeline_stores_atomics`, then decide the default. v14 is expected to behave the same.
24. **112 `gl_BaseInstance` on fan instance splits.** CPU instance splits (> 2048 instances) and per-instance fan windows shift `gl_BaseInstance` (pre-existing for other topologies, now also for fans). Indexed fans with primitive restart and indirect fans over the arena are still unhandled.
25. **Small follow-ups.** 114: u_trace of earlier secondary segments is not appended. jm-v9/004: EPERM/EINVAL from EXEC_INIT is now only `mesa_logd`, which hides real failures (log at warning once when it is not the "zone exists" case). android/014: the HIDL mapper4 raw-vtable backend is unreachable from app namespaces and carries ABI risk (drop it or keep it for standalone tools only).
26. **Series-order fragility.** 110 shifted offsets so that a jm-v9/001 hunk (atoms mutex destroy) applied into the CSF csif error path; jm-v9/005 moves it back. `git apply` accepts offsets silently, so `apply-patches.sh` should fail on any offset, not only on fuzz.
27. **PanProbe reports skips as PASS.** The G57 beta.17 "3/17" counts `vmr_secondary`, which logs `SKIP ... vmr=0` then `RESULT PASS`. Report SKIP separately (app side) and count it as not run.

New findings from tester data (2026-10-06, D1 ids 54-90).

28. **Immortalis-G925 named "Mali-G725".** The `(13, 8, 0)` row is still "G725" in wt-beta17 (`pan_model.c:113`). Cosmetic: rename or add an Immortalis-G925 alias (< 1 h). The third-party driver's deviceID `0xd8300010` is the same GPU (low revision bits differ), not a new gpu_id.
29. **v13 D3D8/D3D9 cubes run at about 1/3 the frame count of D3D10/D3D11.** Xiaomi 15T Pro (G925): D3D8/D3D9 cubes log 420-450 frames in 27 s, D3D10/D3D11 1360-1400. The stock G615 has no such gap (D3D8/D3D9 1132-1195, D3D10/D3D11 1282-1332). Cause unknown; needs a `DXVK_HUD=full` run on v13.
30. **PanProbe `large_draw` timeout under Mesa shader logging (harness).** Two stock G615 runs (`2161a553`, `0b225021`) wrote 110-117 MB of shader disassembly and hit the runner's 60 s limit after 11 passing cases. Not a driver bug. Raise the limit when debug logging is on, or warn testers, and record the Mesa debug variables in the upload manifest (app side).
31. **First beta.16 tester upload.** Infinix X6857 `96a133dc` (imported `.so`): 16/17, only the stock mapper failure. No beta.16 regression seen.
32. **First v13 game run.** Core Keeper (Unity 6, D3D11) on the G925 creates an FL11_0 device and reaches Steamworks init with no driver error; assembly load took 197 s; user stopped it at 233 s. Gameplay not reached.
33. **`robustBufferAccess2` on v10 (G710/G610/G510).** Status: NOT STARTED. `VK_EXT_robustness2` / `VK_KHR_robustness2` `robustBufferAccess2` is gated `PAN_ARCH >= 11` in the PanVK physical device code, so v10 reports it as false. It is the only hard Bachata S4 requirement that v10 misses ([requirements](../../docs/bachata-s4-vulkan-requirements.md)). Effort: M. Needs a v10 bounds-check path for UBO/SSBO descriptor ranges, then CTS `dEQP-VK.robustness.robustness2.*` buffer cases on a G610 before the gate drops to 10.
34. **Tess-fed GS `gl_PrimitiveIDIn` is the triangle ordinal, not the patch id (beta.17c).** Status: NOT A DRIVER BUG (2026-10-06): that run bundled a stale ICD (item 36); with the real 17c/17d ICD `gs_tess_primitive_id` passes. PanProbe `gs_tess_primitive_id` (2026-10-06, Poco X6 Pro G615, beta.17c) fails every case: `direct ids bad=24`, `indirect`, replays, `chunk ids bad=3600`. Patch 088 (gate for this test) is missing or regressed in the beta.17c series. Log: `/var/tmp/panvk/phase2/gs_tess_primitive_id.log`. Affects D3D11 hull/domain + GS games.
35. **`minDepth == maxDepth` viewport is not flat.** Status: FIXED in beta.17d by csf-v11/121 (only a non-zero range is widened; a zero range writes exact minDepth and does not depth clip, so the 4 dEQP `inverted_depth_ranges.nodepthclamp_deltazero` cases fail, not in the gates). `depth_stencil` 10/10 on G615. [Worklog](../driver-remaining/120-gs-restart-strip-table.md). `depth_stencil` `d24s8/d32s8_viewport_zero_depth_range` read 0.5 - 1.83e-5 instead of exactly 0.5. `MIN_DEPTH_CLIP_RANGE` (37.7e-6) widens a zero depth range in `panvk_vX_cmd_draw.c:833` and `:890-901`. D3D draws with MinZ == MaxZ (sky boxes, HUD at fixed depth, depth-equal passes) get the wrong depth. Fix: keep the widening for the clip range only, and do not apply it to the viewport depth transform.
36. **beta.17c PanProbe regressions on v11 (G615).** Status: NOT A REGRESSION (2026-10-06). The APK was built without `-PpanvkSo` and bundled the stale default `build/android-dxint-dist/libvulkan_panfrost.so` (BuildID `587c2bb6`, Mesa without the series) under a beta.17c `bundled-driver.json`. Rebuilt with `-PpanvkSo=/var/tmp/panvk/dist-beta17d/libvulkan_panfrost.so`: 36/36 x3 and `verify_zip.py` OK. Original report: 2026-10-06 suite 29/36: `depth_bounds` (`depthBounds not reported`), `vs_viewport_index` (`shaderOutputViewportIndex not reported`, then `CreateGraphicsPipelines r=-13`), `large_draw` (`points_indirect bad=134466 written=65536 generated=200000`, then `QueueSubmit r=-4` DEVICE_LOST for the rest), `vmr_secondary` (`sec_4x bad=3` and five other secondary/4x cases), and also `gs_viewport_depth` (all three cases wrong depth: `depthL=0.375 expL=0.25`). DXVK/vkd3d/S4 compliance still PASS (both features are soft). Zip: `/var/tmp/panvk/phase4/panprobe-20261006-202914.zip`. Compare with a beta.16 run on the same device, then bisect.
37. **`robustImageAccess2` on v10+.** Status: WIP (dx-ria2). vkd3d-proton device creation requires it; vkd3d compliance lists it as `proton: robustImageAccess2` missing on G615. Blocks any vkd3d-proton run.
38. **`denormBehaviorIndependence = NONE`** (`panvk_vX_physical_device.c:1069`). Status: OPEN. vkd3d-proton needs denorm independence + FP32 FTZ/preserve for SM 6.2, so it caps at SM 6.0 (`proton: SM6.2` soft missing). Check whether the hardware allows per-bit-size denorm modes and report `INDEPENDENCE_32_BIT_ONLY` or `ALL`.
39. **`maxFragmentDualSrcAttachments = 8` is suspicious.** Status: OPEN. Most drivers report 1. Write a test that uses dual-source blend with attachments 1-7 (not only 0); if it fails, report 1.

## Open problems

- Tester data (2026-10-06: 86 D1 uploads, ids 2-90, + 2 direct tester zips = 88 records from 19 devices; [device list](../../docs/universal/DEVICES.md), [index](../../docs/universal/README.md)):
  - v9: G57 1/17 (beta.17 candidate 2/17 real, dev smoke). G77 0/17, gpu_id 0x90800011 not in the model table (item 19).
  - v10: three G610 devices. Both 5.10 MC6 phones (23054RA19C, new vivo V2284A) stop at DXVK adapter selection on BC (item 14). The 5.15 MC4 scores 15/17 (BC, mapper).
  - v11: eight G615 devices. Dev 17/17. Stock ROMs 16/17 (mapper, item 15), incl. the first beta.16 upload. Stock 2311DRK48G runs D3D8-D3D11 cubes on x86/x64/ARM64EC to exit 0 at FL11_1.
  - v12: G720 14/17 in six runs (item 16).
  - v13: two Immortalis-G925 devices, 14/17 (item 16). First DXVK runs: cubes exit 0 at FL11_0 (item 23), D3D8/D3D9 slow (item 29), Core Keeper reaches Steamworks init (item 32).
  - v14: G1-Ultra fails `vkCreateDevice` on `MEM_ALLOC_EX` ENOTTY in all 8 runs (item 20; no new data).
  - Still-unseen gpu_ids: G710, G510, G310, G620, Immortalis-G720, G68, G78, G625, plain G725, G1-Premium, G1-Pro. No tester has run the beta.17 candidate.

- CTS (beta.13, G615): 188 old failures, same on beta.10 and beta.12: `clipping.user_defined.clip_distance*`/`clip_cull_distance*` through GS or tessellation (128), and `draw.*shader_viewport_index.fragment_shader_2..16` (60). 16 of the clip cases pass when run alone, so some state leaks between cases. Evidence: `validation/driver-remaining/beta13-device/`.
- 16 KiB pages (110 + jm-v9/005) are untested anywhere: every tester kernel with a page-size build suffix, including the v14 6.12 one, is a `-4k` build. Remaining 4 KiB-only assumption: the GPU MMU granule `pgsize_bitmap = 4K`, unused by the kbase paths. The rest of 110 is items 18 and 20.

- 2 intermittent DeviceLost in `transform_feedback query_copy_*`: not seen in the 096 CTS run (36144-case list, 0 DeviceLost); keep watching.
- kbase/firmware: a long compute job while the vertex/tiler CSG waits on another CSG can get that group killed (096 root cause). 096 shortens the planner; other long single-workgroup jobs in that position could still hit it. Likely also behind the 2048-instance chunk cap.
- `draw.*depth_bias_patch_list_tri_line` fails (pre-existing, root cause unknown).
- 077 system-scope signals raise an interrupt per cross-subqueue signal; game perf cost unmeasured.
- 075 sync_file export uses one device-wide KCPU queue: head-of-line blocking; possible deadlock with wait-before-signal timelines (not seen in CTS).
- 089: one unreproduced intermittent on the first run after a fresh install (case B drew nothing; swapchain_lifecycle failed once in the same run). Not seen in 11 later gs runs and 6 swapchain runs. `swapchain_lifecycle` also failed once each during 090 and 092 run-alls, then passed on rerun.
- 085: recovery of an evicted CSG via host notification is unproven.
- [Issue #2](https://github.com/zenithblue-oss/panvk-kbase-android/issues/2): GTA IV (D3D9 via DXVK, Wine wow64) stutters then freezes. The log shows thousands of DXVK `Failed to allocate staging buffer memory, res -2` (`VK_ERROR_OUT_OF_DEVICE_MEMORY`). The device-local heap is sized from system RAM by `os_get_gpu_heap_size()`. Possible causes: kbase allocation failure under RAM pressure, or a driver BO leak. Needs the driver version, device RAM, the full log, and a heap-usage trace.
- Tiler geometry buffer padding of one page found empirically; root cause unknown.
- Swapchain lifecycle test on the Android surface does not change the extent; only an `oldSwapchain` recreate is tested.
- P12 test expects 32 GS invocations until `PANVK_MESA` defaults to the newest tree.
- Tests regenerate the P13/P16/P18/P19/P21 reports on every run.
- `/tmp` is a 7.5G tmpfs; big CTS/build data goes to the repo `tmp/` (gitignored).

## Deferred (vkd3d out of scope for now)

### vkd3d-proton native smoke
Deferred (vkd3d/D3D12 and FL12 out of scope for now; sparse is NO-GO on kbase). Chroot build script exists (`scripts/vkd3d/build-vkd3d-proton.sh`) and built on host, but no device run yet. Blocked by `robustImageAccess2=false`.

### robustImageAccess2
Deferred (vkd3d out of scope for now). Hard requirement for vkd3d-proton device creation (`panvk_vX_physical_device.c:631`). WIP in `work/mesa-ria2` branch `dx-ria2` (+2/-2 null image descriptors via texture subdescriptor, enabled on arch 11+), not proven. Keep disabled until CTS `dEQP-VK.robustness.robustness2.*` image/texel and `image_robustness.*` pass with 0 fail.

### X11 present teardown hang
Seen once under Xvfb in the glibc chroot (`x11_wait_for_present` in `destroySwapchain`). Not reproduced on Android: `swapchain_lifecycle` passes, and the beta.8 Android X11 software present path (084) returns `VK_TIMEOUT` instead of hanging or DeviceLost.
