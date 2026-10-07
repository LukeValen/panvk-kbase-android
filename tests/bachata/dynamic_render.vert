#version 450

layout(push_constant) uniform PushConstants {
    float z;
} pc;

layout(location = 0) out vec2 outUV;

void main() {
    const vec2 pos[6] = vec2[6](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2(-1.0,  1.0),
        vec2(-1.0,  1.0),
        vec2( 1.0, -1.0),
        vec2( 1.0,  1.0)
    );

    vec2 p = pos[gl_VertexIndex];
    gl_Position = vec4(p, pc.z, 1.0);
    outUV = p + vec2(0.5, 0.5);
}
