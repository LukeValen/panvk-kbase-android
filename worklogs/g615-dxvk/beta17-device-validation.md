# beta.17b device validation (Poco X6 Pro, G615)

Date: 2026-10-06. Device `192.168.1.34:35419` (2311DRK48I, mt6897, Mali-G615 MC6, kbase CSF 1.21, Android 16).
ICD: `dist-beta17b/libvulkan_panfrost.so`, BuildID `2eced92e…` (Android, stripped in APK: sha `a38b083c…`); glibc `dist-beta17b/glibc/` (sha `2b691f65…`) for CTS.
Scope (user change mid-run): PanProbe + CTS only. PanPlay was installed and its driver selected, but no game or cube was run (user tests PanPlay). NFS A/B, cube FL/SHM checks: not done.

## Installed (final state)

| App | Version | APK | Notes |
|---|---|---|---|
| PanProbe | 1.2.3 (7), debug | `/var/tmp/panvk/apk-beta17b/panprobe-1.2.3.apk` sha `5fe67cdf…` | bundled beta.17b ICD, bundled-driver.json beta.17b; selection BUNDLED (was IMPORTED beta.16 `c0e54909…`, file kept) |
| PanPlay | 1.2.3 (10), debug | `/var/tmp/panvk/apk-beta17b/panplay-1.2.3.apk` sha `a2e94173…` | bundled beta.17b; `launcher.xml` driver = `panvk-kbase-g615-0.1.0-beta.17b`; data kept (119 shortcut files, games, m11/m12 drivers) |

Both installed with `adb install -r` over 1.2.2 (same signer `8b10fde8…`). Built with `-PpanvkSo=dist-beta17b/...`; the launcher `bundled-driver.json` was set to beta.17b for both builds (PanProbe reads it too) and restored to beta.15 after (git clean).
Backups: `/var/tmp/panvk/b17v/backup/` (both 1.2.2 APKs, prefs, launcher shortcuts/drivers meta tar).

## PanProbe

- `autorun all` (beta17b debug APK x3, 1.2.3 x3) and UI Run all x1: **16/17, 0 skip** every time. Only `large_draw` fails.
- `large_draw`: new 112 case `gs_restart_direct`/`gs_restart_indirect` (triangle GS on the 139263-prim restart strips) faults the CSG (`exception 0x41` or `0x72`), then DEVICE_LOST takes every later case.
  - Same fault with the **beta.16** ICD (imported `c0e54909…`) and the new test. The old 1.2.2 test (no gs_restart cases) passes 14/14 with beta.16. So: pre-existing driver bug exposed by the new test, not a beta.17b regression. The 112 worklog called GS primitive IDs after restarts "not a gap"; on hardware this draw hangs.
  - Per-case in chroot (`LARGE_DRAW_ONLY`, native build of `large_draw.c`, glibc ICDs): beta.17b passes every other case (points, instanced, strip, restart, fan_direct/indexed/instanced, small_inst x2, cond_skip/pass_indirect, cond_skip_tess, gs_direct/indirect, tess x5, indirect_count, multi_indirect). beta.16 fails `fan_direct`, `fan_indexed`, `cond_skip_indirect` (fixed by 112).
- New cases: `vmr_secondary` 13/13 (114), `vs_viewport_index` 7/7 incl. `G_vs_fs_primitive_id` (115), `depth_bounds` 9/9 incl. `H_eft_depth_write` and `I_perf_dyn_off` (ratio 1.98, plain 0.37 ms vs dyn-off 0.73 ms; expected after the 116 rework dropped the FPK path).
- `swapchain_lifecycle` PASS 121 FPS (resized 63-121 FPS, varies run to run).
- `driverInfo` still reads `PanVK-kbase beta.16` in the beta.17b build (099 string not bumped).

## Logcat / 110 / 118 / 014 lines (autorun all)

`kbase: CSF driver, uAPI version 1.21, page_size=4096`; `queue_group_create layout=32 bytes`; `tiler_heap_init layout=16 bytes`; `mem_alloc via ALLOC_EX`; `panvk: gpu_id 0xb8a31030 variant 0x4 arch v11 model Mali-G615 texture_features 0xc7fe001e 0 0 0`; `BC emulation on (native compressed mask 0xc7fe001e)`; mapper `init backend=aimapper name=mediatek how=BINDER_PASSTHROUGH rc=0 version=5`. No `Unknown gpu_id`, no fatal signal.
Note: `logd` was stopped on the device (logcat empty, "Logcat read failure"); started with `su -c start logd`. The first two autoruns have no logcat.

## Upload zip (`--ez zip true`, not uploaded)

1.2.3 zip 76 KB (< 25 MiB). Manifest: app 1.2.3 (7), driverName "beta.17b (local)", buildId 2eced92e, pass 16 / fail 1 / skip 0, per-test status. deviceFacts: kbaseUapi `CSF 1.21`, gpuId 0xb8a31030, textureFeatures, BC decision, page 4096, props, mapperLibs (6), 7 driverLines (all lines above). logcat 450 KB, not truncated, buffers main/system/crash/events, own uid.

## CTS (chroot Alpine, glibc ICD, `deqp-vk` with resume)

52725 cases in one list: the 36144-case prerast set (`tmp/g615-gap/cts-all.txt`: xfb, PG, geometry, tess, draw indirect, multisample/VMR, secondary, no-attachment, cond), BC (`texture.compressed` bc, 192), draw fans (448), draw `shader_viewport_index` (196), `depth_bounds` (4458, monolithic pipeline + dynamic_state), and the 11331-case sync + memory gate.
Total 32338 pass / 116 fail / 20271 NotSupported, no crash or DeviceLost.

