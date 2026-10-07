#version 450
// Full-screen strip (4 vertices). Depth runs (x + 0.5) / W across the target.
void main()
{
   vec2 p = vec2((gl_VertexIndex & 1) == 0 ? -1.0 : 1.0,
                 (gl_VertexIndex & 2) == 0 ? -1.0 : 1.0);
   gl_Position = vec4(p, p.x * 0.5 + 0.5, 1.0);
}
