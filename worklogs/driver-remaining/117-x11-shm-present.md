# 117: X11 software present via MIT-SHM AttachFd, sw FIFO pacing

PROGRESS item 9. Patch: `patches/csf-v11/117-x11-sw-present-via-mit-shm-attach-fd-and-fifo-pacing.patch`
(wsi only, on top of 083/084/106). Launcher side: Winlator X server MIT-SHM 1.2 (uncommitted).
Review fix (2026-10-06): ShmPutImage now reads the offset and checks offset+image size against the segment capacity with 64-bit math (BadValue on overflow), then slices at the offset. The driver sends offset 0 with the full stride x height image.

## Which X server PanPlay uses

- Default `display_mode=builtin`: the Winlator Java X server inside the launcher
  (`com.winlator.xserver`, `BuiltinXServer.kt`). Optional `termux` mode uses Termux:X11.
- Builtin extensions: BIG-REQUESTS, MIT-SHM, Present, SYNC, XInputExtension. No DRI3, no RANDR.
  - MIT-SHM was 1.1: QueryVersion/Attach(SysV shmid via the `ANDROID_SYSVSHM_SERVER` broker)/
    Detach/PutImage. No AttachFd. QueryVersion reply was 17 bytes, not 32 (a strict xcb client
    would block on it).
  - Present 1.0: PresentPixmap copies at once and sends CompleteNotify with a fake 60 Hz MSC
    from `nanoTime`. NotifyMSC is BadImplementation. So server Present events cannot pace FIFO.
  - The connector already receives SCM_RIGHTS fds (`setCanReceiveAncillaryMessages(true)`).
- Mesa's own MIT-SHM sw path needs DRI3 + Present + SysV `shmget` + `VK_EXT_external_memory_host`
  (wants_shm). None of these exist here (bionic has no shmget, panvk has no host-pointer import).

## Changes

Driver (117):
- Swapchain create (sw, no upstream shm): probe MIT-SHM >= 1.2. Per image: memfd
  (`os_create_anonymous_file`), mmap, `xcb_shm_attach_fd_checked` (read-only, xcb closes the fd).
  Any failure -> whole chain stays on PutImage. `MESA_VK_WSI_DEBUG=noshm` disables.
- Present: one memcpy cpu_map -> segment (full frame, or per clipped damage rect), one
  `xcb_shm_put_image`. The per-present GetGeometry is sent after the put on this path, so its
  reply proves the server read the segment before the next copy (review finding: before, it was
  sent first).
- FIFO / FIFO_RELAXED: present thread sleeps to the next deadline of
  `MESA_VK_X11_SW_REFRESH_HZ` (1..1000; unset/0 = unpaced as before). A frame late by less than
  one interval keeps the grid; a full missed interval restarts it. MAILBOX/IMMEDIATE untouched.
  Present ids (084) and the present-thread fence wait (106) untouched.
- dlopen list (083) gains `xcb_shm_attach_fd_checked`, `xcb_shm_put_image`,
  `xcb_shm_query_version(_reply)`. Needs libxcb-shm >= 1.10; imagefs ships libxcb 1.17.0.

Launcher (Winlator X server, not committed):
- `MITSHMExtension.java`: version 1.2, opcode 6 AttachFd, QueryVersion reply padded to 32 bytes.
- `SHMSegmentManager.java`: `attachFd` (ParcelFileDescriptor size from fstat, `mapSHMSegment`
  read-only, fd closed after mmap), fd segments unmapped on Detach. Only memfd (regular file)
  is accepted; ashmem fds report size -1 and get BadSHMSegment -> client falls back.
- `Containers.kt`: graphical runs get `MESA_VK_X11_SW_REFRESH_HZ` = max supported display mode
  refresh (per-game env can override, `0` = unpaced).
- Known limit: segments of a client that dies without Detach stay mapped until the X server stops
  (same as the SysV path).

## DRI3 / Present decision

Not implemented. Builtin server has no DRI3; Present there is a copy with fake MSC. Our sw
images are CPU-mapped kbase memory, not dma-bufs a server could import, and panvk has no
host-pointer import for a zero-copy shm image. Termux:X11 mode is optional and also gives no
real vblank. Revisit only if the builtin server gets DRI3 with dma-buf import.

