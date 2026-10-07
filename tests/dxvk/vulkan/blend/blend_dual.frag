#version 450

layout(location = 0, index = 0) out vec4 out0;
layout(location = 0, index = 1) out vec4 out1;

layout(push_constant) uniform PushConsts {
   vec4 src1;
} pc;

void main()
{
   out0 = vec4(200.0 / 255.0, 100.0 / 255.0, 50.0 / 255.0, 255.0 / 255.0);
   out1 = pc.src1;
}
