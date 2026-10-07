#!/bin/sh
# Regenerate vkd3d_heap_spv.h from GLSL compute shaders (glslangValidator).
set -eu
cd "$(dirname "$0")"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT HUP INT TERM
G="glslangValidator -V --target-env vulkan1.3"
for name in ssbo_heap image_heap mutable_heap; do
    $G --vn "${name}_spv" -o "$T/$name.h" "$name.comp" >/dev/null
done
$G --vn uniform_texel_heap_spv -o "$T/uniform_texel_heap.h" texel_heap.comp >/dev/null
$G -DSTORAGE_HEAP=1 --vn storage_texel_heap_spv -o "$T/storage_texel_heap.h" texel_heap.comp >/dev/null
{
    printf '#ifndef VKD3D_HEAP_SPV_H\n#define VKD3D_HEAP_SPV_H\n#include <stdint.h>\n'
    cat "$T/ssbo_heap.h" "$T/uniform_texel_heap.h" "$T/storage_texel_heap.h" "$T/image_heap.h" "$T/mutable_heap.h" |
        sed 's/^const uint32_t/static const uint32_t/'
    printf '#endif\n'
} > vkd3d_heap_spv.h
