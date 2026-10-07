# 112 Prerast chunk gaps (v10-v12 CSF gpu_prerast)

Patch: `patches/csf-v11/112-split-prerast-fans-cap-instance-grids-skip-conditional-chunks.patch` (not committed).
Files: `src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c` (`gpu_prerast_split_fan`, `gpu_prerast_split`, `gpu_prerast_chunk_loop`), `src/panfrost/libpan/draw_helper.cl` (`panlib_fan_chunk_indices`, `panlib_tess_step`).

Status: built, reviewed (self + Sol). **Not run on hardware.** The G615 was not connected and the G57 (v9 JM) has no gpu_prerast.

## The four 094 limits

| Limit | Before | Now |
|---|---|---|
| Triangle fans over the arena | Logged `gpu_prerast_unhandled` and dropped | Direct fans split, see below. Still unhandled: indexed fans with primitive restart, and indirect fans over the arena. |
| 2048 instances per chunk | The chunk loop caps them and loops until `next_inst >= insts`, so nothing is dropped (checked in `panlib_chunk_step`). Fans skip the chunk loop, and tessellation chunks had no cap. So a fan of 3 vertices x 9000 instances, or 1 patch x 9000 instances, made one lowering grid with 9000 instances (the G615 hang size). | Fans with more than 2048 instances are split on the CPU in steps of at most 2048. `panlib_tess_step` caps `k` at 2048. |
| Chunks skipped under rasterizer discard | Not a gap. Only the raster draw is skipped (`gpu_prerast_draw_generated`). XFB (`gpu_prerast_xfb`), primitives generated (planner atomic, or the GS kernel) and pipeline statistics (`cmd_pstats_draw`, once per API draw before the split) all run on the compute side. Every `large_draw` pipeline uses `rasterizerDiscardEnable`, so all its cases test this. | Unchanged. |
| GS primitive ID after restarts | Not a gap. `prim_base` is `prims_done`: exact closed-strip counts per window, plus `decomposed(n_verts)` for a mid-strip cut whose overlap repeats. The GS kernel adds the primitives before each invocation's strip inside the chunk (`gs_prims_before` scan). That makes the ID exact for in-bounds restart strips. Sol agrees. | Unchanged. Now tested (`gs_restart_*`). |

New gap, found and fixed: a chunked draw (indirect, many instances, or a restart strip) skipped by conditional rendering still planned and lowered its chunks, and it added to primitives generated. Only the raster launch was predicated. XFB was already predicated in `gpu_prerast_xfb`. Fix: after `panlib_chunk_setup`, if `gpu_prerast_cond_render_pass` is 0, the CS zeroes `in_draw[0..1]`. The planner then plans nothing, `more` = 0, and both loops run once to hand the arena over. Tessellation already did this (087).

## Fan split

The draw is cut into windows of `win = max_verts - 1` API vertices starting at `s = 1, win, 2*win - 1, ...`. Each chunk is an indexed fan `[hub, s .. s+n-1]`, so consecutive windows share one vertex. Chunk triangle `t` is `{s+t, s+t+1, hub}`, which is API triangle `s-1+t` with the same vertices, winding and provoking vertex. `gpu_prerast_prim_base = s-1`. Each chunk counts `n-1` triangles, which adds up to `count-2`.

`panlib_fan_chunk_indices` (64-wide) writes the 32-bit chunk indices on the compute queue. `cs_wait_slots` then runs before the lowered VS reads them. Only the compute-side VS reads the chunk index buffer; the raster draw reads the arena indices.

- Non-indexed fans use the element numbers as indices and keep `vertex.base = firstVertex`. That gives the same `gl_VertexIndex`/`gl_BaseVertex`.
- Indexed fans copy `index[offset]` and `index[offset+s..]`. Elements past the buffer read as 0.

## Known limits (documented, not fixed)

