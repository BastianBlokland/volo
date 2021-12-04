#include "asset_mesh.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "ecs_world.h"

#include "mesh_utils_internal.h"
#include "repo_internal.h"

/**
 * Wavefront Obj.
 * Polygonal faces are supported (no curves or lines), materials are ignored at this time.
 * Format specification: http://www.martinreddy.net/gfx/3d/OBJ.spec
 * Faces are assumed to be convex and are triangulated using a simple triangle fan.
 */

/**
 * Indices for a single face vertex.
 * These are already bounds checked and converted to absolute indices starting from 0.
 * Normal and texcoord are optional, 'sentinel_u32' means unused.
 */
typedef struct {
  u32 positionIndex;
  u32 normalIndex;
  u32 texcoordIndex;
} ObjVertex;

/**
 * Obj face.
 * Contains three or more vertices, no upper bound on amount of vertices.
 */
typedef struct {
  u32  vertexIndex;
  u32  vertexCount;
  bool useFaceNormal; // Indicates that a face normal should be used instead of per vertex normal.
} ObjFace;

typedef struct {
  DynArray positions; // GeoVector[]
  DynArray texcoords; // GeoVector[]
  DynArray normals;   // GeoVector[]
  DynArray vertices;  // ObjVertex[]
  DynArray faces;     // ObjFace[]
  u32      totalTris;
} ObjData;

typedef enum {
  ObjError_None = 0,
  ObjError_IndexOutOfBounds,
  ObjError_UnexpectedEndOfFile,
  ObjError_FaceTooFewVertices,
  ObjError_TooManyVertices,

  ObjError_Count,
} ObjError;

static String obj_error_str(const ObjError err) {
  static const String msgs[] = {
      string_static("None"),
      string_static("Out of bounds index"),
      string_static("Unexpected end-of-file"),
      string_static("Face contains too few vertices (minimum is 3)"),
      string_static("Mesh contains too many vertices"),
  };
  ASSERT(array_elems(msgs) == ObjError_Count, "Incorrect number of obj-error messages");
  return msgs[err];
}

static String obj_consume_optional(const String input, const String toConsume, bool* outConsumed) {
  const bool consume = string_starts_with(input, toConsume);
  if (outConsumed) {
    *outConsumed = consume;
  }
  return consume ? string_consume(input, toConsume.size) : input;
}

/**
 * Read x and y floats seperated by whitespace.
 */
static String obj_read_vec2(String input, GeoVector* out) {
  f64 x, y;
  input = format_read_f64(input, &x);
  input = format_read_whitespace(input, null);
  input = format_read_f64(input, &y);
  *out  = geo_vector((f32)x, (f32)y);
  return input;
}

/**
 * Read x, y and z floats seperated by whitespace.
 */
static String obj_read_vec3(String input, GeoVector* out) {
  f64 x, y, z;
  input = format_read_f64(input, &x);
  input = format_read_whitespace(input, null);
  input = format_read_f64(input, &y);
  input = format_read_whitespace(input, null);
  input = format_read_f64(input, &z);
  *out  = geo_vector((f32)x, (f32)y, (f32)z);
  return input;
}

/**
 * Vertex position,
 * Example: 'v 1.42 2.42 3.42'.
 */
static String obj_read_position(String input, ObjData* data, ObjError* err) {
  input = format_read_whitespace(input, null);
  input = obj_read_vec3(input, dynarray_push_t(&data->positions, GeoVector));

  *err = ObjError_None;
  return format_read_line(input, null);
}

/**
 * Vertex texture coordinate:
 * Example: 'vt 1.42 2.42'.
 */
static String obj_read_texcoord(String input, ObjData* data, ObjError* err) {
  input = format_read_whitespace(input, null);

  GeoVector texcoord;
  input      = obj_read_vec2(input, &texcoord);
  texcoord.y = 1 - texcoord.y; // Flip the y axis to use the top as the origin.
  *dynarray_push_t(&data->texcoords, GeoVector) = texcoord;

  *err = ObjError_None;
  return format_read_line(input, null);
}

/**
 * Vertex normal.
 * Example: 'vn 1.42 2.42 3.42'.
 */
static String obj_read_normal(String input, ObjData* data, ObjError* err) {
  input = format_read_whitespace(input, null);

  GeoVector normal;
  input = obj_read_vec3(input, &normal);
  if (UNLIKELY(!normal.x && !normal.y && !normal.z)) {
    normal = geo_forward; // Handle obj files that define 'vn 0 0 0'.
  }
  *dynarray_push_t(&data->normals, GeoVector) = geo_vector_norm(normal);

  *err = ObjError_None;
  return format_read_line(input, null);
}

