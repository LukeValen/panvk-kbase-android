# Mali v14 (5th-gen CSF, Mali-G1 family) status

All file:line references throughout this document are relative to the Mesa root of the beta.16 tree (Mesa 5a07217f + csf-v11 series up to 107 + jm-v9 001-003), written as `src/panfrost/...:NNN`, unless marked "beta.17 tree" (the unreleased beta.17 candidate: beta.16 + csf-v11 108-118 + android/014 + wsi/017 + jm-v9 004-005).

## Summary
The first v14 data arrived after the beta.16 snapshot: eight PanPlay uploads from one vivo V2515 (MT6993, Mali-G1-Ultra MC12) on beta.15. The gpu_id is in the model table and the physical device enumerates cleanly (DXVK prints `Found device: Mali-G1-Ultra MC12`, `textureCompressionBC : 1`). Every run then fails inside `vkCreateDevice` with `VK_ERROR_OUT_OF_DEVICE_MEMORY`, because the first GPU allocation uses `KBASE_IOCTL_MEM_ALLOC_EX` and this kernel rejects that ioctl with `ENOTTY` ("Inappropriate ioctl for device"). No queue, tiler heap or shader has run on v14 yet. No PanProbe archive has been uploaded for v14. Mali-G1-Premium and Mali-G1-Pro remain unseen. The 2026-10-06 pass (D1 ids 54 to 90) has no new v14 records. The unreleased beta.17 candidate carries the `MEM_ALLOC_EX` to `MEM_ALLOC` fallback and the uAPI version log (patch 110), which have not run on v14.

## GPUs and devices tested

| Driver build | Anon ID(s) | Device | SoC | GPU | gpu_id -> Mesa model | Kernel | Android | App |
|---|---|---|---|---|---|---|---|---|
| beta.15 | `8fd54bb9`, `d321b12a`, `703077ca`, `eb5ccca3`, `0a6b82e7`, `9aca5216`, `fa35ee7e`, `0cc289c7` | vivo V2515 | MT6993 | Mali-G1-Ultra MC12 | `0xe8800010` -> G1-Ultra | 6.12 android16 (4 KiB pages) | 16 | PanPlay 1.2.2 |

The hardware identifier `0xe8800010` decodes to architecture 14.8, product 0, revision r0p1, which matches `PAN_PROD_ID(14, 8, 0)` variant 4 "G1-Ultra" in `src/panfrost/model/pan_model.c:115`. PanVK names the device "Mali-G1-Ultra MC12", the same name the D1 record reports, so the model match and core count are correct. Five of the eight archives were retrieved and verified; the other three (`703077ca`, `0a6b82e7`, `fa35ee7e`) were uploaded to an external host only and could not be fetched, so their results come from the D1 row (game and exit code) alone.

## Kernel / kbase interface seen
- **Kernel version:** Linux 6.12.58 android16 with a `-4k` build suffix, so the device runs 4 KiB pages. `ro.product.cpu.pagesize.max = 16384` only says the userspace is 16 KiB-ready. 16 KiB pages are therefore not the cause; the 4096-byte page size hard-coded in `src/panfrost/lib/kmod/kbase_kmod.c:1881` matches this kernel.
- **kbase interface:** CSF. The kbase uAPI version is not logged in the uploads. It is at least 1.9, because the driver takes the `MEM_ALLOC_EX` branch only for CSF uAPI >= 1.9 (`src/panfrost/lib/kmod/kbase_kmod.c:1909-1910`).
- **Vendor API level:** `ro.board.first_api_level = 202504`, `ro.board.api_level = 202504`, `ro.product.first_api_level = 36`, `ro.hardware.gralloc = common`. The vendor DDK version was not captured (PanPlay records carry no GL version).
- **Gralloc:** `No gralloc hwmodule detected` and `Using fallback gralloc implementation`, as on every other device. The mapper path was never reached.
- **EXEC_INIT:** No `KBASE_IOCTL_MEM_EXEC_INIT` warning appears.
- **Queue-group layout and tiler heaps:** Not reached. Device creation fails at the first buffer allocation, before any queue group or tiler heap is created, so the queue-group layout used by this kernel is unknown.

## Per-test results

### PanPlay execution runs

