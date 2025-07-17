#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_dynarray.h"
#include "core_float.h"
#include "core_format.h"
#include "ecs_entity.h"
#include "ecs_world.h"
#include "geo_matrix.h"

#include "import_mesh_internal.h"
#include "loader_mesh_internal.h"
#include "manager_internal.h"
#include "mesh_utils_internal.h"
#include "repo_internal.h"

/**
 * Wavefront Obj.
 * Polygonal faces are supported (no curves or lines), materials are ignored at this time.
 * Format specification: http://www.martinreddy.net/gfx/3d/OBJ.spec
 * Faces are assumed to be convex and are triangulated using a simple triangle fan.
 *
 * NOTE: This doesn't do any handedness correction (as Obj doesn't specify the handedness), that
 * does mean that obj files exported from software with a right-handed coordinate system appear
 * flipped.
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
  ObjError_NoFaces,
  ObjError_ImportFailed,

  ObjError_Count,
} ObjError;

static String obj_error_str(const ObjError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Out of bounds index"),
      string_static("Unexpected end-of-file"),
      string_static("Face contains too few vertices (minimum is 3)"),
      string_static("Mesh contains too many vertices"),
      string_static("At least one mesh face is required"),
      string_static("Import failed"),
  };
  ASSERT(array_elems(g_msgs) == ObjError_Count, "Incorrect number of obj-error messages");
  return g_msgs[err];
}

INLINE_HINT static void obj_mem_consume_inplace(Mem* mem, const usize amount) {
  mem->ptr = bits_ptr_offset(mem->ptr, amount);
  mem->size -= amount;
}

INLINE_HINT static bool obj_starts_with(const String str, const String start) {
  if (UNLIKELY(start.size > str.size)) {
    return false;
  }
  u8* strItr = mem_begin(str);
  for (u8* startItr = mem_begin(start); startItr != mem_end(start); ++startItr, ++strItr) {
    if (*strItr != *startItr) {
      return false;
    }
  }
  return true;
}

INLINE_HINT static bool obj_starts_with_char(const String str, const u8 ch) {
  if (UNLIKELY(!str.size)) {
    return false;
  }
  return *mem_begin(str) == ch;
}

INLINE_HINT static String
obj_consume_optional(String input, const String toConsume, bool* outConsumed) {
  const bool consume = obj_starts_with(input, toConsume);
  if (outConsumed) {
    *outConsumed = consume;
  }
  if (consume) {
    obj_mem_consume_inplace(&input, toConsume.size);
  }
  return input;
}

INLINE_HINT static String
obj_consume_optional_char(String input, const u8 toConsume, bool* outConsumed) {
  const bool consume = obj_starts_with_char(input, toConsume);
  if (outConsumed) {
    *outConsumed = consume;
  }
  if (consume) {
    obj_mem_consume_inplace(&input, 1);
  }
  return input;
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
  input                                         = obj_read_vec2(input, &texcoord);
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
  if (UNLIKELY(geo_vector_mag_sqr(normal) <= f32_epsilon)) {
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
  num   = num < 0 ? (i64)attributeCount + num : num - 1;
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
  input = obj_consume_optional_char(input, 'v', null);
  input = obj_read_index(input, data->positions.size, &vertex.positionIndex, err);
  if (UNLIKELY(*err)) {
    return input;
  }

  bool consumed;
  input = obj_consume_optional_char(input, '/', &consumed);
  if (!consumed) {
    goto Success; // Vertex only specifies a position, this is perfectly valid.
  }
  if (!obj_starts_with_char(input, '/')) {
    // Texcoord index (optionally prefixed by 'vt').
    input = obj_consume_optional(input, string_lit("vt"), null);
    input = obj_read_index(input, data->texcoords.size, &vertex.texcoordIndex, err);
    if (UNLIKELY(*err)) {
      return input;
    }
  }
  input = obj_consume_optional_char(input, '/', &consumed);
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
  ObjFace face = {.vertexIndex = (u32)data->vertices.size};

  while (LIKELY(!string_is_empty(input))) {
    switch (*string_begin(input)) {
    case ' ':
    case '\t':
    case 0x0B:
    case 0x0C:
      obj_mem_consume_inplace(&input, 1); // Ignore Ascii whitespace characters.
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
      obj_mem_consume_inplace(&input, 1); // Ignore Ascii whitespace characters.
      break;
    case 'v':
      obj_mem_consume_inplace(&input, 1); // Consume 'v'.
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
        obj_mem_consume_inplace(&input, 1); // Consume 't'.
        input = obj_read_texcoord(input, data, err);
        break;
      case 'n':
        obj_mem_consume_inplace(&input, 1); // Consume 'n'.
        input = obj_read_normal(input, data, err);
        break;
      default:
        input = format_read_line(input, null); // Unknown data.
        break;
      }
      break;
    case 'f':
      obj_mem_consume_inplace(&input, 1); // Consume 'f'.
      input = obj_read_face(input, data, err);
      break;
    default:
      input = format_read_line(input, null); // Unknown data.
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

static void
obj_triangulate(const ObjData* data, const AssetImportMesh* importData, AssetMeshBuilder* builder) {

  const GeoMatrix vertexImportTrans = geo_matrix_trs(
      importData->vertexTranslation, importData->vertexRotation, importData->vertexScale);

  dynarray_for_t(&data->faces, ObjFace, face) {
    const GeoVector* positions = dynarray_begin_t(&data->positions, GeoVector);
    const GeoVector* normals   = dynarray_begin_t(&data->normals, GeoVector);
    const ObjVertex* vertices  = dynarray_begin_t(&data->vertices, ObjVertex);

    GeoVector faceNrm;
    if (face->useFaceNormal || importData->flatNormals) {
      faceNrm = asset_mesh_tri_norm(
          positions[vertices[face->vertexIndex].positionIndex],
          positions[vertices[face->vertexIndex + 1].positionIndex],
          positions[vertices[face->vertexIndex + 2].positionIndex]);
    }

    // Create a triangle fan around the first vertex.
    const ObjVertex* inA   = &vertices[face->vertexIndex];
    AssetMeshVertex  vertA = {
         .position = positions[inA->positionIndex],
         .normal   = face->useFaceNormal ? faceNrm : normals[inA->normalIndex],
         .texcoord = obj_get_texcoord(data, inA),
    };
    asset_mesh_vertex_transform(&vertA, &vertexImportTrans);
    asset_mesh_vertex_quantize(&vertA);

    for (u32 i = 2; i < face->vertexCount; ++i) {
      const ObjVertex* inB   = &vertices[face->vertexIndex + i - 1];
      AssetMeshVertex  vertB = {
           .position = positions[inB->positionIndex],
           .normal   = face->useFaceNormal ? faceNrm : normals[inB->normalIndex],
           .texcoord = obj_get_texcoord(data, inB),
      };
      asset_mesh_vertex_transform(&vertB, &vertexImportTrans);
      asset_mesh_vertex_quantize(&vertB);

      const ObjVertex* inC   = &vertices[face->vertexIndex + i];
      AssetMeshVertex  vertC = {
           .position = positions[inC->positionIndex],
           .normal   = face->useFaceNormal ? faceNrm : normals[inC->normalIndex],
           .texcoord = obj_get_texcoord(data, inC),
      };
      asset_mesh_vertex_transform(&vertC, &vertexImportTrans);
      asset_mesh_vertex_quantize(&vertC);

      /**
       * NOTE: Convert from clock-wise winding to counter-clockwise by submitting in opposite order.
       */
      asset_mesh_builder_push(builder, &vertA);
      asset_mesh_builder_push(builder, &vertC);
      asset_mesh_builder_push(builder, &vertB);
    }
  }
}