static String obj_read_index(String input, const usize attributeCount, u32* out, ObjError* err) {
  /**
   * Obj indices are 1 based.
   * Negative indices can be used to index relative to the end of the current data.
   */
  i64 num;
  input = format_read_i64(input, &num, 10);
  num   = num < 0 ? (i64)attributeCount - num : num - 1;
  if (UNLIKELY(num < 0 || num >= (i64)attributeCount)) {
    *err = ObjError_IndexOutOfBounds;
  } else {
    *err = ObjError_None;
  }
  *out = (u32)num;
  return input;
}

/**
 * Vertex definition.
 * position-index / texcoord-index / normal-index.
 * Example: '6/4/1'
 */
static String obj_read_vertex(String input, ObjData* data, ObjError* err) {
  ObjVertex vertex = {.texcoordIndex = sentinel_u32, .normalIndex = sentinel_u32};

  // Position index (optionally prefixed by 'v').
  input = obj_consume_optional(input, string_lit("v"), null);
  input = obj_read_index(input, data->positions.size, &vertex.positionIndex, err);
  if (UNLIKELY(*err)) {
    return input;
  }

  bool consumed;
  input = obj_consume_optional(input, string_lit("/"), &consumed);
  if (!consumed) {
    goto Success; // Vertex only specifies a position, this is perfectly valid.
  }
  if (!string_starts_with(input, string_lit("/"))) {
    // Texcoord index (optionally prefixed by 'vt').
    input = obj_consume_optional(input, string_lit("vt"), null);
    input = obj_read_index(input, data->texcoords.size, &vertex.texcoordIndex, err);
    if (UNLIKELY(*err)) {
      return input;
    }
  }
  input = obj_consume_optional(input, string_lit("/"), &consumed);
  if (consumed) {
    // Normal index (optionally prefixed by 'vn').
    input = obj_consume_optional(input, string_lit("vn"), null);
    input = obj_read_index(input, data->normals.size, &vertex.normalIndex, err);
    if (UNLIKELY(*err)) {
      return input;
    }
  }
Success:
  *dynarray_push_t(&data->vertices, ObjVertex) = vertex;
  *err                                         = ObjError_None;
  return input;
}

/**
 * Mesh Face.
 * Example: 'f 6/4/1 3/5/3 7/6/5'
 */
static String obj_read_face(String input, ObjData* data, ObjError* err) {
  ObjFace face = {.vertexIndex = data->vertices.size};

  while (LIKELY(!string_is_empty(input))) {
    switch (*string_begin(input)) {
    case ' ':
    case '\t':
    case 0x0B:
    case 0x0C:
      input = string_consume(input, 1); // Ignore Ascii whitespace characters.
      break;
    case '\r':
    case '\n':
      goto End;
    default: {
      input = obj_read_vertex(input, data, err);
      if (UNLIKELY(*err)) {
        return input;
      }
      ObjVertex* vertex = dynarray_at_t(&data->vertices, data->vertices.size - 1, ObjVertex);
      face.useFaceNormal |= sentinel_check(vertex->normalIndex);
      ++face.vertexCount;
    } break;
    }
  }
End:
  if (face.vertexCount < 3) {
    *err = ObjError_FaceTooFewVertices;
    return input;
  }
  input                                   = format_read_line(input, null);
  *dynarray_push_t(&data->faces, ObjFace) = face;
  data->totalTris += face.vertexCount - 2;
  *err = ObjError_None;
  return input;
}

static String obj_read_data(String input, ObjData* data, ObjError* err) {
  while (LIKELY(input.size && !*err)) {
    switch (*string_begin(input)) {
    case ' ':
    case '\t':
    case '\n':
    case 0x0B:
    case 0x0C:
    case '\r':
      input = string_consume(input, 1); // Ignore Ascii whitespace characters.
      break;
    case 'v':
      input = string_consume(input, 1); // Consume 'v'.
      if (UNLIKELY(!input.size)) {
        *err = ObjError_UnexpectedEndOfFile;
        goto End;
      }
      switch (*string_begin(input)) {
      case ' ':
      case '\t':
        input = obj_read_position(input, data, err);
        break;
      case 't':
        input = string_consume(input, 1); // Consume 't'.
        input = obj_read_texcoord(input, data, err);
        break;
      case 'n':
        input = string_consume(input, 1); // Consume 'n'.
        input = obj_read_normal(input, data, err);
        break;
      default:
        input = format_read_line(input, null); // Unknown data (includes comments etc).
        break;
      }
      break;
    case 'f':
      input = string_consume(input, 1); // Consume 'f'.
      input = obj_read_face(input, data, err);
      break;
    }
  }
End:
  return input;
}

