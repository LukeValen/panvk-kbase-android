#!/bin/sh
# Regenerate occlusion_query_spv.h from the GLSL sources (glslangValidator).
set -e
cd "$(dirname "$0")"
T=$(mktemp -d)
glslangValidator -V --target-env vulkan1.1 --vn vs_oq -o "$T/vs.h" \
   occlusion_query.vert >/dev/null
glslangValidator -V --target-env vulkan1.1 --vn fs_oq -o "$T/fs.h" \
   occlusion_query.frag >/dev/null
cat "$T"/*.h | sed 's/^const uint32_t/static const uint32_t/' > occlusion_query_spv.h
rm -rf "$T"
