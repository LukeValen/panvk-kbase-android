# 114: variableMultisampleRate in attachment-less secondaries and resumed passes

Patch: `patches/csf-v11/114-cut-attachment-less-secondaries-per-sample-count.patch`. Not committed. Not run on hardware.

## Gaps left by 095

- Mixed sample counts inside one attachment-less secondary: every draw used the count of the first draw. 095 only logged a warning.
- Attachment-less render passes suspended in one command buffer and resumed in another: the render context stayed open with the first count.

## Why 086 stays removed

`worklogs/driver-remaining/secondary-msaa.md` lists the 086 blockers:

- 086 emitted the render context inside the secondary, so `tile_size_px` stayed 0.
- 086 did not handle mixed counts inside one secondary.
- A resumed primary with `tiler == 0` overwrote the inherited context.

114 does none of these. The secondary never emits or splits a render context. Only the primary does that work, in `CmdExecuteCommands`, with full render-pass knowledge (`cmd_select_tile_size`, `split_render_ctx`).

## Change

- **Secondary.**
  - `vmr_track_secondary()` runs once per API draw, outside any CS block, in RENDER_PASS_CONTINUE secondaries with no attachments and no rasterizer discard.
  - When the count changes, `cmd_cut_vmr_segment()` cuts the segment:
    - It flushes the sync points and runs `cs_wait_slots(all)`.
    - It ends each non-empty CS builder and keeps its root chunk address, size and `req_resource_mask` in `cmdbuf->vmr_segs`, a `util_dynarray` with no cap.
    - It restarts the builders and dirties all draw state.
- **Primary, `CmdExecuteCommands`.**
  - For each segment, it prepares the render context with that segment's count (the split happens when the count changes), flushes its own sync points, then `cs_call`s the segment streams.
  - The tail stream follows the same steps.
  - The primary's sync points are now always flushed after the context work. This also fixes the 095 ordering.
- **Suspend/resume.**
  - An attachment-less pass that suspends ends its context (`split_render_ctx` instead of `get_render_ctx`).
  - `CmdBeginRendering` without attachments drops `RESUMING`, so the resumed part starts its own context with its own count.
  - `split_render_ctx` clears `RESUMING`.

## Limits

- Each attachment-less suspend/resume costs one extra fragment job.
- A segment cut stalls the secondary stream (`cs_wait_slots`). The cost is only paid when the count changes.
- Occlusion queries that span segment cuts inside one secondary are untested.

## Tests

APK `vmr_secondary` (`tests/dxvk/vulkan/vmr-secondary/vmr_secondary.c`):

- The test now enables `variableMultisampleRate` and SKIPs without it.
- `sec_mixed` is a normal case now (it was a known gap).
- New cases:
  - `sec_mixed_1414`
  - `sec_mixed_call`
  - `resume_cb_4x_1x`: suspend in one primary, resume in a second primary in the same submit.
  - `resume_cb_sec`

## Build

- Worktree: `/var/tmp/panvk/wt-114`, snapshot 4210afb (series through 109) plus 114/115/116.
  - It has one local build fix: `vk_android.h` included in `panvk_wsi.c`. That fix is not part of these patches.
- Patch check: 114, 115 and 116 apply in sequence on a clean 4210afb.
- Universal v10-v14 Android ICD: `/var/tmp/panvk/dist-114/libvulkan_panfrost.so`.
  - SHA256 `eb816af9c154e491da2a6ad0efd96b09191977b30db2c130465edb21374d6168`
  - BuildID `d492cf95c471de16556f7a6e50a5e79271a53227`
  - validate-binary PASS.
- APK with this ICD bundled: `/var/tmp/panvk/apk-114/app-debug.apk`.

## G615 commands (when connected)

```sh
adb install -r /var/tmp/panvk/apk-114/app-debug.apk
adb shell am force-stop dev.zenithblue.panvktest
adb shell am start -n dev.zenithblue.panvktest/.MainActivity --es driver bundled --es autorun vmr_secondary
adb shell run-as dev.zenithblue.panvktest cat files/autorun.txt   # want 13/13 PASS
adb shell am start -n dev.zenithblue.panvktest/.MainActivity --es driver bundled --es autorun all   # 17/17
# CTS (bionic deqp-vk + shim, ICD from dist-114):
deqp-vk --deqp-case='dEQP-VK.pipeline.*.multisample.variable_rate*'
deqp-vk --deqp-case='dEQP-VK.pipeline.*.multisample.mixed_count*'
```
