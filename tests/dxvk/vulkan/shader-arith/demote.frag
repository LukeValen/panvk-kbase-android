#version 450
#extension GL_EXT_demote_to_helper_invocation : require

layout(location = 0) out uint o;

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    float v = float(p.x);
    if (((p.x ^ p.y) & 1) != 0)
        demote;
    float h = helperInvocationEXT() ? 1.0 : 0.0;
    float d = dFdxFine(v);
    float dh = dFdxFine(h);
    o = helperInvocationEXT() ? 0xBADu : (0x600Du + uint(p.x) + 8u * uint(p.y) + (uint(abs(d)) << 16) + (uint(abs(dh)) << 20));
}
