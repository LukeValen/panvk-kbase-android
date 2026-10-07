#version 450
layout(location = 0) flat in vec4 in_attr;
layout(location = 0) out vec4 out_color;

void main()
{
    out_color = vec4(any(notEqual(in_attr.xyz, vec3(0.0))) ? 1.0 : 0.0,
                     in_attr.w == 0.0 ? 0.0 : (in_attr.w == 1.0 ? 1.0 : 0.5),
                     0.0, 1.0);
}
