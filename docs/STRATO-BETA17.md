# PanVK beta.17 — Strato Android native buffers

Base: upstream `g615-v11-csf-v0.1.0-beta.17`, commit
`b4ee2e2490ffad0e6625ecbd78ef016bdf6cf5ce`, pinned Mesa
`5a07217f034b3e50d8c7c7794f97a2df1742613b`.

Strato uses AdrenoTools to load the Android Vulkan HAL. Its native buffers
reach `vk_android_import_anb_memory`. Upstream's AHardwareBuffer fixes and
owned-swapchain probes do not replace this function's assumption that the
first native-handle FD is the graphics dma-buf.

The single additional driver patch checks each native-handle FD, rejects
negative/non-seekable/empty candidates, queries supported memory types and
selects one compatible with the image requirements. It retains the original
single allocation, CLOEXEC duplication and FD ownership rules. Non-handle
errors are propagated. The existing kbase FD-property query remains free
of transient GPU imports, avoiding the known VA-lifetime issue.

All shader, geometry, memory, synchronization and feature choices are the
upstream beta.17 choices. Earlier Eden-specific patch stacks are not included.
The emulator itself is unchanged.

`tests/test_strato_anb.py` exercises the actual patched production function
with ASan/UBSan, covering metadata/fence descriptors before the real buffer,
invalid handles, incompatible memory types, allocation errors and ownership.
The CI builds from the pinned upstream tree and records source/binary hashes.
The package contains only `meta.json` and `libvulkan_panfrost.so`, matching
Strato's strict metadata schema. Minimum Android API: 35.

Artifact: `PanVK-G615-Strato-Beta17-ANB1.zip`. Import through Strato's driver
selector; keep the previous driver installed for comparison. Hardware game
validation is pending; host tests and a successful import do not prove that
swapchain creation, rendering or all games are correct.
