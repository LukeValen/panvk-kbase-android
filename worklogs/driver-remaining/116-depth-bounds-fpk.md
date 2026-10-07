# 116: depthBounds emulation keeps FPK while off, EarlyFragmentTests ordering

Patch: `patches/csf-v11/116-keep-fpk-and-early-zs-while-depth-bounds-is-off.patch`. Not committed. Not run on hardware.

**Rework (2026-10-06, beta.17b), after review:** the FPK-while-off part was dropped. With the test off, the bound FS is still the lowered one and writes SampleMask, so a DCD that claims no coverage write and allows FPK did not match that shader. The DCD now uses the lowered shader's real info. Only the db_on EarlyFragmentTests late-ZS fix and `depth_bounds_app_mask` remain. The FPK sections below describe the old version. Getting FPK back would need an unlowered FS variant that is bound while the test is off.

## Gaps left by 092

- **FPK and early ZS lost while the test is off.**
  - 092 lowers every FS of a pipeline whose depth bounds test is dynamic. That covers all DXVK pipelines.
  - The lowering always writes SampleMask, so the draw lost forward pixel kill and got a late ZS update even with the test off.
  - This affects v10/v11, and v12+ since 109.
- **EarlyFragmentTests ordering.**
  - With the test on, the early ZS update wrote the new depth before the lowered test read it.
  - The test therefore compared the fragment's own depth, not the stored depth.
  - Samples outside the bounds also kept their ZS writes.

## Change

- **Shader compile** (`panvk_vX_shader.c`, `panvk_depth_bounds_off_info()`):
  - For a lowered FS, it also computes the app-only coverage, `can_fpk` and the early-ZS LUT, as if the shader were not lowered.
  - It uses the same formulas as `pan_compiler.c`.
  - `depth_bounds_app_mask` is serialized with the shader.
- **Draw** (`build_dcd_flags`):
  - When the test is off or no depth attachment is bound, the draw uses the app's values: `shader_modifies_coverage`, FPK and the earlyzs LUT.
  - `z_read` follows only the enabled test.
- **EarlyFragmentTests with the test on:**
  - Condition: the FS has depth/stencil writes or an occlusion query.
  - The early test stays, but the ZS update moves after the FS (`MALI_PIXEL_KILL_FORCE_LATE` update).
  - The bounds test then reads the stored depth, and samples it kills write nothing.

## Limits (spec-allowed or documented)

- An EFT FS that also discards, writes SampleMask or uses alpha-to-coverage keeps the 092 behaviour. Those must not gate EFT ZS writes.
- EFT fragments that fail the bounds test still run their side effects (stores, atomics), because the emulated test runs inside the FS.
- On v13+, the HSR `ld_tile` path still disables HSR culling while the test is on.

## Tests

APK `depth_bounds` (`device/depth-bounds.c`, `device/depth-bounds-eft.frag`):

- `H_eft_depth_write`: EFT FS, depth write 0.9, static bounds [0.3, 0.6]. Expected: green and depth 0.9 only where the stored depth is inside the bounds. The old ordering kills everything.
- `I_perf_dyn_off`: 2000 overdraw strips with the dynamic enable off, compared with a pipeline without the bounds test. FAIL if more than 1.5x slower (both above 2 ms).
  - This is a coarse CPU wall-clock check. Read the PERF line.

## Build / commands

The ICD and APK are the same as 114: `worklogs/driver-remaining/114-secondary-vmr-segments.md`.

```sh
adb shell am start -n dev.zenithblue.panvktest/.MainActivity --es driver bundled --es autorun depth_bounds
adb shell run-as dev.zenithblue.panvktest cat files/autorun.txt   # A-I PASS, note plain/dyn_off ms
deqp-vk --deqp-case='dEQP-VK.pipeline.*depth_bounds*'
deqp-vk --deqp-case='dEQP-VK.dynamic_state.*depth_bounds*'
# NFS race HUD (DXVK sets the enable dynamically): expect >= beta.16 fps
```
