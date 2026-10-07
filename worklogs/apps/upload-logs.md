# Upload zip logs for remote devices (PROGRESS item 13)

Date: 2026-10-06. Status: in code, not committed, not released, no version bump.

Goal: uploads from devices we do not own (G610 v10, G720/G925 v12/v13, G77, stock G615s, Pixel) carry enough to diagnose remotely.

## Changes

PanProbe (`apps/panvk-test`):
- `UploadLogs.kt` (new, shared copy in PanPlay): head+tail log cap (4 MiB, marker line), run-window logcat (`-d -v threadtime -b main,system,crash,events -T <run start>`, falls back to fewer buffers), device-facts block.
- Manifest gains `deviceFacts` (kbaseUapi, gpuId, textureFeatures, bcEmulation, pageSize, kernel, allowlisted props incl. `ro.board.platform`, `ro.hardware.gralloc`, `ro.vendor.api_level`, `ro.board.api_level`, mapper libs under `/vendor/lib64{,/hw}`, `/system/lib64/hw`, driverLines), `logcat` (original/kept bytes, buffers, `fullLogs`, scope), `truncatedLogs` (original sizes).
- `jni.c` `Native.kbaseVersion()`: VERSION_CHECK on a throwaway `/dev/mali0` fd (CSF nr 52, then JM nr 0). Works with any driver, even when the ICD fails.
- `MESA_LOG=file,android`: driver lines go to each test log and logcat.
- `vkinfo.c`: enables `VK_EXT_debug_utils` and a persistent messenger; messages land in `vkinfo.log` as `VKDBG ...`. `vkinfo.log` is now copied into the run folder.
- `bc_decode.c`: no dereference of `devs[0]` when enumeration returns 0 devices or fails, and a NULL `vk_icdGetInstanceProcAddr` check (the no-Mali SIGBUS/SIGSEGV).
- Debug intent `--ez zip true` (with `--es autorun ...`): builds the upload zip locally and copies it to external files. No upload.

PanPlay (`apps/panvk-launcher`):
- `Containers.kt` env: `DXVK_LOG_PATH=Z:<files>/gfx-logs`, `VKD3D_LOG_FILE=Z:<files>/gfx-logs/vkd3d.log` (DXVK_LOG_LEVEL info already set).
- `SessionLogs.collect`: moves gfx-logs files (d3d8/9/10/11, dxgi, vkd3d) into the session folder; all logs head+tail capped with original sizes in `session.json` `logs`; logcat via `UploadLogs.logcat` (run window, 4 MiB); `device-facts.json`. The cloud zip manifest gets `deviceFacts`, `logcat` and `logs`.
- PanPlay has no JNI kbase probe: `kbaseUapi` comes from the driver line, so it needs a driver with csf-v11/110.

Driver: `patches/csf-v11/118-log-kbase-and-device-decisions.patch` (logging only, rebased after 110). INFO lines: `panvk: gpu_id ... texture_features ...`, `panvk: BC emulation on|off (native compressed mask ...)`, plus `mesa_loge` "Unknown gpu_id". The kbase uAPI/page size and the queue-group/tiler-heap layouts come from 110; the mapper backend comes from android/014. EXEC_INIT logging was dropped because jm-v9/004-005 rewrite that block.

Anonymity: props are allowlisted (no serial, boot or account props). Without READ_LOGS, logcat holds only the app's own uid.

## Tests (G57 tablet TB336FU, v9 JM)

- PanProbe Run all with the wt-final+118 driver: 1/17 (unchanged v9 baseline). Zip 58 KiB; logcat 187 KB (own uid, all 4 buffers); no truncation. deviceFacts: JM 11.38, gpu_id 0x90930010, TEXTURE_FEATURES 0xf7fe03fe 0xc3fff7ff 0xbfe1ff9f 0x10c6, BC emulation on (mask 0xf7fe03fe), page 4096, ro.board.platform mt6835, gralloc common, vendor API 33, mapper `android.hardware.graphics.mapper@4.0-impl-mediatek.so`.
- With the 110 lines: `kbase: JM driver, uAPI version 11.38, page_size=4096` is picked up.
- bc_decode no longer crashes (FAIL with a reason, including with the system driver). The 0-device path itself is untested: no non-Mali device was available.
- The VKDBG messenger is untested: G57 raises no vk_errorf.
- `/proc/version` is SELinux-blocked for apps, so `procVersion` is null; `kernel` comes from `os.version`.
- Side finding: android/014 logs `mapper load failed (2 candidate names, first=mediatek)` on the tablet even though `mapper@4.0-impl-mediatek.so` exists.
- PanPlay Cube D3D11 x64 with bundled beta.16: Abnormal exit (1) as expected. The session has `dxvk-dxcube-x86_64_{dxgi,d3d11}.log` (from DXVK_LOG_PATH), `device-facts.json` and logcat 64 KB; zip 24 KB with `deviceFacts`/`logcat`/`logs` in the manifest. driverLines are limited until PanPlay bundles a driver with 110/118.
- Full series (121 patches incl. 118) applies via `scripts/apply-patches.sh` on a clean base and builds (`/var/tmp/panvk/wt-118b`, `dist-118b`).

## G615 checklist (not run)

- PanProbe Run all + `--ez zip true`: deviceFacts `CSF 1.21`, G615 gpu_id, BC emulation decision, queue_group_create / tiler_heap_init layout lines, `aimapper` / mapper backend line, zip < 25 MiB with Mesa debug on (trace logs get capped).
- PanPlay DXVK game: dxvk-*.log present, logcat capped at 4 MiB, the cloud zip manifest carries deviceFacts.
