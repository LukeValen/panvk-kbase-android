# PanVK 1.1.4-FC — LukeValen Switch Safety

Base: GunaCharanTeja `g615-1.1.4-FC`, commit
`aa01eb0cd2d5e622ffbaa3a1e097857fe8ed8719`.
Mesa: `5a07217f034b3e50d8c7c7794f97a2df1742613b`.

This experimental Android driver variant ports the previous LukeValen Switch
compatibility and Safety v4 work onto the exact 1.1.4-FC sources. It targets
Strato, Eden and Lemon on the Kbase backend; it does not change those apps.

## Changes

- Probe native-buffer handles for an importable DMA-BUF, including MediaTek
  handles whose first descriptor is not the image buffer.
- Disable the DRM syncobj payload-copy callback on Kbase only. Validate the
  actual assignment, avoiding the Safety v3 false-positive patch skip.
- Complete Android native-buffer release work before returning fence `-1`.
  Do not enter the experimental KCPU fence-export path for this release.
- Export binary semaphore SYNC_FD by waiting for that semaphore's actual
  payload and returning `-1` only on success. Preserve pending-submit waits,
  binary/timeline validation, permanent-payload reset and temporary-payload
  restoration. Unlike the previous Safety v4 fallback, this waits only the
  requested payload instead of calling DeviceWaitIdle on unrelated queues.
- Preserve 64-bit GPU function addresses for provoking-vertex CS calls.
- Default `PANVK_KBASE_GPU_SEMAPHORE_WAITS`, `PANVK_LAZY_VTWAIT`,
  `PANVK_LAZY_CWAIT` and `PANVK_RP_MERGE` to false in this dedicated variant.
  The upstream implementations remain available with explicit environment
  opt-in. Upstream RDR1 query/empty-tiler fixes remain included.
- Avoid a warning on every synchronous frame release.
- Package exactly `meta.json` (the nine standard fields) and
  `libvulkan_panfrost.so`, with Android API 35 minimum and SHA-256 checksums.

## Build

The `Build PanVK 1.1.4-FC Switch Safety` Actions workflow produces the driver
ZIP on branch `switch-g615-1.1.4-fc-safety`. It builds a real Android static
libdrm dependency; no host/glibc libdrm is linked into the driver.

To prepare Mesa locally, run:

```sh
./scripts/fetch-mesa.sh
./scripts/apply-patches.sh --profile g615-v11-csf --switch-safety
```

The overlay is a separate final patch family, so it cannot be overwritten by
later upstream patches. Patch application fails on drift, and semantic
postconditions check the effective source before compilation. Without
`--switch-safety`, the upstream profile is unchanged.

## Validation limits

All 161 upstream patches plus six Switch patches apply to clean pinned Mesa.
Static checks validate the Kbase callback, successful waits before signaled
fences, semaphore reset semantics, 64-bit addresses and conservative defaults.
Compilation and ELF/ZIP validation are enforced by the build workflow.

Hardware testing in Strato/Eden/Lemon is still required. This reduces exposure
to the previously problematic synchronization paths; it is not a guarantee
against every deadlock or GPU fault. Synchronous presentation and conservative
barriers can reduce CPU/GPU overlap and FPS compared with upstream 1.1.4-FC.
No Vulkan features are fabricated, and no D3D engine identity is forced.
