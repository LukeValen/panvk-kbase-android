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
- Added `104-eden-anb-sync-fallback.patch`: if PanVK submits the native-buffer
  release successfully but Kbase `SYNC_FD` export fails, Eden drains the queue
  synchronously and returns fence `-1` (already complete) instead of failing
  presentation. The normal KCPU fence-export path remains preferred.

### Still under validation

- Continuous frame presentation.
- Release-fence export through Kbase KCPU queues.
- Game-specific crashes after successful Vulkan initialization.
- Pause/resume behavior when a rendered frame is visible only after the
  Surface state changes.

## 0.1.0-eden-dev.8

- CI run: `37232907395` — success.
- Eden-only build: Android WSI only, Kbase only, no X11 WSI, no Panthor KMD.
- Includes patches 103 and 104 for Kbase release-fence stability.
- Driver SHA256: `39cb25612b2e083c65b4a29f8b5060e16b77b7b375f17fff27518c3cb7fb7248`.
- Package SHA256: `0087bc9315ecf9dffa97fdb5499f493b1e3026c2b2cd4323708ad28fe51073a2`.
- Package: `PanVK-G615-0.1.0-eden-dev.8-eden.zip`.
- This build is for on-device Eden validation and is not a stable release.
