cmake_minimum_required(VERSION 3.22)
project(panvktest C)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../..")
set(VULKAN_INCLUDE_DIR "${REPO_ROOT}/work/mesa-dxint/include" CACHE PATH "Vulkan headers")

function(add_vk_test test_name test_rel_path)
    set(target_name "t_${test_name}")
    set(src_file "${REPO_ROOT}/${test_rel_path}")
    get_filename_component(src_dir "${src_file}" DIRECTORY)

    add_library(${target_name} SHARED "${src_file}")
    target_compile_options(${target_name} PRIVATE
        -include ${CMAKE_CURRENT_SOURCE_DIR}/pt_compat.h
        -w -O2 -fvisibility=default
    )
    target_include_directories(${target_name} PRIVATE
        "${VULKAN_INCLUDE_DIR}"
        "${src_dir}"
    )
    if(test_name STREQUAL "bc_decode")
        target_include_directories(${target_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compat")
    endif()
    target_link_libraries(${target_name} PRIVATE dl m)
endfunction()

# 10 standalone Vulkan test programs
add_vk_test(gpu_prerast_slice tests/dxvk/vulkan/gpu_prerast_slice.c)
add_vk_test(clip_cull tests/dxvk/vulkan/clip-cull/clip_cull.c)
add_vk_test(multi_viewport tests/dxvk/vulkan/multi-viewport/multi_viewport.c)
add_vk_test(fill_mode tests/dxvk/vulkan/fill-mode/fill_mode.c)
add_vk_test(bc_decode tests/dxvk/vulkan/bc/bc_decode.c)
add_vk_test(geometry tests/dxvk/vulkan/geometry/geometry.c)
add_vk_test(tessellation tests/dxvk/vulkan/tessellation/tessellation.c)
add_vk_test(xfb tests/dxvk/vulkan/xfb/xfb.c)
add_vk_test(pipeline_stats tests/dxvk/vulkan/pipeline_stats/pipeline_stats.c)
add_vk_test(vertex_stores tests/dxvk/vulkan/vertex-stores/vertex_stores.c)
add_vk_test(gs_viewport_depth device/gs-viewport-depth.c)
add_vk_test(vs_viewport_index device/vs-viewport-index.c)
add_vk_test(depth_bounds device/depth-bounds.c)
add_vk_test(large_draw tests/dxvk/vulkan/large-draw/large_draw.c)
add_vk_test(vmr_secondary tests/dxvk/vulkan/vmr-secondary/vmr_secondary.c)
add_vk_test(tess_cond_state tests/dxvk/vulkan/tess-cond-state/tess_cond_state.c)
add_vk_test(bachata_reqs tests/bachata/bachata_reqs.c)
add_vk_test(bachata_exec tests/bachata/bachata_exec.c)
add_vk_test(robustness2 tests/dxvk/vulkan/robustness2/robustness2.c)
add_vk_test(blend tests/dxvk/vulkan/blend/blend.c)
add_vk_test(occlusion_query tests/dxvk/vulkan/occlusion-query/occlusion_query.c)
add_vk_test(descriptor_model tests/dxvk/vulkan/descriptor-model/descriptor_model.c)
add_vk_test(shader_arith tests/dxvk/vulkan/shader-arith/shader_arith.c)
add_vk_test(depth_stencil tests/dxvk/vulkan/depth-stencil/depth_stencil.c)
add_vk_test(sampler tests/dxvk/vulkan/sampler/sampler.c)
add_vk_test(draw_params tests/dxvk/vulkan/draw-params/draw_params.c)
add_vk_test(submit_stress tests/dxvk/vulkan/submit_stress/submit_stress.c)
add_vk_test(csf_event device/csf-event-regression.c)
add_vk_test(gs_tess_primitive_id device/gs-tess-primitive-id.c)
add_vk_test(bachata_storage_fmtless tests/bachata/storage_fmtless.c)
add_vk_test(bachata_dynamic_render tests/bachata/dynamic_render.c)
# Requirement checkers: run with the Info page (compliance) and as suite tests (FAIL only on hard missing).
add_vk_test(dxvk_reqs tests/dxvk/vulkan/dxvk-reqs/dxvk_reqs.c)
add_vk_test(vkd3d_reqs tests/vkd3d/vkd3d_reqs.c)
# vkd3d execution tests (suite).
add_vk_test(vkd3d_heap tests/vkd3d/heap/vkd3d_heap.c)
add_vk_test(vkd3d_timeline tests/vkd3d/timeline/vkd3d_timeline.c)

# SHARED lib t_vkinfo from vkinfo.c
add_library(t_vkinfo SHARED "${CMAKE_CURRENT_SOURCE_DIR}/vkinfo.c")
target_compile_options(t_vkinfo PRIVATE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/pt_compat.h
    -w -O2 -fvisibility=default
)
target_include_directories(t_vkinfo PRIVATE
    "${VULKAN_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(t_vkinfo PRIVATE dl m)

# SHARED JNI lib panvktest from jni.c + swapchain.c
add_library(panvktest SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/jni.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/swapchain.c")
target_compile_options(panvktest PRIVATE -w -O2 -fvisibility=default)
target_compile_definitions(panvktest PRIVATE VK_USE_PLATFORM_ANDROID_KHR)
target_include_directories(panvktest PRIVATE
    "${VULKAN_INCLUDE_DIR}"
    "${REPO_ROOT}/tests/dxvk/vulkan/clip-cull")
target_link_libraries(panvktest PRIVATE android log dl m)

# Executable runner packaged as libpt_runner.so
add_executable(pt_runner runner.c)
target_compile_options(pt_runner PRIVATE -w -O2 -fvisibility=default)
set_target_properties(pt_runner PROPERTIES
    OUTPUT_NAME "libpt_runner.so"
    SUFFIX ""
    POSITION_INDEPENDENT_CODE TRUE
)
target_link_libraries(pt_runner PRIVATE dl log)
