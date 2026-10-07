#!/bin/sh
# Regenerate blend_spv.h from the GLSL sources (glslangValidator).
set -e
cd "$(dirname "$0")"
T=$(mktemp -d)
glslangValidator -V --target-env vulkan1.1 --vn vs_blend -o "$T/vs_blend.h" \
   blend.vert >/dev/null
glslangValidator -V --target-env vulkan1.1 --vn fs_mrt -o "$T/fs_mrt.h" \
   blend_mrt.frag >/dev/null
glslangValidator -V --target-env vulkan1.1 --vn fs_dual -o "$T/fs_dual.h" \
   blend_dual.frag >/dev/null
glslangValidator -V --target-env vulkan1.1 --vn fs_logic -o "$T/fs_logic.h" \
   blend_logic.frag >/dev/null
cat "$T"/*.h | sed 's/^const uint32_t/static const uint32_t/' > blend_spv.h
rm -rf "$T"
