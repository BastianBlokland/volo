#ifndef INCLUDE_VERTEX
#define INCLUDE_VERTEX

struct Vertex {
  vec4 position; // x, y, z position
  vec4 texcoord; // x, y texcoord1
};

#define VERT_INPUT_BINDING()                                                                       \
  layout(set = 0, binding = 0, std140) readonly buffer VertexBuffer { Vertex[] vertices; }

#define VERT_CURRENT() vertices[gl_VertexIndex]

#endif // INCLUDE_VERTEX
