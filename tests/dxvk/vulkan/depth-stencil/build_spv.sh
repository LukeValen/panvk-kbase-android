#!/bin/sh
# Regenerate depth_stencil_spv.h from the GLSL shaders (glslangValidator).
set -e
cd "$(dirname "$0")"
T=$(mktemp -d)
G="glslangValidator -V --target-env vulkan1.1"

$G --vn ds_vert_spv -o "$T/vert.h" ds.vert >/dev/null
$G --vn ds_flat_vert_spv -o "$T/flat.h" ds_flat.vert >/dev/null
$G --vn ds_frag_spv -o "$T/frag.h" ds.frag >/dev/null

cat "$T/vert.h" "$T/flat.h" "$T/frag.h" | sed 's/^const uint32_t/static const uint32_t/' > depth_stencil_spv.h
rm -rf "$T"
