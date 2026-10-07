# 110: EXEC_INIT before JIT_INIT (PROGRESS item 17)

Patch: `patches/jm-v9/004-init-exec-va-before-jit.patch`. It lives in jm-v9 rather than csf-v11 because it edits the JM sizing lines that jm-v9/001 adds. A csf-v11/110 copy would break the apply of jm-v9/001.

## Root cause (kbase source)
Sources: armbian/linux-rockchip `rk-5.10-rkr6` / `rk-6.1-rkr3` (bifrost r4x) and google-modules/gpu `android-gs-raviole-5.10-android12-d1` (JM) / `pantah-...-qpr1` (CSF). Copies are in `/var/tmp/panvk/kbase-src/`.
- `kbase_api_mem_exec_init` calls `kbase_region_tracker_init_exec(kctx, va_pages)`.
- `va_pages == 0` or `> KBASE_REG_ZONE_EXEC_VA_MAX_PAGES` (4G, 0x100000 pages) returns -EINVAL.
- `MALI_USE_CSF`: returns 0 straight away. The comment says "we now setup the EXEC_VA zone during initialization, so this request is a null-op" (CSF uAPI 1.9: "auto-initialization of EXEC_VA zone").
- JM, and CSF before 1.9 (raviole-era code has no CSF branch): `if (kbase_has_exec_va_zone_locked(kctx) || kctx->jit_va) return -EPERM`. The comment says "EXEC_VA zone must come before JIT's CUSTOM_VA". It also returns -ENOMEM if any allocation already exists. The zone is carved from the end of SAME_VA on 64-bit clients.
- JM `EXEC_INIT` ioctl exists from JM uAPI 11.13. There is no api_version gating in the dispatch.
- Without the zone, `kbase_mem_alloc` sends GPU_EX allocations that are not SAME_VA to CUSTOM_VA (`(flags & GPU_EX) && kbase_has_exec_va_zone()`, else custom). That is why v9 shaders still ran.
- Our `kbase_kmod_dev_create` ran JIT_INIT first and got EPERM on every JM / old-CSF context. jm-v9/001 also shrank the JM request to 1024 pages (4 MiB). Once the order is fixed, that size would cap all shader BOs at 4 MiB.

## Fix
- EXEC_INIT now runs before JIT_INIT and asks for 0x100000 pages on every arch.
- EPERM/EINVAL are logged with `mesa_logd`, because the CUSTOM_VA fallback still works. Any other errno logs a warning.

## CSF unchanged (G615 1.21, G720 ~1.30)
- On CSF >= 1.9, EXEC_INIT returns 0 before taking any lock or touching any zone, and the request size (0x100000) is the same as before.
- JIT_INIT still runs at the same point, with the same arguments.
- So on these kernels the only change is the ioctl order, and neither ioctl's effect depends on order. G615 was not run.

## Device: TB336FU G57 MC2 (JM, kernel reports uAPI 11.38)
- Raw probe (`/var/tmp/panvk/v9/execprobe.c`):
  - Old order: JIT ok, EXEC EPERM; GPU_EX BO at va 0x41000 (CUSTOM_VA).
  - New order: EXEC ok, JIT ok; GPU_EX BO at 0x7f00001000 (EXEC_VA, top 4G below 2^39).
  - A 2048-page GPU_EX BO also succeeds; it would not fit the old 1024-page JM zone.
- PanProbe (panvk-test 1.2.2, imported driver, `autorun all`): 1/17, the same as the baseline (vertex_stores passes; the rest are v9 feature gates).
  - Per-test Mesa logs: `EXEC_INIT failed` in 0/18 (the 19:50 baseline run had 17/18). No JIT_INIT failures and no OOM.
- deqp-vk (bionic + shim):
  - `memory.allocation.basic.*`: 102/102 pass, before and after.
  - `api.smoke.*`, 6 alternating runs per driver against a same-series base build (`dist-110base`): base 19/36, patched 21/36.
  - The triangle / asm_triangle / unused_resolve image compares flip between runs on both builds. This is a pre-existing v9 JM flake, not a regression.
- Afterwards, the driver files were restored: panvk-test `libimported.so` sha c0e549..., `/data/local/tmp/v9cts/libvulkan_panfrost.so` sha 8a2849.... Temporary files were removed.

## Build
- `/var/tmp/panvk/v9/mesa-110`: base 5a07217f plus the full g615-v11-csf profile (csf-v11 through 108, jm-v9 001-003), then this patch.
- `BDIR=/var/tmp/panvk/v9/build-110`, `DDIR=/var/tmp/panvk/v9/dist-110` (sha 3001cb5e...).

## Risks
- v10 old CSF (G610, 5.10) is inferred from the source only, not run. It should now get an EXEC_VA zone where it previously fell back to CUSTOM_VA.
- Shader VAs on JM move from CUSTOM_VA to the top 4G of SAME_VA. Shaders that need to share a 4G page are fine, because the zone is exactly one aligned 4G window.

Final combined build (2026-10-05, with 108/109/014): `/var/tmp/panvk/dist-final/libvulkan_panfrost.so` sha `12d49610...`, BuildID `479f5f57...`. TB336FU: PanProbe 1/17 (baseline), `EXEC_INIT failed` 0 (logcat + 19 per-test logs), deqp `memory.allocation.basic.*` 102/102.