static bool obj_import(
    const AssetImportEnvComp* importEnv,
    ObjData*                  data,
    const String              assetId,
    AssetImportMesh*          out) {
  (void)data;

  out->flatNormals = false;

  out->vertexTranslation = geo_vector(0);
  out->vertexRotation    = geo_quat_ident;
  out->vertexScale       = geo_vector(1.0f, 1.0f, 1.0f);

  out->rootTranslation = geo_vector(0);
  out->rootRotation    = geo_quat_ident;
  out->rootScale       = geo_vector(1.0f, 1.0f, 1.0f);

  out->jointCount = 0;
  out->animCount  = 0;

  return asset_import_mesh(importEnv, assetId, out);
}

void asset_load_mesh_obj(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;

  ObjError          err     = ObjError_None;
  AssetMeshBuilder* builder = null;

  ObjData data = {
      .positions = dynarray_create_t(g_allocHeap, GeoVector, 64),
      .texcoords = dynarray_create_t(g_allocHeap, GeoVector, 64),
      .normals   = dynarray_create_t(g_allocHeap, GeoVector, 64),
      .vertices  = dynarray_create_t(g_allocHeap, ObjVertex, 64),
      .faces     = dynarray_create_t(g_allocHeap, ObjFace, 32),
  };
  obj_read_data(src->data, &data, &err);
  asset_repo_close(src);
  if (err) {
    asset_mark_load_failure(world, entity, id, obj_error_str(err), (i32)err);
    goto Done;
  }
  if (!data.totalTris) {
    err = ObjError_NoFaces;
    asset_mark_load_failure(world, entity, id, obj_error_str(err), (i32)err);
    goto Done;
  }

  const u32 numVerts = data.totalTris * 3;
  // TODO: This check is very conservative as the index buffer could reuse many vertices.
  if (numVerts > asset_mesh_vertices_max) {
    err = ObjError_TooManyVertices;
    asset_mark_load_failure(world, entity, id, obj_error_str(err), (i32)err);
    goto Done;
  }

  AssetImportMesh importData;
  if (!obj_import(importEnv, &data, id, &importData)) {
    err = ObjError_ImportFailed;
    asset_mark_load_failure(world, entity, id, obj_error_str(err), (i32)err);
    goto Done;
  }

  builder = asset_mesh_builder_create(g_allocHeap, numVerts);
  obj_triangulate(&data, &importData, builder);
  asset_mesh_compute_tangents(builder);

  AssetMeshBundle meshBundle = {0};
  meshBundle.mesh            = asset_mesh_create(builder);

  *ecs_world_add_t(world, entity, AssetMeshComp) = meshBundle.mesh;
  asset_mark_load_success(world, entity);

  asset_cache(world, entity, g_assetMeshBundleMeta, mem_var(meshBundle));

Done:
  if (builder) {
    asset_mesh_builder_destroy(builder);
  }
  dynarray_destroy(&data.positions);
  dynarray_destroy(&data.texcoords);
  dynarray_destroy(&data.normals);
  dynarray_destroy(&data.vertices);
  dynarray_destroy(&data.faces);
}