| Driver build | Anon ID | Target executable | Exit code | Observed behaviour |
|---|---|---|---|---|
| beta.15 | `8fd54bb9` | D3D8 x86 cube | 1 | `vkCreateDevice` fails (`VK_ERROR_OUT_OF_DEVICE_MEMORY`), also in DXVK safe mode; `CUBE: FAIL CreateDevice hr=0x8876086a` after 2 s |
| beta.15 | `d321b12a` | D3D8 x86 cube | 1 | Same, after 1 s |
| beta.15 | `703077ca` | D3D9 x86 cube | 1 | Archive not retrievable; D1 exit code 1 |
| beta.15 | `eb5ccca3` | D3D9 ARM64EC cube | 1 | Same `vkCreateDevice` failure; `CUBE: FAIL CreateDevice hr=0x8876086a` |
| beta.15 | `0a6b82e7` | D3D11 ARM64EC cube | 1 | Archive not retrievable; D1 exit code 1 |
| beta.15 | `9aca5216` | D3D11 ARM64EC cube | 1 | Same failure; `CUBE: FAIL create hr=0x80004005` |
| beta.15 | `fa35ee7e` | D3D11 x64 cube | 1 | Archive not retrievable; D1 exit code 1 |
| beta.15 | `0cc289c7` | D3D11 x64 cube | 1 | Same failure; `CUBE: FAIL create hr=0x80004005` |

### PanProbe test suite (17 tests)
No PanProbe run has been uploaded for v14. All 17 tests are untested. Since `vkCreateDevice` fails, every PanProbe test would also fail at device creation on beta.15.

## Failures and log excerpts

### Device creation failure (`wine-run.log`, all five retrieved archives)
```
info:  DXVK: v3.1.1+
info:  Found device: Mali-G1-Ultra MC12 (panvk 26.2.99)
info:    textureCompressionBC           : 1
E MESA    : kbase: KBASE_IOCTL_MEM_ALLOC_EX failed: Inappropriate ioctl for device
err:   Failed to create Vulkan device: VK_ERROR_OUT_OF_DEVICE_MEMORY, retrying 'safe mode'.
E MESA    : kbase: KBASE_IOCTL_MEM_ALLOC_EX failed: Inappropriate ioctl for device
err:   Failed to create Vulkan device: VK_ERROR_OUT_OF_DEVICE_MEMORY
err:   Failed to initialize DXVK device.
CUBE: FAIL CreateDevice hr=0x8876086a
```
The `MEM_ALLOC_EX` error is the only Mesa error in the logs. It appears exactly once per `vkCreateDevice` attempt, so the very first BO allocation fails. Physical-device setup (version check, `GET_GPUPROPS`, feature reporting) works, which rules out a wrong ioctl magic or a broken handshake.

## Root causes
- **`KBASE_IOCTL_MEM_ALLOC_EX` rejected (new, v14 only so far):** `kbase_kmod.c` allocates through `KBASE_IOCTL_MEM_ALLOC_EX` (ioctl 59, 64-byte union, request `0xc040803b`; definition in `include/drm-uapi/mali_kbase_ioctl.h:175`) whenever the kernel is CSF with uAPI >= 1.9 (`src/panfrost/lib/kmod/kbase_kmod.c:1909-1927`). `ENOTTY` means the kernel's ioctl dispatcher did not recognise that request at all. An incomplete context setup would return `EPERM` instead, and a field-layout change alone would not produce `ENOTTY`. So this vendor kbase either drops ioctl 59 or uses a different size or direction for it. Which one is not known, because the kbase uAPI version is not logged. The same code works on G615 (uAPI 1.21), G720 and G925 (6.6 kernels). This analysis is an inference from the error code. It has not been checked against the vendor kernel source.
- **Driver treats unknown uAPI majors as compatible:** `pan_kmod_driver_version_at_least()` (`src/panfrost/lib/kmod/pan_kmod.h:609`) accepts any higher major version. If this kernel reports a new major uAPI, other ioctls whose layout changed between kbase releases are also at risk: queue-group creation (ioctls 58 and 42, `src/panfrost/lib/kmod/kbase_kmod.c:589`), tiler-heap init (ioctl 48, `kbase_kmod.c:1170`) and the KCPU command ABI.

