#version 450
layout(location = 0) out vec4 o;
/* R = primitive ID (unorm8 exact for IDs < 256). */
void main() { o = vec4(float(gl_PrimitiveID) / 255.0, 0.0, 0.0, 1.0); }
