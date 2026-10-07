#!/bin/sh
# Regenerate draw_params_spv.h from GLSL (glslangValidator).
set -e
cd "$(dirname "$0")"
T=$(mktemp -d)
G="glslangValidator -V --target-env vulkan1.3"

$G --vn draw_params_vert_spv -o "$T/vert.h" draw_params.vert >/dev/null
$G --vn draw_params_frag_spv -o "$T/frag.h" draw_params.frag >/dev/null

cat "$T/vert.h" "$T/frag.h" | sed 's/^const uint32_t/static const uint32_t/' > draw_params_spv.h
rm -rf "$T"
