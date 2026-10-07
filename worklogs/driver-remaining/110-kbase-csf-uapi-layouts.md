# 110: kbase CSF uAPI layouts, 16 KiB pages, MEM_ALLOC_EX fallback

Patches:
- `patches/csf-v11/110-select-kbase-csf-uapi-layouts-and-16k-pages.patch`
- `patches/jm-v9/005-size-exec-jit-zones-and-imports-in-kernel-pages.patch`

jm-v9/005 is the part that touches code jm-v9 adds or moves. The universal series applies jm-v9 after csf-v11, so these changes cannot go in 110.

PROGRESS items 18 and 20, plus the kbase-gaps open problem.

## Sources checked (public kbase)
Headers and `mali_kbase_core_linux.c` were fetched to `/var/tmp/panvk/uapi-hdrs/`:
- android.googlesource.com kernel/google-modules/gpu: `android-gs-raviole-5.10-android13` (CSF 1.10), `android-gs-pantah-5.10-android14` (1.14), `android-gs-caimito-6.1-android15` (1.24).
- github.com/rockchip-linux/kernel: `develop-6.1` (1.30), `develop-5.10` (1.31).

| ioctl | Layout | nr | Size | uAPI |
|---|---|---|---|---|
| CS_QUEUE_GROUP_CREATE | `_1_6` | 42 | 32 | all CSF (compat case kept up to 1.31) |
| CS_QUEUE_GROUP_CREATE | `_1_18` | 58 | 40 | 1.7+ (`reserved` u64, `csi_handlers` 1.12+, later named `dvs_buf`); kept as compat from 1.19 |
| CS_QUEUE_GROUP_CREATE | current | 58 | 112 | 1.19+ (`padding[9]` tail); `cs_fault_report_enable` (byte 31) 1.25+ |
| CS_TILER_HEAP_INIT | `_1_13` | 48 | 16 | all; compat from 1.14 (kernel passes `buf_desc_va = 0`) |
| CS_TILER_HEAP_INIT | current | 48 | 24 | 1.14+ (`buf_desc_va`) |
| MEM_ALLOC_EX | - | 59 | 64 | CSF 1.9+ (raviole 1.10 through rk 1.31, number unchanged) |
| MEM_ALLOC | - | 5 | 32 | all; on CSF the kernel builds an ALLOC_EX with `fixed_address = 0` |

Kernel compat handlers (`kbasep_cs_queue_group_create_1_6/_1_18`, `kbasep_cs_tiler_heap_init_1_13`, `kbase_api_mem_alloc`) copy only fields that we set identically. Any accepted layout therefore yields the same kernel request.

## Selection (`kbase_kmod.h`: `kbase_csf_group_create_layouts`, `kbase_csf_tiler_heap_init_layouts`)
- Group create:
  - 1.25+: 112 (fault report on), then 32, then 40.
  - 1.19-1.24: 32, 112, 40.
  - 1.7-1.18: 32, 40.
  - Below 1.7: 32 only.
- Heap init: 16 first; on 1.14+, 24 next.
- Only EINVAL or ENOTTY moves to the next layout. ENOTTY is what an unknown nr/size gives (`-ENOIOCTLCMD`). Other errors, such as ENOMEM, fail at once.
- The first accepted layout is cached per device (atomic cmpxchg) and logged once.
- MEM_ALLOC_EX: on ENOTTY/EINVAL while no allocation has succeeded yet, the driver warns and uses MEM_ALLOC. The working path is cached.

Behaviour on known devices:
- G615 (1.21): 32-byte group create and 16-byte heap init, then ALLOC_EX, the same ioctls as beta.16.
- G720 (~1.30): 112-byte group create with fault reporting, 16-byte heap init and ALLOC_EX, also the same as before.
- The only change on these devices is the new INFO lines.
- Failure path change: before 110, any error on the 1.25+ path fell back to 1.6. Now only EINVAL/ENOTTY does.

Host check: `/var/tmp/panvk/wt110-test/t.c` asserts the tables for 1.0, 1.6, 1.7, 1.10, 1.18, 1.19, 1.21, 1.24, 1.25, 1.30, 2.0 and the heap cases. It passes (`layout selection OK`).