## Tests

Builds: Android ICD `/var/tmp/panvk/dist-117/libvulkan_panfrost.so`
SHA256 `ceaace4098a35169a51b1d353c5aa56299865d9856b3916aea2038f4400185cc`
(worktree `/var/tmp/panvk/wt-117` = wt-final snapshot 4210afb + wt-final's uncommitted
panvk_wsi.c/vk_android.{c,h} needed to compile + 117). No warnings in the wsi files.
Launcher `./gradlew :app:assembleDebug` OK (dirty main tree, not installed).

Host (no Xvfb; headless sway + Xwayland), `tests/x11-shm/x11_shm_present_test.c`, same request
sequence as the driver (memfd + AttachFd + memcpy + ShmPutImage + GetGeometry per frame),
readback via GetImage:

| server | size | ShmPutImage(attach_fd) fps | PutImage fps | readback |
|---|---|---|---|---|
| Xwayland | 1280x720 | 2532 / 2253 | 998 / 936 | ok |
| Xwayland | 1920x1080 | 475 / 445 | 280 / 301 | ok |
| Xwayland `-extension MIT-SHM` | both | falls back to PutImage | - | ok |

Run: `bash /var/tmp/panvk/x11shm-117/run.sh` (script there; builds the test, starts
`WLR_BACKENDS=headless sway`, Xwayland :77 / :78).

Device: not tested. G615 not connected; TB336FU locked by agent-110 (deqp) and only 1-2/17 in
PanProbe. The launcher half (Java AttachFd) is only compile-checked.

## G615 test commands (to do)

1. Install launcher APK with the MIT-SHM 1.2 change and driver
   `/var/tmp/panvk/dist-117/libvulkan_panfrost.so` (check sha above) as a PanPlay driver.
2. Termux:X11 1500-frame test (083/084 method, ~343 fps PutImage baseline): same swapchain
   present loop, 1500 frames, Xlib and XCB, IMMEDIATE and FIFO; with
   `MESA_VK_X11_SW_REFRESH_HZ` unset expect fps >= baseline; with `=60` FIFO ~60 fps, IMMEDIATE
   unchanged. Check `vkWaitForPresentKHR` ids and `VK_TIMEOUT` as in 084.
3. Builtin X server: dxcube shortcut ("dxcube long"), DXVK HUD full; compare
   `MESA_VK_WSI_DEBUG=noshm` (PutImage) vs default. Baseline 40-50 fps at 1280x720 PutImage.
   `files/xserver.log` must show no BadSHMSegment for the driver's segments.
4. NFS Most Wanted race window, DXVK HUD full, 4 samples x 2 runs, interleaved with beta.16:
   baseline 90.6 fps mean (p99 16-22 ms). Must not drop notably. Then per-game env
   `MESA_VK_X11_SW_REFRESH_HZ=0` to separate SHM from pacing.
5. FIFO pacing sanity: a FIFO app faster than the panel should sit at the panel rate
   (120 on Poco X6 Pro), not above.

## Launcher X server: per-client SHM segment lifetime (2026-10-06)

- `SHMSegmentManager` now records the owning `XClient` per xid. `XClient.freeResources()`
  (the disconnect hook that frees windows/pixmaps/GCs/cursors) calls `detachAll(client)`, so
  segments a client never ShmDetach'd are unmapped (memfd) or SysV-detached on disconnect.
  The AttachFd fd itself is already closed right after mmap (adoptFd try-with-resources).
- Per-client cap: 64 segments / 512 MiB total; Attach and AttachFd past it get BadAlloc.
- AttachFd maps exactly the fstat size (`getStatSize`); size <= 0 -> BadSHMSegment.
  Not covered: a client that ftruncate-shrinks its memfd after attach can still SIGBUS
  PutImage; fixing needs F_SEAL_SHRINK, which Mesa/xcb clients do not set.
  Driver side fixed (2026-10-06): 117 seals its memfds `F_SEAL_SHRINK|F_SEAL_GROW` before
  AttachFd (failure ignored, mesa_logd). Other clients remain unsealed.
- `assembleDebug` OK.
