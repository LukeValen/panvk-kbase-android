#version 450
layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec4 in_attr;
layout(location = 0) flat out vec4 out_attr;

void main()
{
    gl_Position = in_pos;
    out_attr = in_attr;
}
