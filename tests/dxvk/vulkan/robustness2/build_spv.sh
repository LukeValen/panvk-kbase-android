#!/bin/sh
# Regenerate robustness2_spv.h from GLSL shaders (glslangValidator).
set -e
cd "$(dirname "$0")"
T=$(mktemp -d)
G="glslangValidator -V --target-env vulkan1.3"

$G --vn null_desc_spv -o "$T/null_desc.h" null_desc.comp >/dev/null
$G --vn oob_spv -o "$T/oob.h" oob.comp >/dev/null
$G --vn vbuf_vert_spv -o "$T/vbuf_vert.h" vbuf.vert >/dev/null
$G --vn vbuf_frag_spv -o "$T/vbuf_frag.h" vbuf.frag >/dev/null

cat "$T/null_desc.h" "$T/oob.h" "$T/vbuf_vert.h" "$T/vbuf_frag.h" | sed 's/^const uint32_t/static const uint32_t/' > robustness2_spv.h
rm -rf "$T"
