#!/usr/bin/env python3
"""Fail the build if the pinned Switch safety overlay is missing or drifts."""
import re
import sys
from pathlib import Path

mesa = Path(sys.argv[1] if len(sys.argv) > 1 else 'work/mesa')
def read(path):
    return (mesa / path).read_text()
def require(condition, message):
    if not condition:
        raise SystemExit('SWITCH SAFETY FAILED: ' + message)

device = read('src/panfrost/vulkan/panvk_vX_device.c')
require(re.search(r'device->vk\.copy_sync_payloads = physical_device->kbase_node_path\[0\]\s*\? NULL\s*:\s*vk_drm_syncobj_copy_payloads;', device), 'Kbase must not use DRM payload-copy callback')
anb = read('src/vulkan/runtime/vk_android.c')
require('[ANB-FD] probing' in anb, 'native-buffer DMA-BUF probing missing')
release = anb[anb.index('vk_common_QueueSignalReleaseImageANDROID('):]
require(re.search(r'QueueWaitIdle\(_queue\).*?if \(idle_result != VK_SUCCESS\).*?return idle_result;.*?DestroySemaphore.*?queue->anb_semaphore = VK_NULL_HANDLE;.*?\*pNativeFenceFd = -1;', release, re.S), 'native fence must only signal after successful queue completion')
require('[ANB-SYNC] Eden/Kbase synchronous release QueueWaitIdle=%d' not in anb, 'per-frame log overhead restored')
sem = read('src/vulkan/runtime/vk_semaphore.c')
export = sem[sem.index('vk_common_GetSemaphoreFdKHR('):]
require('Cannot export a timeline semaphore as SYNC_FD' in export, 'binary semaphore validation missing')
require('VK_SYNC_WAIT_PENDING' in export, 'threaded submit synchronization missing')
require(re.search(r'vk_sync_wait\(device, sync, 0, VK_SYNC_WAIT_COMPLETE,\s*UINT64_MAX\);\s*if \(unlikely\(result != VK_SUCCESS\)\)\s*return result;\s*\*pFd = -1;', export), 'SYNC_FD must wait for the actual payload and propagate failures')
require('DeviceWaitIdle' not in export.replace('DeviceWaitIdle can', ''), 'unrelated queues must not be waited on')
require(export.index('*pFd = -1;') < export.index('vk_sync_reset(device, sync)') < export.index('vk_semaphore_reset_temporary(device, semaphore)'), 'export must preserve payload reset and temporary restoration')
queue = read('src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c')
require(re.search(r'DEBUG_GET_ONCE_BOOL_OPTION\(kbase_gpu_semaphore_waits,\s*"PANVK_KBASE_GPU_SEMAPHORE_WAITS", false\)', queue), 'cross-subqueue GPU waits enabled by default')
draw = read('src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c')
require('uint64_t fn_addr =' in draw and 'uint32_t fn_addr =' not in draw, 'truncated GPU function address')
cmd = read('src/panfrost/vulkan/csf/panvk_vX_cmd_buffer.c')
for option, text in [('PANVK_LAZY_VTWAIT', cmd), ('PANVK_LAZY_CWAIT', cmd), ('PANVK_RP_MERGE', draw)]:
    require(f'debug_get_bool_option("{option}", false)' in text, option + ' must be opt-in')
require('!strcmp(engine, "vkd3d") || !strcmp(engine, "DXVK")' in cmd, 'upstream strict Vulkan engine selection changed')
print('ALL SWITCH SAFETY POSTCONDITIONS PASSED')
