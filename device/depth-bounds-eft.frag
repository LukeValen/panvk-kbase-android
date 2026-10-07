#version 450
layout(early_fragment_tests) in;
layout(location = 0) out vec4 o;
void main() { o = vec4(0.0, 1.0, 0.0, 1.0); }
