#include "asset_mesh.h"
#include "asset_raw.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_path.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "json_read.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "mesh_utils_internal.h"
#include "repo_internal.h"

/**
 * GLTF (GL Transmission Format) 2.0.
 * Format specification: https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html
 *
 * NOTE: Only meshes[0] is imported.
 *
 * NOTE: Gltf buffer-data uses little-endian byte-order and 2's complement integers, and this loader
 * assumes the host system matches that.
 */

typedef enum {
  GltfLoadPhase_Meta,
  GltfLoadPhase_BuffersAcquire,
  GltfLoadPhase_BuffersWait,
  GltfLoadPhase_BufferViews,
  GltfLoadPhase_Accessors,
  GltfLoadPhase_Primitives,
  GltfLoadPhase_Build,
} GltfLoadPhase;

typedef struct {
  u64 versionMajor, versionMinor;
} GltfMeta;

typedef struct {
  u32         length;
  EcsEntityId entity;
  String      data; // NOTE: Available after the BuffersWait phase.
} GltfBuffer;

typedef struct {
  String data;
} GltfBufferView;

typedef enum {
  GltfType_i8  = 5120,
  GltfType_u8  = 5121,
  GltfType_i16 = 5122,
  GltfType_u16 = 5123,
  GltfType_u32 = 5125,
  GltfType_f32 = 5126,
} GltfType;

typedef struct {
  GltfType compType;
  u32      compCount;
  union {
    void* data_raw;
    i8*   data_i8;
    u8*   data_u8;
    i16*  data_i16;
    u16*  data_u16;
    u32*  data_u32;
    f32*  data_f32;
  };
  u32 count;
} GltfAccessor;

typedef enum {
  GltfPrimMode_Points,
  GltfPrimMode_Lines,
  GltfPrimMode_LineLoop,
  GltfPrimMode_LineStrip,
  GltfPrimMode_Triangles,
  GltfPrimMode_TriangleStrip,
  GltfPrimMode_TriangleFan,

  GltfPrimMode_Max,
} GltfPrimMode;

typedef struct {
  GltfPrimMode mode;
  u32          accIndices; // Optional.
  u32          accPosition;
  u32          accTexcoord; // Optional.
  u32          accNormal;   // Optional.
  u32          accTangent;  // Optional.
  u32          accJoints;   // Optional.
  u32          accWeights;  // Optional.
} GltfPrim;

ecs_comp_define(AssetGltfLoadComp) {
  String        assetId;
  GltfLoadPhase phase;
  JsonDoc*      jDoc;
  JsonVal       jRoot;
  GltfMeta      meta;
  DynArray      buffers;     // GltfBuffer[].
  DynArray      bufferViews; // GltfBufferView[].
  DynArray      accessors;   // GltfAccessor[].
  DynArray      primitives;  // GltfPrim[].
};

static void ecs_destruct_gltf_load_comp(void* data) {
  AssetGltfLoadComp* comp = data;
  json_destroy(comp->jDoc);
  dynarray_destroy(&comp->buffers);
  dynarray_destroy(&comp->bufferViews);
  dynarray_destroy(&comp->accessors);
  dynarray_destroy(&comp->primitives);
}

u32 gltf_component_size(const GltfType type) {
  switch (type) {
  case GltfType_i8:
  case GltfType_u8:
    return 1;
  case GltfType_i16:
  case GltfType_u16:
    return 2;
  case GltfType_u32:
  case GltfType_f32:
    return 4;
  default:
    UNREACHABLE;
  }
}

