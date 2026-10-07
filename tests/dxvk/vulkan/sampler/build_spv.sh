#!/bin/sh
# Regenerate sampler_spv.h from the GLSL compute shaders (glslangValidator).
set -e
cd "$(dirname "$0")"
T=$(mktemp -d)
G="glslangValidator -V --target-env vulkan1.1"

for s in s2d cube cubearr gather gather_ext; do
   $G --vn ${s}_spv -o "$T/$s.h" $s.comp >/dev/null
done

cat "$T/s2d.h" "$T/cube.h" "$T/cubearr.h" "$T/gather.h" "$T/gather_ext.h" |
   sed 's/^const uint32_t/static const uint32_t/' > sampler_spv.h
rm -rf "$T"