static GeoVector obj_get_texcoord(const ObjData* data, const ObjVertex* vertex) {
  if (sentinel_check(vertex->texcoordIndex)) {
    return geo_vector(0);
  }
  return *dynarray_at_t(&data->texcoords, vertex->texcoordIndex, GeoVector);
}

static GeoVector obj_tri_norm(const GeoVector a, const GeoVector b, const GeoVector c) {
  const GeoVector surface = geo_vector_cross3(geo_vector_sub(b, a), geo_vector_sub(c, a));
  if (UNLIKELY(!surface.x && !surface.y && !surface.z)) {
    // Triangle with zero area has technically no normal, but does ocur in the wild.
    return geo_forward;
  }
  return geo_vector_norm(surface);
}

static void obj_triangulate(const ObjData* data, AssetMeshBuilder* builder) {
  dynarray_for_t((DynArray*)&data->faces, ObjFace, face, {
    const GeoVector* positions = data->positions.data.ptr;
    const GeoVector* normals   = data->positions.data.ptr;
    const ObjVertex* vertices  = data->vertices.data.ptr;

    GeoVector faceNrm;
    if (face->useFaceNormal) {
      faceNrm = obj_tri_norm(
          positions[vertices[face->vertexIndex].positionIndex],
          positions[vertices[face->vertexIndex + 1].positionIndex],
          positions[vertices[face->vertexIndex + 2].positionIndex]);
    }

    // Create a triangle fan around the first vertex.
    const ObjVertex*      inA   = &vertices[face->vertexIndex];
    const AssetMeshVertex vertA = {
        .position = positions[inA->positionIndex],
        .normal   = face->useFaceNormal ? faceNrm : normals[inA->normalIndex],
        .texcoord = obj_get_texcoord(data, inA),
    };

    for (usize i = 2; i < face->vertexCount; ++i) {
      const ObjVertex*      inB   = &vertices[face->vertexIndex + i - 1];
      const AssetMeshVertex vertB = {
          .position = positions[inB->positionIndex],
          .normal   = face->useFaceNormal ? faceNrm : normals[inB->normalIndex],
          .texcoord = obj_get_texcoord(data, inB),
      };

      const ObjVertex*      inC   = &vertices[face->vertexIndex + i];
      const AssetMeshVertex vertC = {
          .position = positions[inC->positionIndex],
          .normal   = face->useFaceNormal ? faceNrm : normals[inC->normalIndex],
          .texcoord = obj_get_texcoord(data, inC),
      };

      asset_mesh_builder_push(builder, vertA);
      asset_mesh_builder_push(builder, vertB);
      asset_mesh_builder_push(builder, vertC);
    }
  });
}

NORETURN static void obj_report_error(const ObjError err) {
  diag_crash_msg("Failed to parse WaveFront Obj Mesh, error: {}", fmt_text(obj_error_str(err)));
}

void asset_load_obj(EcsWorld* world, EcsEntityId assetEntity, AssetSource* src) {
  ObjError err  = ObjError_None;
  ObjData  data = {
      .positions = dynarray_create_t(g_alloc_heap, GeoVector, 64),
      .texcoords = dynarray_create_t(g_alloc_heap, GeoVector, 64),
      .normals   = dynarray_create_t(g_alloc_heap, GeoVector, 64),
      .vertices  = dynarray_create_t(g_alloc_heap, ObjVertex, 64),
      .faces     = dynarray_create_t(g_alloc_heap, ObjFace, 32),
  };
  obj_read_data(src->data, &data, &err);
  asset_source_close(src);
  if (err) {
    obj_report_error(err);
  }

  const usize numVerts = data.totalTris * 3;
  if (numVerts > u16_max) {
    obj_report_error(ObjError_TooManyVertices);
  }
  AssetMeshBuilder* builder = asset_mesh_builder_create(g_alloc_heap, numVerts);
  obj_triangulate(&data, builder);

  *ecs_world_add_t(world, assetEntity, AssetMeshComp) = asset_mesh_create(builder);
  ecs_world_add_empty_t(world, assetEntity, AssetLoadedComp);

  // Cleanup temporary resources.
  asset_mesh_builder_destroy(builder);
  dynarray_destroy(&data.positions);
  dynarray_destroy(&data.texcoords);
  dynarray_destroy(&data.normals);
  dynarray_destroy(&data.vertices);
  dynarray_destroy(&data.faces);
}