typedef enum {
  GltfError_None = 0,
  GltfError_InvalidJson,
  GltfError_MalformedFile,
  GltfError_MalformedAsset,
  GltfError_MalformedVersion,
  GltfError_MalformedRequiredExtensions,
  GltfError_MalformedBuffers,
  GltfError_MalformedBufferViews,
  GltfError_MalformedAccessors,
  GltfError_MalformedAccessorType,
  GltfError_MalformedPrims,
  GltfError_MalformedPrimIndices,
  GltfError_MalformedPrimPositions,
  GltfError_MalformedPrimNormals,
  GltfError_MalformedPrimTangents,
  GltfError_MalformedPrimTexcoords,
  GltfError_MalformedPrimJoints,
  GltfError_MalformedPrimWeights,
  GltfError_JointCountExceedsMaximum,
  GltfError_MissingVersion,
  GltfError_InvalidBuffer,
  GltfError_UnsupportedExtensions,
  GltfError_UnsupportedVersion,
  GltfError_UnsupportedPrimitiveMode,
  GltfError_NoPrimitives,

  GltfError_Count,
} GltfError;

static String gltf_error_str(const GltfError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Invalid json"),
      string_static("Malformed gltf file"),
      string_static("Gltf 'asset' field malformed"),
      string_static("Gltf malformed version string"),
      string_static("Gltf 'extensionsRequired' field malformed"),
      string_static("Gltf 'buffers' field malformed"),
      string_static("Gltf 'bufferViews' field malformed"),
      string_static("Gltf 'accessors' field malformed"),
      string_static("Invalid accessor type"),
      string_static("Gltf 'primitives' field malformed"),
      string_static("Malformed primitive indices"),
      string_static("Malformed primitive positions"),
      string_static("Malformed primitive normals"),
      string_static("Malformed primitive tangents"),
      string_static("Malformed primitive texcoords"),
      string_static("Malformed primitive joints"),
      string_static("Malformed primitive weights"),
      string_static("Joint count exceeds maximum"),
      string_static("Gltf version specification missing"),
      string_static("Gltf invalid buffer"),
      string_static("Gltf file requires an unsupported extension"),
      string_static("Unsupported gltf version"),
      string_static("Unsupported primitive mode, only triangle primitives supported"),
      string_static("Gltf mesh does not have any primitives"),
  };
  ASSERT(array_elems(g_msgs) == GltfError_Count, "Incorrect number of gltf-error messages");
  return g_msgs[err];
}

