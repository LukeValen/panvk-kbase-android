#version 450

layout(location = 0) out vec4 out0;
layout(location = 1) out vec4 out1;
layout(location = 2) out vec4 out2;
layout(location = 3) out vec4 out3;

void main()
{
   out0 = vec4(11.0 / 255.0, 22.0 / 255.0, 33.0 / 255.0, 44.0 / 255.0);
   out1 = vec4(30.0 / 255.0, 40.0 / 255.0, 50.0 / 255.0, 60.0 / 255.0);
   out2 = vec4(20.0 / 255.0, 30.0 / 255.0, 40.0 / 255.0, 50.0 / 255.0);
   out3 = vec4(210.0 / 255.0, 80.0 / 255.0, 90.0 / 255.0, 250.0 / 255.0);
}
