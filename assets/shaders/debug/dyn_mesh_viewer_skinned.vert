#include "binding.glsl"
#include "vertex.glsl"

struct ViewerData {
  f32m4 viewProj;
};

bind_draw_data(0) readonly uniform Draw { ViewerData u_draw; };
bind_draw_data(1) readonly buffer Mesh { VertexPacked[] u_vertices; };

void main() {
  const Vertex vert = vert_unpack(u_vertices[in_vertexIndex]);

  out_vertexPosition = u_draw.viewProj * f32v4(vert.position, 1);
}
