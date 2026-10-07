#version 450
// Full-screen strip (4 vertices) at a constant depth of 0.5 (slope 0).
void main()
{
   vec2 p = vec2((gl_VertexIndex & 1) == 0 ? -1.0 : 1.0,
                 (gl_VertexIndex & 2) == 0 ? -1.0 : 1.0);
   gl_Position = vec4(p, 0.5, 1.0);
}
