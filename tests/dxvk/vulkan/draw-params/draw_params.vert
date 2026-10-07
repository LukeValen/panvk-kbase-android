#version 460
#extension GL_ARB_shader_draw_parameters : enable

layout(location = 0) in uint a_inst;

/* std430: uint at 0, uvec4 array aligns to 16. */
layout(std430, set = 0, binding = 0) buffer Ssbo {
   uint count;
   uvec4 rec[];
};

layout(push_constant) uniform Pc {
   uint unused_tag;
};

void main()
{
   uint idx = atomicAdd(count, 1u);
   rec[idx] = uvec4(uint(gl_VertexIndex),
                    uint(gl_InstanceIndex),
                    (uint(gl_BaseVertex) & 0xffffu) | (uint(gl_BaseInstance) << 16u),
                    a_inst);
   /* Keep the push-constant live without changing recorded values. */
   if (unused_tag == 0xffffffffu)
      gl_Position = vec4(1.0, 0.0, 0.0, 1.0);
   else
      gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
   gl_PointSize = 1.0;
}
