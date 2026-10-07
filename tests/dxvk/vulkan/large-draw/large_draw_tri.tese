#version 450
#extension GL_ARB_enhanced_layouts : enable

layout(triangles, equal_spacing, ccw) in;
layout(location = 0) in vec4 tc_v[];
layout(location = 0, xfb_buffer = 0, xfb_offset = 0, xfb_stride = 16) out vec4 out_val;

void main() {
   gl_Position = vec4(gl_TessCoord, 1.0);
   out_val = vec4(tc_v[0].x, tc_v[0].y, float(gl_PrimitiveID), 1.0);
}