- Indexed fans with `primitiveRestartEnable` over the arena: a restart starts a new hub, which the CPU split cannot see. Still `gpu_prerast_unhandled`. DXVK only sets restart for strips.
- Indirect fans over the arena: their counts are unknown at record time and fans are not in the GPU planner. The same applies to indirect fans with more than 2048 instances.
- CPU instance splits (fans now, and all topologies over the arena before this patch) shift `instance.base`. `gl_InstanceIndex` stays right, but `gl_BaseInstance` and attribute divisors other than 1 see the chunk base. This is pre-existing in `gpu_prerast_split` (found by Sol).
- `firstIndex + s` is 32-bit and can wrap for absurd firstIndex values. Same as the existing strip split.
- The VS runs the fan hub once per chunk. Strip overlaps already re-run vertices, and Vulkan allows extra VS invocations.
- Pre-existing, outside 112: the planner ignores restart for list topologies (overlap 0) in oversized draws (`primitiveTopologyListRestart`). `update_prims_generated_query` (non-chunked draws) is not predicated by conditional rendering.

## Tests (APK `large_draw`, `tests/dxvk/vulkan/large-draw/large_draw.c`)

New cases:

- `fan_direct`: 150001-vertex fan.
- `fan_indexed`: 140001 descending indices, firstIndex 3, vertexOffset 5.
- `fan_instanced`: 4 vertices x 5000 instances.
- `small_inst_direct` / `small_inst_indirect`: 3 points x 9000 instances.
- `gs_restart_direct` / `gs_restart_indirect`: new `large_draw_tri.geom` on the restart strips. Each record is the vertex-index sum plus `gl_PrimitiveIDIn == k`.
- `cond_skip_indirect`: zero predicate, so XFB bytes, the XFB query and primitives generated must all be 0.
- `cond_pass_indirect`: the same draw with a non-zero predicate.
- `cond_skip_tess`.
- Tessellation cases: see 113.

SPIR-V was regenerated with `build_spv.sh`. The test compiles on the host (`gcc -fsyntax-only`) and in the APK NDK build.

## Build

- Worktree `/var/tmp/panvk/wt-112`: base 169d6a0, series applied, commit `36207b3` "series-baseline", then the 112 commit.
- Android ICD `/var/tmp/panvk/dist-112/libvulkan_panfrost.so`: SHA256 `93918e1764bad951131beb53864c5c0e5fca3964627434d74648782faf8b8d3e`, BuildID `baebf78f7069e2e0fe76ef026b53c62db85600a4`, validate-binary PASS.
- APK with this ICD bundled: `/var/tmp/panvk/apk-112/app-debug.apk`.
- Applying the full series on a fresh 169d6a0: 112 applies. The concurrent csf-v11/118 currently fails to apply (`kbase_kmod.c:1390`); that is not from 112.

## G615 commands (when connected)

```sh
adb install -r /var/tmp/panvk/apk-112/app-debug.apk
adb shell am force-stop dev.zenithblue.panvktest
adb shell am start -n dev.zenithblue.panvktest/.MainActivity --es driver bundled --es autorun large_draw
adb shell run-as dev.zenithblue.panvktest cat files/autorun.txt   # want large_draw PASS, XFB_FAILS=0
adb logcat -d | grep -E 'large_draw|case |gpu_prerast|unhandled'   # no "triangle fan"/"unhandled"
adb shell am start -n dev.zenithblue.panvktest/.MainActivity --es driver bundled --es autorun all   # 17/17
# CTS (bionic deqp-vk + shim, ICD from dist-112):
deqp-vk --deqp-case='dEQP-VK.transform_feedback.*'
deqp-vk --deqp-case='dEQP-VK.query_pool.*primitives_generated*'
deqp-vk --deqp-case='dEQP-VK.transform_feedback.primitives_generated_query.*'
deqp-vk --deqp-case='dEQP-VK.draw.*triangle_fan*'
deqp-vk --deqp-case='dEQP-VK.conditional_rendering.*'
deqp-vk --deqp-case='dEQP-VK.tessellation.*'
deqp-vk --deqp-case='dEQP-VK.geometry.*'
```

Run `large_draw` at least 5 times: `fan_instanced`, `small_inst_*` and `tess_instanced` are the hang-risk cases.

Pass criteria:
- `large_draw`: all cases PASS.
- CTS: equal to the beta.16 baseline (transform_feedback 15793/0, tessellation 526/0, geometry 189/0, conditional_rendering 922/0).

Update 2026-10-06: the GS primitive ID scan above was correct but quadratic and hung the GPU on the APK (`gs_restart_*`); fixed by csf-v11/120, see `120-gs-restart-strip-table.md`.