static void gltf_load_fail_msg(
    EcsWorld* world, const EcsEntityId entity, const GltfError err, const String msg) {
  log_e(
      "Failed to parse gltf mesh",
      log_param("code", fmt_int(err)),
      log_param("error", fmt_text(msg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
}

static void gltf_load_fail(EcsWorld* world, const EcsEntityId entity, const GltfError err) {
  gltf_load_fail_msg(world, entity, err, gltf_error_str(err));
}

static bool gltf_check_val(AssetGltfLoadComp* ld, const JsonVal jVal, const JsonType type) {
  return !sentinel_check(jVal) && json_type(ld->jDoc, jVal) == type;
}

static bool gltf_field_u32(AssetGltfLoadComp* ld, const JsonVal jVal, const String name, u32* out) {
  if (json_type(ld->jDoc, jVal) != JsonType_Object) {
    return false;
  }
  const JsonVal jField = json_field(ld->jDoc, jVal, name);
  if (!gltf_check_val(ld, jField, JsonType_Number)) {
    return false;
  }
  *out = (u32)json_number(ld->jDoc, jField);
  return true;
}

static bool
gltf_field_str(AssetGltfLoadComp* ld, const JsonVal jVal, const String name, String* out) {
  if (json_type(ld->jDoc, jVal) != JsonType_Object) {
    return false;
  }
  const JsonVal jField = json_field(ld->jDoc, jVal, name);
  if (!gltf_check_val(ld, jField, JsonType_String)) {
    return false;
  }
  *out = json_string(ld->jDoc, jField);
  return true;
}

static void gltf_parse_version(AssetGltfLoadComp* ld, String str, GltfError* err) {
  str = format_read_u64(str, &ld->meta.versionMajor, 10);
  if (string_is_empty(str)) {
    ld->meta.versionMinor = 0;
    *err                  = GltfError_None;
    return;
  }
  if (*string_begin(str) != '.') {
    *err = GltfError_MalformedVersion;
    return;
  }
  format_read_u64(string_consume(str, 1), &ld->meta.versionMinor, 10);
  *err = GltfError_None;
}

static void gltf_parse_meta(AssetGltfLoadComp* ld, GltfError* err) {
  const JsonVal asset = json_field(ld->jDoc, ld->jRoot, string_lit("asset"));
  if (!gltf_check_val(ld, asset, JsonType_Object)) {
    *err = GltfError_MalformedAsset;
    return;
  }
  String versionStr;
  if (!gltf_field_str(ld, asset, string_lit("version"), &versionStr)) {
    *err = GltfError_MissingVersion;
    return;
  }
  gltf_parse_version(ld, versionStr, err);
  if (*err) {
    return;
  }
  if (ld->meta.versionMajor != 2 && ld->meta.versionMinor != 0) {
    *err = GltfError_UnsupportedVersion;
    return;
  }
  const JsonVal extensions = json_field(ld->jDoc, ld->jRoot, string_lit("extensionsRequired"));
  if (!sentinel_check(extensions)) {
    if (!gltf_check_val(ld, extensions, JsonType_Array)) {
      *err = GltfError_MalformedRequiredExtensions;
      return;
    }
    // NOTE: No extensions are suppored at this time.
    if (json_elem_count(ld->jDoc, extensions) != 0) {
      *err = GltfError_UnsupportedExtensions;
      return;
    }
  }
  *err = GltfError_None;
}

static String gltf_buffer_asset_id(AssetGltfLoadComp* ld, const String uri) {
  const String root = path_parent(ld->assetId);
  if (string_is_empty(root)) {
    return uri;
  }
  return fmt_write_scratch("{}/{}", fmt_text(root), fmt_text(uri));
}

static void gltf_buffers_acquire(
    AssetGltfLoadComp* ld, EcsWorld* world, AssetManagerComp* manager, GltfError* err) {
  const JsonVal buffers = json_field(ld->jDoc, ld->jRoot, string_lit("buffers"));
  if (!gltf_check_val(ld, buffers, JsonType_Array)) {
    goto Error;
  }
  json_for_elems(ld->jDoc, buffers, bufferElem) {
    u32 byteLength;
    if (!gltf_field_u32(ld, bufferElem, string_lit("byteLength"), &byteLength)) {
      goto Error;
    }
    String uri;
    if (!gltf_field_str(ld, bufferElem, string_lit("uri"), &uri)) {
      goto Error;
    }
    const String      id     = gltf_buffer_asset_id(ld, uri);
    const EcsEntityId entity = asset_lookup(world, manager, id);
    asset_acquire(world, entity);

    *dynarray_push_t(&ld->buffers, GltfBuffer) = (GltfBuffer){
        .length = byteLength,
        .entity = entity,
    };
  }
  *err = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedBuffers;
}

static void gltf_parse_bufferviews(AssetGltfLoadComp* ld, GltfError* err) {
  const JsonVal bufferViews = json_field(ld->jDoc, ld->jRoot, string_lit("bufferViews"));
  if (!gltf_check_val(ld, bufferViews, JsonType_Array)) {
    goto Error;
  }
  json_for_elems(ld->jDoc, bufferViews, bufferView) {
    u32 bufferIndex;
    if (!gltf_field_u32(ld, bufferView, string_lit("buffer"), &bufferIndex)) {
      goto Error;
    }
    if (bufferIndex >= ld->buffers.size) {
      goto Error;
    }
    u32 byteOffset;
    if (!gltf_field_u32(ld, bufferView, string_lit("byteOffset"), &byteOffset)) {
      byteOffset = 0;
    }
    u32 byteLength;
    if (!gltf_field_u32(ld, bufferView, string_lit("byteLength"), &byteLength)) {
      goto Error;
    }
    const String bufferData = dynarray_at_t(&ld->buffers, bufferIndex, GltfBuffer)->data;
    if (byteOffset + byteLength > bufferData.size) {
      goto Error;
    }
    *dynarray_push_t(&ld->bufferViews, GltfBufferView) = (GltfBufferView){
        .data = string_slice(bufferData, byteOffset, byteLength),
    };
  }
  *err = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedBufferViews;
}

static void gltf_parse_accessor_type(const String typeString, u32* outCompCount, GltfError* err) {
  if (string_eq(typeString, string_lit("SCALAR"))) {
    *outCompCount = 1;
    goto Success;
  }
  if (string_eq(typeString, string_lit("VEC2"))) {
    *outCompCount = 2;
    goto Success;
  }
  if (string_eq(typeString, string_lit("VEC3"))) {
    *outCompCount = 3;
    goto Success;
  }
  if (string_eq(typeString, string_lit("VEC4"))) {
    *outCompCount = 4;
    goto Success;
  }
  if (string_eq(typeString, string_lit("MAT2"))) {
    *outCompCount = 8;
    goto Success;
  }
  if (string_eq(typeString, string_lit("MAT3"))) {
    *outCompCount = 12;
    goto Success;
  }
  if (string_eq(typeString, string_lit("MAT4"))) {
    *outCompCount = 16;
    goto Success;
  }
  *err = GltfError_MalformedAccessorType;
  return;

Success:
  *err = GltfError_None;
}

static bool gtlf_check_access_type(const GltfType type) {
  switch (type) {
  case GltfType_i8:
  case GltfType_u8:
  case GltfType_i16:
  case GltfType_u16:
  case GltfType_u32:
  case GltfType_f32:
    return true;
  }
  return false;
}

static void gltf_parse_accessors(AssetGltfLoadComp* ld, GltfError* err) {
  const JsonVal accessors = json_field(ld->jDoc, ld->jRoot, string_lit("accessors"));
  if (!gltf_check_val(ld, accessors, JsonType_Array)) {
    goto Error;
  }
  json_for_elems(ld->jDoc, accessors, accessor) {
    GltfAccessor* result = dynarray_push_t(&ld->accessors, GltfAccessor);
    u32           viewIndex;
    if (!gltf_field_u32(ld, accessor, string_lit("bufferView"), &viewIndex)) {
      goto Error;
    }
    if (viewIndex >= ld->bufferViews.size) {
      goto Error;
    }
    u32 byteOffset;
    if (!gltf_field_u32(ld, accessor, string_lit("byteOffset"), &byteOffset)) {
      byteOffset = 0;
    }
    if (!gltf_field_u32(ld, accessor, string_lit("componentType"), (u32*)&result->compType)) {
      goto Error;
    }
    if (!gtlf_check_access_type(result->compType)) {
      goto Error;
    }
    if (!gltf_field_u32(ld, accessor, string_lit("count"), &result->count)) {
      goto Error;
    }
    String typeString;
    if (!gltf_field_str(ld, accessor, string_lit("type"), &typeString)) {
      goto Error;
    }
    gltf_parse_accessor_type(typeString, &result->compCount, err);
    if (*err) {
      goto Error;
    }
    const u32    compSize = gltf_component_size(result->compType);
    const String viewData = dynarray_at_t(&ld->bufferViews, viewIndex, GltfBufferView)->data;
    if (byteOffset + compSize * result->compCount * result->count > viewData.size) {
      goto Error;
    }
    result->data_raw = mem_at_u8(viewData, byteOffset);
  }
  *err = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedAccessors;
}

static void gltf_parse_primitives(AssetGltfLoadComp* ld, GltfError* err) {
  /**
   * NOTE: This loader only supports a single mesh.
   */
  const JsonVal meshes = json_field(ld->jDoc, ld->jRoot, string_lit("meshes"));
  if (!gltf_check_val(ld, meshes, JsonType_Array) || !json_elem_count(ld->jDoc, meshes)) {
    goto Error;
  }
  const JsonVal mesh = json_elem(ld->jDoc, meshes, 0);
  if (json_type(ld->jDoc, mesh) != JsonType_Object) {
    goto Error;
  }
  const JsonVal primitives = json_field(ld->jDoc, mesh, string_lit("primitives"));
  if (!gltf_check_val(ld, primitives, JsonType_Array)) {
    goto Error;
  }
  json_for_elems(ld->jDoc, primitives, primitive) {
    if (json_type(ld->jDoc, primitive) != JsonType_Object) {
      goto Error;
    }
    GltfPrim* result = dynarray_push_t(&ld->primitives, GltfPrim);
    if (!gltf_field_u32(ld, primitive, string_lit("mode"), (u32*)&result->mode)) {
      result->mode = GltfPrimMode_Triangles;
    }
    if (result->mode > GltfPrimMode_Max) {
      goto Error;
    }
    if (!gltf_field_u32(ld, primitive, string_lit("indices"), &result->accIndices)) {
      result->accIndices = sentinel_u32; // Indices are optional.
    }
    const JsonVal attributes = json_field(ld->jDoc, primitive, string_lit("attributes"));
    if (!gltf_check_val(ld, attributes, JsonType_Object)) {
      goto Error;
    }
    if (!gltf_field_u32(ld, attributes, string_lit("POSITION"), &result->accPosition)) {
      goto Error;
    }
    if (!gltf_field_u32(ld, attributes, string_lit("TEXCOORD_0"), &result->accTexcoord)) {
      result->accTexcoord = sentinel_u32; // Texcoords are optional.
    }
    if (!gltf_field_u32(ld, attributes, string_lit("NORMAL"), &result->accNormal)) {
      result->accNormal = sentinel_u32; // Normals are optional.
    }
    if (!gltf_field_u32(ld, attributes, string_lit("TANGENT"), &result->accTangent)) {
      result->accTangent = sentinel_u32; // Tangents are optional.
    }
    if (!gltf_field_u32(ld, attributes, string_lit("JOINTS_0"), &result->accJoints)) {
      result->accJoints = sentinel_u32; // Joints are optional.
    }
    if (!gltf_field_u32(ld, attributes, string_lit("WEIGHTS_0"), &result->accWeights)) {
      result->accWeights = sentinel_u32; // Weights are optional.
    }
  }
  *err = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedPrims;
}

static bool gltf_check_access(
    AssetGltfLoadComp* ld, const u32 index, const GltfType type, const u32 compCount) {
  GltfAccessor* accessors = dynarray_begin_t(&ld->accessors, GltfAccessor);
  if (index >= ld->accessors.size) {
    return false;
  }
  return accessors[index].compType == type && accessors[index].compCount == compCount;
}

typedef enum {
  GltfFeature_Texcoords = 1 << 1,
  GltfFeature_Normals   = 1 << 2,
  GltfFeature_Tangents  = 1 << 3,
  GltfFeature_Skinning  = 1 << 4,
} GltfFeature;

typedef struct {
  GltfFeature features;
  u32         vertexCount;
} GltfMeshMeta;

static GltfMeshMeta gltf_mesh_meta(AssetGltfLoadComp* ld, GltfError* err) {
#define verify(_EXPR_, _ERR_)                                                                      \
  if (UNLIKELY(!(_EXPR_))) {                                                                       \
    *err = GltfError_##_ERR_;                                                                      \
    goto Error;                                                                                    \
  }

  verify(ld->primitives.size, NoPrimitives);

  GltfAccessor* accessors   = dynarray_begin_t(&ld->accessors, GltfAccessor);
  u32           vertexCount = 0;
  GltfFeature   features    = ~0; // Assume we have all features until accessors are missing.
  dynarray_for_t(&ld->primitives, GltfPrim, prim) {

    verify(prim->mode == GltfPrimMode_Triangles, UnsupportedPrimitiveMode);
    verify(gltf_check_access(ld, prim->accPosition, GltfType_f32, 3), MalformedPrimPositions);

    const u32 attrCount = accessors[prim->accPosition].count;
    if (sentinel_check(prim->accIndices)) {
      // Non-indexed primitive.
      verify((attrCount % 3) == 0, MalformedPrimPositions);
      vertexCount += attrCount;
    } else {
      // Indexed primitive.
      verify(gltf_check_access(ld, prim->accIndices, GltfType_u16, 1), MalformedPrimIndices);
      verify((accessors[prim->accIndices].count % 3) == 0, MalformedPrimIndices);
      vertexCount += accessors[prim->accIndices].count;
    }
    if (sentinel_check(prim->accTexcoord)) {
      features &= ~GltfFeature_Texcoords;
    } else {
      verify(gltf_check_access(ld, prim->accTexcoord, GltfType_f32, 2), MalformedPrimTexcoords);
      verify(accessors[prim->accTexcoord].count == attrCount, MalformedPrimTexcoords);
    }
    if (sentinel_check(prim->accNormal)) {
      features &= ~GltfFeature_Normals;
    } else {
      verify(gltf_check_access(ld, prim->accNormal, GltfType_f32, 3), MalformedPrimNormals);
      verify(accessors[prim->accNormal].count == attrCount, MalformedPrimNormals);
    }
    if (sentinel_check(prim->accTangent)) {
      features &= ~GltfFeature_Tangents;
    } else {
      verify(gltf_check_access(ld, prim->accTangent, GltfType_f32, 4), MalformedPrimTangents);
      verify(accessors[prim->accTangent].count == attrCount, MalformedPrimTangents);
    }
    if (sentinel_check(prim->accJoints)) {
      features &= ~GltfFeature_Skinning;
    } else {
      verify(gltf_check_access(ld, prim->accJoints, GltfType_u16, 4), MalformedPrimJoints);
      verify(accessors[prim->accJoints].count == attrCount, MalformedPrimJoints);
      verify(gltf_check_access(ld, prim->accWeights, GltfType_f32, 4), MalformedPrimWeights);
      verify(accessors[prim->accJoints].count == attrCount, MalformedPrimWeights);
    }
  }
  return (GltfMeshMeta){.features = features, .vertexCount = vertexCount};

Error:
  return (GltfMeshMeta){0};

#undef verify
}

static void gltf_build_mesh(AssetGltfLoadComp* ld, AssetMeshComp* outMesh, GltfError* err) {
  GltfMeshMeta meta = gltf_mesh_meta(ld, err);
  if (*err) {
    return;
  }
  AssetMeshBuilder* builder   = asset_mesh_builder_create(g_alloc_heap, meta.vertexCount);
  GltfAccessor*     accessors = dynarray_begin_t(&ld->accessors, GltfAccessor);

  typedef const u16* AccessorU16;
  typedef const f32* AccessorF32;
  AccessorF32        positions, texcoords, normals, tangents, weights;
  AccessorU16        indices, joints;
  u32                attrCount, vertexCount;

  dynarray_for_t(&ld->primitives, GltfPrim, primitive) {
    positions = accessors[primitive->accPosition].data_f32;
    attrCount = accessors[primitive->accPosition].count;
    if (meta.features & GltfFeature_Texcoords) {
      texcoords = accessors[primitive->accTexcoord].data_f32;
    }
    if (meta.features & GltfFeature_Normals) {
      normals = accessors[primitive->accNormal].data_f32;
    }
    if (meta.features & GltfFeature_Tangents) {
      tangents = accessors[primitive->accTangent].data_f32;
    }
    if (meta.features & GltfFeature_Skinning) {
      joints  = accessors[primitive->accJoints].data_u16;
      weights = accessors[primitive->accWeights].data_f32;
    }
    const bool indexed = !sentinel_check(primitive->accIndices);
    if (indexed) {
      indices     = accessors[primitive->accIndices].data_u16;
      vertexCount = accessors[primitive->accIndices].count;
    } else {
      vertexCount = attrCount;
    }
    for (u32 i = 0; i != vertexCount; ++i) {
      const u32 attr = indexed ? indices[i] : i;
      if (UNLIKELY(attr >= attrCount)) {
        *err = GltfError_MalformedPrimIndices;
        goto Cleanup;
      }
      static const f32 g_zeroTex[4] = {[1] = 1.0f}; // NOTE: y of 1 because we flip the y.
      static const f32 g_zeroNrm[4] = {0.0f};
      static const f32 g_zeroTan[4] = {0.0f};

      const f32* vertPos = &positions[attr * 3];
      const f32* vertTex = meta.features & GltfFeature_Texcoords ? &texcoords[attr * 2] : g_zeroTex;
      const f32* vertNrm = meta.features & GltfFeature_Normals ? &normals[attr * 3] : g_zeroNrm;
      const f32* vertTan = meta.features & GltfFeature_Tangents ? &tangents[attr * 4] : g_zeroTan;

      /**
       * NOTE: Flip the z-axis to convert from right-handed to left-handed coordinate system.
       * NOTE: Flip the texture coordinate y axis as Gltf uses upper-left as the origin.
       */
      const AssetMeshIndex vertIdx = asset_mesh_builder_push(
          builder,
          (AssetMeshVertex){
              .position = geo_vector(vertPos[0], vertPos[1], vertPos[2] * -1.0f),
              .normal   = geo_vector(vertNrm[0], vertNrm[1], vertNrm[2] * -1.0f),
              .tangent  = geo_vector(vertTan[0], vertTan[1], vertTan[2] * -1.0f, vertTan[3]),
              .texcoord = geo_vector(vertTex[0], 1.0f - vertTex[1]),
          });

      if (meta.features & GltfFeature_Skinning) {
        const u16* vertJoints = &joints[attr * 4];
        const f32* vertWeight = &weights[attr * 4];
        const u16  j0 = vertJoints[0], j1 = vertJoints[1], j2 = vertJoints[2], j3 = vertJoints[3];
        enum { JointMax = asset_mesh_joints_max };
        if (UNLIKELY(j0 >= JointMax || j1 >= JointMax || j2 >= JointMax || j3 >= JointMax)) {
          *err = GltfError_JointCountExceedsMaximum;
          goto Cleanup;
        }
        asset_mesh_builder_set_skin(
            builder,
            vertIdx,
            (AssetMeshSkin){
                .joints  = {(u8)j0, (u8)j1, (u8)j2, (u8)j3},
                .weights = {vertWeight[0], vertWeight[1], vertWeight[2], vertWeight[3]},
            });
      }
    }
  }
  if (!(meta.features & GltfFeature_Normals)) {
    asset_mesh_compute_flat_normals(builder);
  }
  if (!(meta.features & GltfFeature_Tangents)) {
    asset_mesh_compute_tangents(builder);
  }
  *outMesh = asset_mesh_create(builder);
  *err     = GltfError_None;

Cleanup:
  asset_mesh_builder_destroy(builder);
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(LoadView) { ecs_access_write(AssetGltfLoadComp); }
ecs_view_define(BufferView) { ecs_access_read(AssetRawComp); }

/**
 * Update all active loads.
 */
ecs_system_define(GltfLoadAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }

  EcsView*     loadView  = ecs_world_view_t(world, LoadView);
  EcsIterator* bufferItr = ecs_view_itr(ecs_world_view_t(world, BufferView));

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId  entity = ecs_view_entity(itr);
    AssetGltfLoadComp* ld     = ecs_view_write_t(itr, AssetGltfLoadComp);

    GltfError err = GltfError_None;
    switch (ld->phase) {
    case GltfLoadPhase_Meta:
      gltf_parse_meta(ld, &err);
      if (err) {
        goto Error;
      }
      ++ld->phase;
      // Fallthrough.
    case GltfLoadPhase_BuffersAcquire:
      gltf_buffers_acquire(ld, world, manager, &err);
      if (err) {
        goto Error;
      }
      ++ld->phase;
      goto Next;
    case GltfLoadPhase_BuffersWait:
      dynarray_for_t(&ld->buffers, GltfBuffer, buffer) {
        if (ecs_world_has_t(world, buffer->entity, AssetFailedComp)) {
          err = GltfError_InvalidBuffer;
          goto Error;
        }
        if (!ecs_world_has_t(world, buffer->entity, AssetLoadedComp)) {
          goto Next; // Wait for the buffer to be loaded.
        }
        if (!ecs_view_maybe_jump(bufferItr, buffer->entity)) {
          err = GltfError_InvalidBuffer;
          goto Error;
        }
        const String data = ecs_view_read_t(bufferItr, AssetRawComp)->data;
        if (data.size < buffer->length) {
          err = GltfError_InvalidBuffer;
          goto Error;
        }
        buffer->data = string_slice(data, 0, buffer->length);
      }
      ++ld->phase;
      // Fallthrough.
    case GltfLoadPhase_BufferViews:
      gltf_parse_bufferviews(ld, &err);
      if (err) {
        goto Error;
      }
      ++ld->phase;
      // Fallthrough.
    case GltfLoadPhase_Accessors:
      gltf_parse_accessors(ld, &err);
      if (err) {
        goto Error;
      }
      ++ld->phase;
      // Fallthrough.
    case GltfLoadPhase_Primitives:
      gltf_parse_primitives(ld, &err);
      if (err) {
        goto Error;
      }
      ++ld->phase;
      // Fallthrough.
    case GltfLoadPhase_Build: {
      AssetMeshComp result;
      gltf_build_mesh(ld, &result, &err);
      if (err) {
        goto Error;
      }
      *ecs_world_add_t(world, entity, AssetMeshComp) = result;
      ecs_world_add_empty_t(world, entity, AssetLoadedComp);
      goto Cleanup;
    }
    }

  Error:
    gltf_load_fail(world, entity, err);

  Cleanup:
    dynarray_for_t(&ld->buffers, GltfBuffer, buffer) { asset_release(world, buffer->entity); }
    ecs_world_remove_t(world, entity, AssetGltfLoadComp);

  Next:
    continue;
  }
}

ecs_module_init(asset_gltf_module) {
  ecs_register_comp(AssetGltfLoadComp, .destructor = ecs_destruct_gltf_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(BufferView);

  ecs_register_system(
      GltfLoadAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView), ecs_view_id(BufferView));
}

void asset_load_gltf(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  JsonDoc*   jsonDoc = json_create(g_alloc_heap, 512);
  JsonResult jsonRes;
  json_read(jsonDoc, src->data, &jsonRes);
  asset_repo_source_close(src);

  if (jsonRes.type != JsonResultType_Success) {
    gltf_load_fail_msg(world, entity, GltfError_InvalidJson, json_error_str(jsonRes.error));
    json_destroy(jsonDoc);
    return;
  }

  if (json_type(jsonDoc, jsonRes.type) != JsonType_Object) {
    gltf_load_fail(world, entity, GltfError_MalformedFile);
    json_destroy(jsonDoc);
    return;
  }

  ecs_world_add_t(
      world,
      entity,
      AssetGltfLoadComp,
      .assetId     = id,
      .jDoc        = jsonDoc,
      .jRoot       = jsonRes.type,
      .buffers     = dynarray_create_t(g_alloc_heap, GltfBuffer, 1),
      .bufferViews = dynarray_create_t(g_alloc_heap, GltfBufferView, 8),
      .accessors   = dynarray_create_t(g_alloc_heap, GltfAccessor, 8),
      .primitives  = dynarray_create_t(g_alloc_heap, GltfPrim, 4));
}
