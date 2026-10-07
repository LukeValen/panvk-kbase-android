# 109 v12+ viewport depth runs, depthBounds, shaderOutputViewportIndex

Status: **patched, built, not run on v12/v13 hardware** (no G720/G925 available; G615 absent). PROGRESS item 16.

Patch: `patches/csf-v11/109-port-viewport-depth-runs-to-v12-and-lift-gates.patch` (base: beta16 series + csf-v11/108). Worktree `/var/tmp/panvk/wt-vpd`, build `/var/tmp/panvk/build-vpd`.

## Why v12+ was excluded

- v10/v11: 089/090 split a draw into ordered viewport runs and give each run its viewport's depth clamp through IDVS `LOW_DEPTH_CLAMP`/`HIGH_DEPTH_CLAMP` (SR 44/45).
- v12/v13/v14 genxml have no clamp registers. SR 44 is `VIEWPORT_HIGH` (Viewport words 0/1: Min/Max X/Y) and SR 46 is `VIEWPORT_LOW` (words 2/3: Min Depth, Max Depth). `prepare_vp()` (v12) loads both pairs. The Viewport struct is identical in v12.xml:1949, v13.xml:2253, v14.xml:2223.
- So 089 left the run split off on v12+ (`vps = NULL`, `gpu_prerast_vp_prepare` returned false): every viewport got the union depth range. That is the G720/G925 `gs_viewport_depth` failure (both viewports wrong depth). 090/092 then kept `shaderOutputViewportIndex` and `depthBounds` off on v12+ as untested.

## Change

- `gpu_prerast_vp_runs` (`csf/panvk_vX_cmd_draw.c`): the per-run z_min/z_max go to SR 46/47 on v12+ (`MALI_IDVS_SR_VIEWPORT_LOW`, `+1`), LOW/HIGH_DEPTH_CLAMP on v10/v11. The run loop is otherwise shared. x/y words keep the framebuffer box that `prepare_vp()` emits for the indexed case. After the runs `MESA_VK_DYNAMIC_VP_VIEWPORTS` is dirtied, so the next draw re-emits the whole viewport (same as v11).
- Removed the v12 exclusions: `vps = NULL` in `gpu_prerast_draw_generated`, the `return false` in `gpu_prerast_vp_prepare`, and `PAN_ARCH < 12` in the chunk-loop fail-closed check.
- `panvk_vX_physical_device.c`: `depthBounds` and `shaderOutputViewportIndex` are `PAN_ARCH >= 10`. The 092 emulation is arch-neutral (FS LD_TILE + sample mask; DCD `no_shader_depth_read`; v13+ HSR already sees `hsr.ld_tile` from `va_gather_hsr_info.c`).

## Verification

- Universal Android ICD (v10-v14) builds: `ninja -C /var/tmp/panvk/build-vpd`, 0 errors.
- v10/v11 codegen: `csf_panvk_vX_cmd_draw.c.o` disassembly differs only in 6 `mov w3, #line` (`__LINE__` of `vk_command_buffer_set_error`); data sections identical. `panvk_vX_physical_device.c.o` byte-identical.
- Review: codex gpt-6.1-sol, no blocking issues (SR mapping, viewport restore, tess/chunk path, depthBounds on v12-v14).

## Risks

- SR 46 = Min Depth / SR 47 = Max Depth comes from genxml + `decode_csf.c:948` + `prepare_vp`; not proven on hardware. A swap would show as failing `gs_viewport_depth` only.
- If the v12+ IDVS consumes the Viewport depth fields differently from the v11 clamp registers (e.g. as a cull range), per-viewport depth may still be wrong; the union-range behaviour it replaces was wrong anyway.
- depthBounds on v12+ inherits 092 limits (no FPK when lowered, EarlyFragmentTests + depth writes). DXVK enables it dynamically, so every DXVK pipeline on G720/G925 now pays the lowered-FS cost.

## Tester steps (G720 v12, G925 v13)

1. Install the build with 109; run full PanProbe. Must pass `gs_viewport_depth` (cases A_clamp, B_clamp_clip, C_noclamp), `vs_viewport_index`, `depth_bounds`; keep `multi_viewport`, `geometry`, `tessellation`, `large_draw`, `tess_cond_state` PASS (expected 17/17).
2. vulkaninfo: `depthBounds = true`, `shaderOutputViewportIndex = true`.
3. If `gs_viewport_depth` fails with depths swapped per viewport, swap SR 46/47; if a GPU fault appears, attach logcat + PanProbe zip.

Final combined build (2026-10-05, with 108/014/004): `/var/tmp/panvk/dist-final/libvulkan_panfrost.so` sha `12d49610...`, BuildID `479f5f57...`. Applies strictly; not device-tested on v12 (no G720 connected).
