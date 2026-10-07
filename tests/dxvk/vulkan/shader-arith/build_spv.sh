#!/bin/sh
# Regenerate shader_arith_spv.h from GLSL compute/graphics shaders (glslangValidator).
set -e
cd "$(dirname "$0")"
T=$(mktemp -d)
G="glslangValidator -V --target-env vulkan1.2"

$G --vn int8_spv -o "$T/int8.h" int8.comp >/dev/null
$G --vn int16_spv -o "$T/int16.h" int16.comp >/dev/null
$G --vn int64_spv -o "$T/int64.h" int64.comp >/dev/null
$G --vn storage8_spv -o "$T/storage8.h" storage8.comp >/dev/null
$G --vn storage16_spv -o "$T/storage16.h" storage16.comp >/dev/null
$G --vn demote_vert_spv -o "$T/demote_vert.h" demote.vert >/dev/null
$G --vn demote_frag_spv -o "$T/demote_frag.h" demote.frag >/dev/null
$G --vn subgroup_size_control_spv -o "$T/subgroup_size_control.h" subgroup_size_control.comp >/dev/null
$G --vn zero_init_a_spv -o "$T/zero_init_a.h" zero_init_a.comp >/dev/null
$G --vn zero_init_b_spv -o "$T/zero_init_b.h" zero_init_b.comp >/dev/null
$G --vn zero_init_c_spv -o "$T/zero_init_c.h" zero_init_c.comp >/dev/null

cat "$T/int8.h" "$T/int16.h" "$T/int64.h" "$T/storage8.h" "$T/storage16.h" \
    "$T/demote_vert.h" "$T/demote_frag.h" "$T/subgroup_size_control.h" \
    "$T/zero_init_a.h" "$T/zero_init_b.h" "$T/zero_init_c.h" | sed 's/^const uint32_t/static const uint32_t/' > shader_arith_spv.h
rm -rf "$T"
