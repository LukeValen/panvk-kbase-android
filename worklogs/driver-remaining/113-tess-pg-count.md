# 113 Exact tessellation primitives-generated count

Status: **the code is already exact, so 113 needs no driver patch.** A targeted test was added. Not run on hardware (the G615 was not connected).

## Check

`panlib_tess_draw` (`libpan/draw_helper.cl`) adds `count / verts_per_prim` per index range to the primitives-generated counter. That counter is the query pointer when there is no GS; with a GS, the GS kernel counts what it emits.

- `vpp = mesa_vertices_per_prim(out.prim)`: points mode gives 1, isolines give 2 (line list), triangles give 3.
- `range` is a multiple of vpp, so no primitive is split across ranges and none is counted twice.
- The raster draw (`out`) has `prims_generated_counted = true`, so `update_prims_generated_query` does not count it again.
- Conditional rendering skip: since 087, the first compute iteration zeroes `in_draw`, `patches_per_instance` and `total_patches`. The skipped draw adds 0. The 093 note ("primitives generated of a skipped conditional tess draw still run") is stale for tessellation. It was true for the non-tess chunk loop, which 112 fixes.
- Rasterizer discard: same counting (compute side), and the raster draw is not needed.
- Multiview + tessellation: `multiviewTessellationShader = false`, so no view multiplier is needed.

Sol review: exact for points, isolines and triangles, and under conditional zeroing.

Related fix in 112: `panlib_tess_step` now caps the instances of a chunk at 2048. With 1-vertex patches, `max_patches / ppi` could make a VS lowering grid of tens of thousands of instances, which is the G615 hang size from 094.

## Tests (APK `large_draw`)

- `tess_direct` / `tess_indirect` (isolines, point mode, 70000 patches) now also check primitives generated == 140000. Before, it was only reported, not checked.
- `tess_tri_direct` / `tess_tri_indirect`: new `large_draw_tri.tesc`/`.tese`. Triangle domain with levels 1, so one triangle per patch. Checks 3 records per patch, `gl_PrimitiveID`, XFB written == primitives generated == 70000.
- `tess_instanced`: 1 patch x 9000 instances. Checks the counts (18000) and that the draw completes with the 2048 cap.
- `cond_skip_tess`: zero predicate, so XFB, the XFB query and primitives generated must all be 0.

G615 commands: see `112-prerast-gaps.md`. Also run `dEQP-VK.transform_feedback.primitives_generated_query.*` and `dEQP-VK.tessellation.*`.
