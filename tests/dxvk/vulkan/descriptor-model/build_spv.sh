#!/bin/sh
# Regenerate descriptor_model_spv.h from GLSL compute shaders (glslangValidator).
set -e
cd "$(dirname "$0")"
T=$(mktemp -d)
G="glslangValidator -V --target-env vulkan1.2"

$G --vn bda_spv -o "$T/bda.h" bda.comp >/dev/null
$G --vn runtime_array_spv -o "$T/runtime_array.h" runtime_array.comp >/dev/null
$G --vn update_after_bind_spv -o "$T/update_after_bind.h" update_after_bind.comp >/dev/null
$G --vn partially_bound_spv -o "$T/partially_bound.h" partially_bound.comp >/dev/null
$G --vn inline_uniform_spv -o "$T/inline_uniform.h" inline_uniform.comp >/dev/null
$G --vn scalar_layout_spv -o "$T/scalar_layout.h" scalar_layout.comp >/dev/null

cat "$T/bda.h" "$T/runtime_array.h" "$T/update_after_bind.h" "$T/partially_bound.h" "$T/inline_uniform.h" "$T/scalar_layout.h" | sed 's/^const uint32_t/static const uint32_t/' > descriptor_model_spv.h
rm -rf "$T"
