#version 450
#extension GL_ARB_enhanced_layouts : enable

/* One point per input triangle: the sum of its vertex indices and its
 * primitive ID.
 */
layout(triangles) in;
layout(points, max_vertices = 1) out;

layout(location = 0) in vec4 in_v[];
layout(location = 0, xfb_buffer = 0, xfb_offset = 0, xfb_stride = 16) out vec4 out_val;

void main() {
   gl_Position = vec4(0.0);
   out_val = vec4(in_v[0].x + in_v[1].x + in_v[2].x, in_v[0].y,
                  float(gl_PrimitiveIDIn), 1.0);
   EmitVertex();
}
