# 120 GS on primitive-restart strips: strip table (+121 flat zero depth range)

Patches: `patches/csf-v11/120-build-a-strip-table-for-gpu-prerast-gs-restart-draws.patch`, `patches/csf-v11/121-keep-the-depth-flat-for-zero-depth-range-viewports.patch`, and 099 driverInfo bumped to `beta.17`. Built as beta.17d (beta.17c = 120 only). Uncommitted, unreleased.

## Bug (beta.17b release blocker)

PanProbe `large_draw` `gs_restart_direct` / `gs_restart_indirect` (triangle GS on 139263-prim restart strips, one strip 70001 vertices) ended the CSG with `CSF group 0 fatal error: status 0x72` (or `CS_FATAL 0x41`) on the APK, then DEVICE_LOST for every later case. beta.16 the same.

## Root cause

The draw is chunked (`gpu_prerast_chunk_wanted`: restart strip over `max_verts`, 094/096 chunk loop). In each chunk the lowered GS kernel (`panvk_gpu_prerast_lower_gs`, one invocation per vertex) with restart enabled:

- walked back over the generated indices to the previous restart to find its strip start (`gs_seg` loop), and
- with `gl_PrimitiveIDIn` read, counted the primitives of every strip before it from index 0 (`gs_prims_before` loop).

Both are O(position): one GS job over a 65536-vertex chunk is quadratic. Chroot timing: the two cases take 5 s with beta.17b and 0.5-1 s with the fix. During the long compute job the vertex/tiler CSG waits in its cross-CSG `SYNC_WAIT` inside the render pass, and on the APK (compositor active, group eviction) kbase/firmware kill it. This is the same mechanism as the 094 restart planner flake (096): long compute jobs while the VT group waits. In chroot (no compositor) the old ICD passes, just slowly.

## Fix (120)

- `panlib_gs_strip_table` (libpan `draw_helper.cl`, one 256-invocation workgroup, same scheme as 096 `chunk_scan`): per generated index writes the strip start, the primitives of the strips before it and the next restart, for `min(vertex_count, 65536)` indices.
- `gpu_prerast_gs_run` launches it before the GS kernel when restart is on. The table lives in the polygon-mode index region of the arena (`PANVK_GPU_PRERAST_GS_STRIP_OFFSET`; a GS draw has no polygon-mode pass; static_assert on the size). New `gs_params.strips`.
- The GS kernel loads `seg`, `before` and (triangles-adjacency) `end` instead of looping; out-of-range invocations clamp the index and are invalid anyway.

## 121 zero depth range

PanProbe `depth_stencil` `*_viewport_zero_depth_range` (minDepth == maxDepth == 0.5): depth was 0.5 +- 1.8e-5. Upstream widens any depth range below 37.7e-6 so the window-space depth cull can still clip clip-space Z (dEQP `inverted_depth_ranges.nodepthclamp_deltazero`). Now only a non-zero range is widened; a zero range writes exactly minDepth and does not depth clip. Cost: the 4 `dEQP-VK.draw.*inverted_depth_ranges.nodepthclamp_deltazero` cases fail (they pass on 17c). They are not in the prerast/sync/memory gates. Both at once is not possible with the fixed-function cull (cull and clamp use the same window-Z bounds); an FS depth-write variant would be needed.

## Evidence (Poco X6 Pro G615, 2026-10-06)

- Chroot `large_draw` (glibc ICD): 17c all 27 cases x5 + `gs_restart` x5 pass, no kbase fault; old 17b `ALL` 9 s vs 17c 3-5 s.
- PanProbe 17-test build (17c): autorun 17/17 x2. 36-test build from the current repo (17d): autorun 36/36 x2, zip run 36/36 + `verify_zip.py` OK (DXVK, Bachata S4, vkd3d compliance pass). `large_draw` 27/27 incl. `gs_restart_*`. No CSF fault in logcat.
- CTS 52725 list (prerast 36144 + BC + fans + shader_viewport_index + depth_bounds + sync/memory gate): 17c identical to 17b (32338 / 116 fail / 20271 NS, same non-pass set). 17d: see `beta17-device-validation.md`.
- Extra: `transform_feedback.*restart*` 4/4, `pipeline.monolithic.input_assembly.*` 181/0, `geometry.input.*` 28/0, `clipping.clip_volume` 43/0, `draw.*depth_clamp*` 196/0 on 17b, 17c and 17d.

## Note on the "beta.17c regressions" (PROGRESS item 36)

The 29/36 run (`phase4/panprobe-20261006-202914.zip`) used a PanProbe APK built without `-PpanvkSo`: it bundled the stale default `build/android-dxint-dist/libvulkan_panfrost.so` (BuildID `587c2bb6`, Mesa `5a07217f` without the series: no depthBounds, no shaderOutputViewportIndex) while `bundled-driver.json` said beta.17c. With the real 17c ICD (BuildID `b44ea0b1`) the same 36-test build gives 35/36 (only `depth_stencil`), `gs_tess_primitive_id` included. Always pass `-PpanvkSo=<dist>/libvulkan_panfrost.so`.
