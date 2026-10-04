# Eden Integration Changelog

This file tracks work specific to the Eden Android Switch-emulator fork.
Historical upstream release notes remain in the root `CHANGELOG.md`.

## 0.1.0-eden-dev.1

### Target

- Eden Android Nintendo Switch emulation
- Poco X6 Pro / Dimensity 8300 Ultra / Mali-G615 MC6
- Pan arch v11 / Kbase CSF
- Requires a specific experimental Eden build that is **not distributed here**

### Fixed / changed

- Enabled the Android Native Buffer path required by Eden.
- Added MediaTek vendor mapper metadata handling.
- Added native-handle DMA-BUF discovery instead of assuming FD index 0.
- Confirmed the valid DMA-BUF is discoverable by
  `GetMemoryFdPropertiesKHR` on the reference device.
- Removed the previous `VK_ERROR_INVALID_EXTERNAL_HANDLE` blocker.
- Added release-sync diagnostics around `QueueSignalReleaseImageANDROID`.
- Identified a Kbase/DRM mismatch where the Kbase device selected
  `vk_drm_syncobj_copy_payloads` and failed with `VK_ERROR_UNKNOWN (-13)`.
- Added `103-disable-drm-sync-copy-on-kbase.patch` so Kbase uses the normal
  queue-submit plus `SYNC_FD` export path.
- Added KCPU/CQS sync-file export diagnostics.
- Added a dedicated Eden-only CI/package target with Android WSI and Kbase
  only.

### Still under validation

- Continuous frame presentation.
- Release-fence export through Kbase KCPU queues.
- Game-specific crashes after successful Vulkan initialization.
- Pause/resume behavior when a rendered frame is visible only after the
  Surface state changes.
