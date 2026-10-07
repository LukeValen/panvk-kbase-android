# 115: FS gl_PrimitiveID across VS viewport runs

Patch: `patches/csf-v11/115-keep-the-fs-primitive-id-across-viewport-runs.patch`. Not committed. Not run on hardware.

## Gap

A VS that writes `ViewportIndex` is drawn as one IDVS draw per viewport run (089/109). The hardware primitive index restarts at every run draw, so the FS saw `gl_PrimitiveID` counting from 0 in each run. Because the run list flattens instances, the ID also did not restart per instance.

## Change

This applies when the FS reads PrimitiveID and the VS has no PrimitiveID output. TES already writes the patch ID and is unchanged.

- **Run builder** (`libpan/vp_runs.cl`, `pack_prim_id`):
  - It stores `record | (primitive ID & 0xffff) << 16` in each run index.
  - The ID counts across restarts and restarts with each instance.
  - It adds `prim_base`, the first primitive of a CPU chunk (`gpu_prerast_prim_base`).
  - A record above 0xffff overflows the draw, and the draw fails closed.
- **VS passthrough** (`panvk_gpu_prerast_vs_vp_passthrough_nir`):
  - It reads the record from bits 0-15.
  - It outputs `PRIMITIVE_ID = index >> 16`. The tiler takes that value through `primitive_index_override`.
- **Restart:** run draws already have primitive restart off, so every packed value is a vertex.

## Limits

- IDs wrap past 65535 (16 bits). To widen, add a side buffer indexed by the record.
- The FS gets primitive ID 0 for polygon-mode LINE/POINT viewport-index draws.
- GPU-chunked (094) large or indirect draws restart the ID per chunk. CPU chunks are correct.

## Tests

- Host: `src/panfrost/libpan/tests/vp_runs.c` covers packing, counting across restarts, the 16-bit record overflow and `prim_base` wrap. Result: `vp_runs ok`.
- APK `vs_viewport_index` case `G_vs_fs_primitive_id` (`device/vs-viewport-index.c`, `device/vs-viewport-index-pid.frag`):
  - 5 primitives alternate viewports.
  - The FS writes its ID to R.
  - Expected values: left 4, right 3. Before this patch both are 0.
  - SKIP without `geometryShader`, because the FS PrimitiveId capability needs it.

## Build / commands

The ICD and APK are the same as 114: `worklogs/driver-remaining/114-secondary-vmr-segments.md`.

```sh
adb shell am start -n dev.zenithblue.panvktest/.MainActivity --es driver bundled --es autorun vs_viewport_index
adb shell run-as dev.zenithblue.panvktest cat files/autorun.txt   # G_vs_fs_primitive_id PASS
deqp-vk --deqp-case='dEQP-VK.draw.*shader_viewport_index*'
# beta.13 baseline: fragment_shader_2..16 failed (60). Expect fewer.
```
