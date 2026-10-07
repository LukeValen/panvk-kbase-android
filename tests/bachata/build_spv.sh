#!/bin/sh
# Regenerate bachata_exec_spv.h from GLSL compute shaders (glslangValidator).
set -e
cd "$(dirname "$0")"
T=$(mktemp -d)
G="glslangValidator -V --target-env vulkan1.3"

$G --vn int64_atomics_spv -o "$T/int64_atomics.h" int64_atomics.comp >/dev/null
$G --vn null_descriptor_spv -o "$T/null_descriptor.h" null_descriptor.comp >/dev/null
$G --vn bda_int64_spv -o "$T/bda_int64.h" bda_int64.comp >/dev/null

cat "$T/int64_atomics.h" "$T/null_descriptor.h" "$T/bda_int64.h" | sed 's/^const uint32_t/static const uint32_t/' > bachata_exec_spv.h

# storage_fmtless.c
$G --vn storage_fmtless_spv -o "$T/storage_fmtless.h" storage_fmtless.comp >/dev/null
sed 's/^const uint32_t/static const uint32_t/' "$T/storage_fmtless.h" > storage_fmtless_spv.h

# dynamic_render.c
$G --vn dynamic_render_vert_spv -o "$T/dr_vert.h" dynamic_render.vert >/dev/null
$G --vn dynamic_render_frag_spv -o "$T/dr_frag.h" dynamic_render.frag >/dev/null
cat "$T/dr_vert.h" "$T/dr_frag.h" | sed 's/^const uint32_t/static const uint32_t/' > dynamic_render_spv.h
rm -rf "$T"