| Subset | beta.17b | Baseline |
|---|---|---|
| prerast 36144 | 17067 / 0 / 19077 NS | 17067 / 0 (beta.10 era, `fin-all`) |
| BC 192 | 96 / 0 / 96 NS | beta.16 same |
| fans 448 | 384 / 0 / 64 NS | beta.16 same |
| shader_viewport_index 196 | 136 / 60 fail | beta.16 same 60 (`fragment_shader_2..16`, known since beta.13) |
| depth_bounds 4458 | 3440 / 0 / 1018 NS | beta.16 same |
| sync + memory gate 11331 | 11257 / 56 / 18 NS | beta.16 56, identical set |

No new CTS failures. Non-pass list: `validation/driver-remaining/beta17-device/cts-b17b-52725-nonpass.txt`.

## Artifacts

- `validation/driver-remaining/beta17-device/`: PanProbe screenshots (`panprobe-poco-1.2.3-beta17b.png`, `panprobe-poco-beta17b-{top,bottom}.png`), autorun, large_draw log, CTS non-pass list.
- `/var/tmp/panvk/b17v/`: probe runs (autorun.txt, logcat, zips), cts status, chroot scripts.
- Device: `/data/local/tmp/chrootAlpine/root/b17cts/` (status, gz qpa), `/root/b17ld/` (native large_draw), `/root/b17icd/`. Chroot bind mounts removed after.

## Verdict

No beta.17b regression vs beta.16 (CTS identical, PanProbe failure also on beta.16). But PanProbe is 16/17, not 17/17: the new `gs_restart_*` cases hang the GPU. Before release either fix the GS-on-restart-strip path or drop/gate those two cases in the test, else every tester upload shows `large_draw` FAIL.
Side notes: device display override 1080x1920 / density 280 appeared during the run (not set by this run; left alone).

## beta.17c / beta.17d (2026-10-06, same device)

Fixes: csf-v11/120 (GS restart strip table, the `gs_restart_*` hang), csf-v11/121 (exact depth for minDepth == maxDepth), 099 driverInfo `beta.17`. [Worklog](../driver-remaining/120-gs-restart-strip-table.md). sources.lock / patchSeriesId not touched (beta.16 release did not touch it either).

| Build | Android ICD sha256 / BuildID | glibc sha256 |
|---|---|---|
| beta.17c (120) | `fb9904d9…` / `b44ea0b1…` | `3647e674…` |
| beta.17d (120+121, final) | `5d9fdb88…` / `313addcb…` | `e93f7ccc…` |

validate-binary PASS (android, glibc) for both. driverInfo: `PanVK-kbase beta.17 (Mesa 26.3.0-devel (git-eda9ac0c6f))`.

- PanProbe 17-test 1.2.3 + 17c: autorun 17/17 x2, `large_draw` 27/27 incl. `gs_restart_*`, no CSF fault. UI Run all x2: 16/17, only `depth_bounds` `I_perf_dyn_off` (plain 2.1-2.2 ms, ratio 1.6-2.1). That case fails whenever plain > 2 ms: the ratio is ~2 by design since the 116 rework (no FPK while the test is off); the GPU was power-limited (gpufreq PPM ceiling idx 40) and plain rose from 0.37 ms to 1.1-2.2 ms. Chroot A/B 17b vs 17c identical (plain 1.7-1.8 ms both).
- PanProbe 36-test 1.2.3 (current repo, other session's tests) + 17d: autorun 36/36 x2; zip run 36/36 (2nd try; 1st had `swapchain_lifecycle` fps=0 while another app was in front) and a final zip run 36/36, `verify_zip.py` OK, compliance DXVK / Bachata S4 / vkd3d all pass. `depth_stencil` 10/10 (`zero_depth_range` d24 +3e-8, d32 0). Zip: `validation/driver-remaining/beta17-device/panprobe-poco-beta17d-36.zip`.
- The earlier "29/36 beta.17c regressions" run (PROGRESS item 36) bundled the stale default `build/android-dxint-dist` ICD (BuildID `587c2bb6`, no series): not a driver regression.
- CTS 52725 list, chroot glibc ICD: 17c = 17b exactly (32338 / 116 / 20271 NS, same non-pass set). 17d: 32339 / 115 / 20271; the one difference is the flaky `synchronization2.timeline_semaphore.wait_before_signal.write_image_tess_control_read_image_geometry.image_64x64x8_r32_sfloat` passing. No new failure. Sync + memory gate 55-56 known.
- Extra CTS 17b/17c/17d: `transform_feedback.*restart*` 4/0, `pipeline.monolithic.input_assembly.*` 181/0, `geometry.input.*` 28/0, `clipping.clip_volume.*` 43/0, `draw.*depth_clamp*` 196/0. `draw.*inverted_depth_ranges*`: 40/0 on 17c, 36/4 on 17d (the 4 `nodepthclamp_deltazero`, cost of 121, not in the gates).
- Chroot `large_draw` 17c: `ALL` x5 27/27 + `gs_restart` x5, no kbase fault.

Installed (final): PanProbe 1.2.3 (7) debug `apk-beta17d/panprobe-1.2.3.apk` sha `47042265…`, driver BUNDLED (17d). PanPlay 1.2.3 (10) debug `apk-beta17d/panplay-1.2.3.apk` sha `19cab4c3…`, `launcher.xml` driver = `panvk-kbase-g615-0.1.0-beta.17d`, data kept (121 shortcut files), no game launched. Both `install -r`. `bundled-driver.json` was set to beta.17d for the builds and restored (git) after. Chroot bind mounts removed.