## What the driver lacks on this arch
- A fallback from `KBASE_IOCTL_MEM_ALLOC_EX` to the legacy `KBASE_IOCTL_MEM_ALLOC` when the kernel returns `ENOTTY`. **Patched, unreleased (`patches/csf-v11/110`), untested on v14.**
  - On ENOTTY or EINVAL before any allocation has succeeded, the driver falls back to `MEM_ALLOC` and caches the working path per device.
  - Public kbase implements `MEM_ALLOC` as ALLOC_EX with `fixed_address = 0`, so the request is identical.
  - Public CSF headers 1.10-1.31 keep `MEM_ALLOC_EX` at nr 59 with 64 bytes. The rejection is vendor- or newer-release-specific, and its cause is still unknown.
- Logging of the kbase uAPI version at device creation (today it is only a debug log, `src/panfrost/lib/kmod/kbase_kmod.c:1344`, compiled out of release builds). **Patched in 110:** `kbase: CSF driver, uAPI version X.Y, page_size=N` at INFO, plus one-time `kbase: mem_alloc via ALLOC_EX|ALLOC`, `kbase: queue_group_create layout=N bytes` and `kbase: tiler_heap_init layout=N bytes` lines. Group create and heap init also retry the other public layouts on EINVAL/ENOTTY.
- Any validation past `vkCreateDevice`: queue groups, tiler heaps, shader upload, submission, and the v12+ feature gates (expected to match v12/v13: `gs_viewport_depth`, `vs_viewport_index`, `depth_bounds`).
- Untested variants: Mali-G1-Premium (`PAN_PROD_ID(14, 8, 1)`) and Mali-G1-Pro (`PAN_PROD_ID(14, 8, 3)`).

## Fix plan

| Rank | Item | Effort | Unblocks |
|---|---|---|---|
| 1 | `MEM_ALLOC_EX` -> `MEM_ALLOC` fallback on ENOTTY/EINVAL before the first successful allocation, cached per device. **Done in csf-v11/110 (unreleased)**: `src/panfrost/lib/kmod/kbase_kmod.c:2023-2055` in the beta.17 tree | Tester rerun | `vkCreateDevice` on G1-Ultra; first real v14 queue, heap and shader data |
| 2 | Log the kbase uAPI version at INFO. **Done in 110**: `kbase_kmod.c:1423` (beta.17 tree). Rejecting unknown CSF major versions explicitly is not done | 0.5–1 h for the reject | Tells us which ioctl layouts this kernel expects |
| 3 | PanProbe run on the G1-Ultra with beta.17 | Data collection | Shows queue-group layout, tiler heap, BC and swapchain behaviour |
| 4 | Same v12+ fixes as v12/v13 (per-viewport depth runs, depthBounds, shaderOutputViewportIndex; candidate patch `csf-v11/109`) | See [v12/README.md](../v12/README.md) | Remaining PanProbe gates, if they show up on v14 |

## Open questions / data needed from testers
- **kbase uAPI version and DDK:** The kbase uAPI version and vendor DDK of the MT6993 kernel. A PanProbe archive would carry the GL version string.
- **PanProbe archive:** A full PanProbe run with a build that contains csf-v11/110. Logcat lines to send:
  - `kbase: CSF driver, uAPI version ...`
  - `kbase: KBASE_IOCTL_MEM_ALLOC_EX failed ... trying KBASE_IOCTL_MEM_ALLOC`
  - `kbase: mem_alloc via ALLOC`
  - `kbase: queue_group_create layout=...`
  - `kbase: tiler_heap_init layout=...`
  - any `layout=... failed` lines

  If group create or heap init still fail with ENOTTY, this kernel's CSF uAPI differs beyond the public headers.
- **Other G1 parts:** `gpu_id` values from Mali-G1-Premium and Mali-G1-Pro devices.

- **VPSA on v14:** As on v13, `vertexPipelineStoresAndAtomics` is a drirc opt-in on v13+ (`src/panfrost/vulkan/panvk_vX_physical_device.c:345-348`, beta.17 tree), so DXVK is expected to cap v14 at FL11_0. See [v13/README.md](../v13/README.md#open-questions--data-needed-from-testers).

## Links
- [Universal Mali status](../README.md)
- [Tested devices](../DEVICES.md)
- [v12 status (same 5th-gen feature gates)](../v12/README.md)
- [v13 status](../v13/README.md)
