# 108: BC emulation decision (PROGRESS item 14)

Patch: `patches/csf-v11/108-emulate-bc-unless-every-bc-format-is-native.patch` (not committed, not released).

## Bug

`panvk_bc_emul_enabled()` disabled emulation when kbase `TEXTURE_FEATURES` had BC1. G57 (v9) and G610 (v10) report BC1 only, while `has_texture_compression_bc()` needs all ten BC bits (`0x1ff80`). Result: `textureCompressionBC = false`, `get_image_plane_format_features()` returned 0 for every BC format, DXVK skipped the adapter.

## Fix

- `panvk_bc_emul_enabled()`: emulate unless all ten BC formats are native (`hw != 0` and texfeat bit set).
- `get_image_plane_format_features()`: with emulation off, BC formats fall through to the native per-format path instead of returning 0.
- All callers go through `panvk_bc_emul_enabled()`: feature bit (`panvk_vX_physical_device.c:338`), format features, `panvk_image_init` (`panvk_image.c:674`), image views and meta copies (via `image->bc_emul`). `PANVK_DEBUG=no_bc_emul` still forces native-only.
- GPUs without native BC1 (G615, G720, G925) keep the same decision as before.

## Validation

- Build: universal Android ICD (`g615-v11-csf` + jm-v9, archs v6-v14) OK, from worktree `/var/tmp/panvk/wt-bc` (pinned base + 111 patches).
- Device: Lenovo TB336FU, Mali-G57 MC2 (v9 JM), bionic deqp-vk + shim (`--deqp-vk-library-path=libvulkan_shim.so`). QPA logs in `/var/tmp/panvk/bc-v9-qpa/`.
  - `dEQP-VK.info.device_features`: `textureCompressionBC` 0 (beta.16) -> 1.
  - `dEQP-VK.api.info.format_properties.bc*`: 16/16 pass (before and after).
  - `dEQP-VK.texture.compressed.*bc*_2d_pot`: mostly Image verification failed. Native `etc2_r8g8b8a8` and `astc_4x4` 2d_pot fail the same way on v9, so this is a pre-existing v9 JM sampling/upload issue, not BC decode specific.
- G615 dev device and G610 not connected: no v10/v11 device run. v11 decision is unchanged by construction (BC1 not native there).

## Open

- v9: compressed-texture image verification fails for native and emulated formats; needs its own item.
- `cmd_bc_decode_zero_initialized` (patch 040) is hooked only in the CSF `CmdPipelineBarrier2`; JM v9 misses BC decode on leaving `ZERO_INITIALIZED` layout. Rest of the emulation (`panvk_vX_cmd_meta.c`, image views) is per-arch and builds for v9.
- v9 DXVK still blocked by Vulkan 1.1 (item 6). v10 G610 needs tester rerun to confirm DXVK picks the adapter.

Final combined build (2026-10-05, with 109/014/004): `/var/tmp/panvk/dist-final/libvulkan_panfrost.so` sha `12d49610...`, BuildID `479f5f57...`. TB336FU: `textureCompressionBC = 1`, PanProbe bc_decode raw/copy PASS, blit FAIL (known v9).