## 16 KiB pages
`LOCAL_PAGE_SHIFT = PAGE_SHIFT` in kernel uAPI headers, so kbase uses the kernel page size everywhere. Changes, all via `kbase_kmod_page_size()` (cached `sysconf(_SC_PAGESIZE)`):
- `const page_size = 4096` in alias, import_host, dma-buf import, dma-heap alloc and BO alloc.
- Special mmap handles via `KBASE_MMAP_HANDLE()`: TRACKING (3), CSF USER_REG (47). Without this, 16 KiB kernels see a misaligned offset (EINVAL).
- CS USER_IO mapping: `BASEP_QUEUE_NR_MMAP_USER_PAGES * page`. The kernel requires exactly 3 kernel pages. The panvk input/output pages are at `+page` and `+2*page` (`mali_kbase_mem_linux.c` `kbase_csf_user_io_pages_vm_fault`).
- EXEC_INIT is 4 GiB / page and JIT_INIT is 128 GiB / page (jm-v9/005). On 4 KiB kernels these are the same 0x100000 and 1 << 25 values as before.
- Left at 4096 on purpose: single-page mmap/munmap lengths (the kernel rounds them up), and `pgsize_bitmap` (the GPU MMU granule; unused by the kbase paths).

Not done: no 16 KiB device to test on. Every tester kernel with a page-size suffix is `-4k`.

## Build / validation
- Series = repo patches as of 2026-10-06 (with 112 and 117), plus 110, 111 and jm-v9/005. 118 is excluded.
  - Applied with `scripts/apply-patches.sh` into `/var/tmp/panvk/wt-110e2e`: 120 patches, strict.
  - Android ICD `/var/tmp/panvk/dist-110/libvulkan_panfrost.so`: SHA256 `ce0ac1332d9ef3d016c4126e484b5113c8db6c1f353d19dc425cd66deab57b8c`, BuildID `987ecdb2fb5bc90c0a7bdb8cac6369c847e5c361`.
  - validate-binary PASS; 7 NEEDED, unchanged.
- **118 conflict:** `csf-v11/118-log-kbase-and-device-decisions.patch` kbase_kmod.c hunks 1-4 no longer apply after 110. They log the group-create layout, the heap-init layout and the uAPI version/page size, which 110 already does. 118 needs a rebase that drops those hunks. Its EXEC_INIT hunk targets JM-moved code.
- TB336FU G57 (v9 JM; the shared kmod paths), with the earlier 110 build that differs only in single-page mmap lengths and alloc logging:
  - deqp `memory.allocation.basic.*` 102/102.
  - logcat `kbase: JM driver, uAPI version 11.38, page_size=4096`.
  - Driver restored to sha 8a2849....
  - A re-run on the final build was skipped because the tablet lock was held by upload-logs-agent.

## Tester checks
- G610 (v10) PanProbe logcat:
  - `kbase: CSF driver, uAPI version 1.x, page_size=4096`
  - `kbase: queue_group_create layout=32 bytes` (40 or 112 means the fallback was needed)
  - `kbase: tiler_heap_init layout=16 bytes`
  - `kbase: mem_alloc via ALLOC_EX` (`ALLOC` if uAPI < 1.9)
  - no `layout=... failed` warnings
- G615/G720: the same lines; G720 must show `layout=112 bytes`. PanProbe should show no regression (17/17 and 14/17 before 109).
- G1-Ultra (v14):
  - `KBASE_IOCTL_MEM_ALLOC_EX failed ... trying KBASE_IOCTL_MEM_ALLOC`, then `kbase: mem_alloc via ALLOC`.
  - `vkCreateDevice` succeeds.
  - Send the uAPI version line.

## Risks
- None of the CSF paths has run on hardware. The selection only changes failure paths on G615/G720.
- An unknown vendor uAPI (v14) may break more than MEM_ALLOC_EX, for example the KCPU ABI or other sizes. The version line will show this.
- Pre-1.9 CSF kernels do not exist in tester data. The table covers them from the public changelog.
