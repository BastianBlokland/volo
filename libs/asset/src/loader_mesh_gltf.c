#include "asset_mesh.h"
#include "asset_raw.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_path.h"
#include "core_stringtable.h"
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
 * NOTE: Only meshes[0] and skins[0] are imported.
 * NOTE: Assumes that skinning information in meshes[0] matches the skin[0] skeleton.
 *
 * NOTE: Gltf buffer-data uses little-endian byte-order and 2's complement integers, and this loader
 * assumes the host system matches that.
 */

typedef enum {
  GltfLoadPhase_BuffersAcquire,
  GltfLoadPhase_BuffersWait,
  GltfLoadPhase_Parse,
} GltfLoadPhase;

typedef struct {
  u32         length;
  EcsEntityId entity;
  String      data; // NOTE: Available after the BuffersWait phase.
} GltfBuffer;

typedef struct {
  String data;
} GltfView;

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
} GltfAccess;

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
  u32          accIndices;  // Access index [Optional].
  u32          accPosition; // Access index.
  u32          accTexcoord; // Access index [Optional].
  u32          accNormal;   // Access index [Optional].
  u32          accTangent;  // Access index [Optional].
  u32          accJoints;   // Access index [Optional].
  u32          accWeights;  // Access index [Optional].
} GltfPrim;

typedef struct {
  u32 accInput;  // Access index [Optional].
  u32 accOutput; // Access index [Optional].
} GltfAnimChannel;

typedef struct {
  u32              nodeIndex;
  AssetMeshAnimPtr childData;
  u32              childCount;
  StringHash       nameHash;
  GeoVector        trans; // x,y,z vector.
  GeoVector        rot;   // x,y,z,w quaternion.
  GeoVector        scale; // x,y,z vector.
} GltfJoint;

typedef struct {
  StringHash      nameHash;
  GltfAnimChannel channels[asset_mesh_joints_max][AssetMeshAnimTarget_Count];
} GltfAnim;

ecs_comp_define(AssetGltfLoadComp) {
  String        assetId;
  JsonDoc*      jDoc;
  JsonVal       jRoot;
  GltfLoadPhase phase;
  GltfBuffer*   buffers;
  GltfView*     views;
  GltfAccess*   access;
  GltfPrim*     prims;
  GltfJoint*    joints;
  GltfAnim*     anims;
  DynArray      animData; // u8[].
  u32           bufferCount;
  u32           viewCount;
  u32           accessCount;
  u32           primCount;
  u32           jointCount;
  u32           animCount;
  u32           accBindPoseInvMats; // Access index [Optional].
  u32           rootJointIndex;     // [Optional].
};

typedef AssetGltfLoadComp GltfLoad;

static void ecs_destruct_gltf_load_comp(void* data) {
  AssetGltfLoadComp* comp = data;
  json_destroy(comp->jDoc);
  if (comp->bufferCount) {
    alloc_free_array_t(g_alloc_heap, comp->buffers, comp->bufferCount);
  }
  if (comp->viewCount) {
    alloc_free_array_t(g_alloc_heap, comp->views, comp->viewCount);
  }
  if (comp->accessCount) {
    alloc_free_array_t(g_alloc_heap, comp->access, comp->accessCount);
  }
  if (comp->primCount) {
    alloc_free_array_t(g_alloc_heap, comp->prims, comp->primCount);
  }
  if (comp->jointCount) {
    alloc_free_array_t(g_alloc_heap, comp->joints, comp->jointCount);
  }
  if (comp->animCount) {
    alloc_free_array_t(g_alloc_heap, comp->anims, comp->animCount);
  }
  dynarray_destroy(&comp->animData);
}

typedef enum {
  GltfError_None = 0,
  GltfError_InvalidJson,
  GltfError_MalformedFile,
  GltfError_MalformedBuffers,
  GltfError_MalformedBufferViews,
  GltfError_MalformedAccessors,
  GltfError_MalformedPrims,
  GltfError_MalformedPrimIndices,
  GltfError_MalformedPrimPositions,
  GltfError_MalformedPrimNormals,
  GltfError_MalformedPrimTangents,
  GltfError_MalformedPrimTexcoords,
  GltfError_MalformedPrimJoints,
  GltfError_MalformedPrimWeights,
  GltfError_MalformedSkin,
  GltfError_MalformedNodes,
  GltfError_MalformedAnimation,
  GltfError_JointCountExceedsMaximum,
  GltfError_InvalidBuffer,
  GltfError_UnsupportedPrimitiveMode,
  GltfError_UnsupportedInterpolationMode,
  GltfError_NoPrimitives,

  GltfError_Count,
} GltfError;

static String gltf_error_str(const GltfError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Invalid json"),
      string_static("Malformed gltf file"),
      string_static("Gltf 'buffers' field malformed"),
      string_static("Gltf 'bufferViews' field malformed"),
      string_static("Gltf 'accessors' field malformed"),
      string_static("Gltf 'primitives' field malformed"),
      string_static("Malformed primitive indices"),
      string_static("Malformed primitive positions"),
      string_static("Malformed primitive normals"),
      string_static("Malformed primitive tangents"),
      string_static("Malformed primitive texcoords"),
      string_static("Malformed primitive joints"),
      string_static("Malformed primitive weights"),
      string_static("Malformed skin"),
      string_static("Malformed nodes"),
      string_static("Malformed animation"),
      string_static("Joint count exceeds maximum"),
      string_static("Gltf invalid buffer"),
      string_static("Unsupported primitive mode, only triangle primitives supported"),
      string_static("Unsupported interpolation mode, only linear interpolation supported"),
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

INLINE_HINT u32 gltf_comp_size(const GltfType type) {
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

INLINE_HINT static bool gltf_json_check(GltfLoad* ld, const JsonVal v, const JsonType type) {
  return LIKELY(!sentinel_check(v) && json_type(ld->jDoc, v) == type);
}

static u32 gltf_json_elem_count(GltfLoad* ld, const JsonVal v) {
  return gltf_json_check(ld, v, JsonType_Array) ? json_elem_count(ld->jDoc, v) : 0;
}

static bool gltf_json_elem_f32(GltfLoad* ld, const JsonVal v, const u32 index, f32* out) {
  if (!gltf_json_check(ld, v, JsonType_Array)) {
    return false;
  }
  const JsonVal elem = json_elem(ld->jDoc, v, index);
  if (!gltf_json_check(ld, elem, JsonType_Number)) {
    return false;
  }
  *out = (f32)json_number(ld->jDoc, elem);
  return true;
}

static bool gltf_json_field_u32(GltfLoad* ld, const JsonVal v, const String name, u32* out) {
  if (!gltf_json_check(ld, v, JsonType_Object)) {
    return false;
  }
  const JsonVal jField = json_field(ld->jDoc, v, name);
  if (!gltf_json_check(ld, jField, JsonType_Number)) {
    return false;
  }
  *out = (u32)json_number(ld->jDoc, jField);
  return true;
}

static bool gltf_json_field_str(GltfLoad* ld, const JsonVal v, const String name, String* out) {
  if (!gltf_json_check(ld, v, JsonType_Object)) {
    return false;
  }
  const JsonVal jField = json_field(ld->jDoc, v, name);
  if (!gltf_json_check(ld, jField, JsonType_String)) {
    return false;
  }
  *out = json_string(ld->jDoc, jField);
  return true;
}

static bool gltf_json_field_vec3(GltfLoad* ld, const JsonVal v, const String name, GeoVector* out) {
  if (UNLIKELY(json_type(ld->jDoc, v) != JsonType_Object)) {
    return false;
  }
  const JsonVal jField  = json_field(ld->jDoc, v, name);
  bool          success = true;
  for (u32 i = 0; i != 3; ++i) {
    success &= gltf_json_elem_f32(ld, jField, i, &out->comps[i]);
  }
  return success;
}

static bool gltf_json_field_vec4(GltfLoad* ld, const JsonVal v, const String name, GeoVector* out) {
  if (UNLIKELY(json_type(ld->jDoc, v) != JsonType_Object)) {
    return false;
  }
  const JsonVal jField  = json_field(ld->jDoc, v, name);
  bool          success = true;
  for (u32 i = 0; i != 4; ++i) {
    success &= gltf_json_elem_f32(ld, jField, i, &out->comps[i]);
  }
  return success;
}

static void gltf_json_name(GltfLoad* ld, const JsonVal v, StringHash* out) {
  String str;
  *out = stringtable_add(
      g_stringtable, gltf_json_field_str(ld, v, string_lit("name"), &str) ? str : string_empty);
}

static u32 gltf_node_to_joint_index(GltfLoad* ld, const u32 nodeIndex) {
  for (u32 i = 0; i != ld->jointCount; ++i) {
    if (ld->joints[i].nodeIndex == nodeIndex) {
      return i;
    }
  }
  return sentinel_u32;
}

static String gltf_buffer_asset_id(GltfLoad* ld, const String uri) {
  const String root = path_parent(ld->assetId);
  return root.size ? fmt_write_scratch("{}/{}", fmt_text(root), fmt_text(uri)) : uri;
}

static bool gtlf_access_check_type(const GltfType type) {
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

static bool gltf_access_check(GltfLoad* ld, const u32 i, const GltfType type, const u32 compCount) {
  if (UNLIKELY(i >= ld->accessCount)) {
    return false;
  }
  return ld->access[i].compType == type && ld->access[i].compCount == compCount;
}

static f32 gltf_access_max_f32(GltfLoad* ld, const u32 acc) {
  diag_assert(ld->access[acc].compType == GltfType_f32);
  f32 res = f32_min;
  for (u32 i = 0; i != ld->access[acc].compCount * ld->access[acc].count; ++i) {
    res = math_max(res, ld->access[acc].data_f32[i]);
  }
  return res;
}

static AssetMeshAnimPtr gltf_anim_data_begin(GltfLoad* ld, const u32 align) {
  // Insert padding to reach the requested alignment.
  dynarray_push(&ld->animData, bits_padding_32((u32)ld->animData.size, align));
  return (u32)ld->animData.size;
}

static AssetMeshAnimPtr gltf_anim_data_push_vec(GltfLoad* ld, const GeoVector val) {
  const AssetMeshAnimPtr res = gltf_anim_data_begin(ld, alignof(GeoVector));
  *((GeoVector*)dynarray_push(&ld->animData, sizeof(GeoVector)).ptr) = val;
  return res;
}

static AssetMeshAnimPtr gltf_anim_data_push_access(GltfLoad* ld, const u32 acc) {
  const u32 elemSize         = gltf_comp_size(ld->access[acc].compType) * ld->access[acc].compCount;
  const AssetMeshAnimPtr res = gltf_anim_data_begin(ld, bits_nextpow2(elemSize));
  const Mem accessorMem = mem_create(ld->access[acc].data_raw, elemSize * ld->access[acc].count);
  mem_cpy(dynarray_push(&ld->animData, accessorMem.size), accessorMem);
  return res;
}

static AssetMeshAnimPtr gltf_anim_data_push_access_vec(GltfLoad* ld, const u32 acc) {
  diag_assert(ld->access[acc].compType == GltfType_f32);
  const u32 compCount      = ld->access[acc].compCount;
  const u32 totalCompCount = compCount * ld->access[acc].count;

  const AssetMeshAnimPtr res = gltf_anim_data_begin(ld, alignof(GeoVector));
  for (u32 i = 0; i != totalCompCount; i += compCount) {
    mem_cpy(
        dynarray_push(&ld->animData, sizeof(f32) * 4),
        mem_create(&ld->access[acc].data_f32[i], sizeof(f32) * compCount));
  }
  return res;
}

static AssetMeshAnimPtr gltf_anim_data_push_access_mat(GltfLoad* ld, const u32 acc) {
  diag_assert(ld->access[acc].compType == GltfType_f32);
  diag_assert(ld->access[acc].compCount == 16);

  const GeoMatrix*       src = ld->access[acc].data_raw;
  const AssetMeshAnimPtr res = gltf_anim_data_begin(ld, alignof(GeoMatrix));
  for (u32 i = 0; i != ld->access[acc].count; ++i) {
    /**
     * Gltf also uses column-major 4x4 f32 matrices, the only post-processing needed is converting
     * from a right-handed to a left-handed coordinate system.
     */
    static const GeoMatrix g_negZMat = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, -1, 0}, {0, 0, 0, 1}}};
    const GeoMatrix        mat       = geo_matrix_mul(&src[i], &g_negZMat);
    mem_cpy(dynarray_push(&ld->animData, sizeof(GeoMatrix)), mem_var(mat));
  }
  return res;
}

static bool gltf_accessor_check(const String typeString, u32* outCompCount) {
  if (string_eq(typeString, string_lit("SCALAR"))) {
    *outCompCount = 1;
    return true;
  }
  if (string_eq(typeString, string_lit("VEC2"))) {
    *outCompCount = 2;
    return true;
  }
  if (string_eq(typeString, string_lit("VEC3"))) {
    *outCompCount = 3;
    return true;
  }
  if (string_eq(typeString, string_lit("VEC4"))) {
    *outCompCount = 4;
    return true;
  }
  if (string_eq(typeString, string_lit("MAT2"))) {
    *outCompCount = 8;
    return true;
  }
  if (string_eq(typeString, string_lit("MAT3"))) {
    *outCompCount = 12;
    return true;
  }
  if (string_eq(typeString, string_lit("MAT4"))) {
    *outCompCount = 16;
    return true;
  }
  return false;
}

static void gltf_buffers_acquire(GltfLoad* ld, EcsWorld* w, AssetManagerComp* man, GltfError* err) {
  const JsonVal buffers = json_field(ld->jDoc, ld->jRoot, string_lit("buffers"));
  if (!(ld->bufferCount = gltf_json_elem_count(ld, buffers))) {
    goto Error;
  }
  ld->buffers = alloc_array_t(g_alloc_heap, GltfBuffer, ld->bufferCount);
  mem_set(mem_create(ld->buffers, sizeof(GltfBuffer) * ld->bufferCount), 0);
  GltfBuffer* out = ld->buffers;

  json_for_elems(ld->jDoc, buffers, bufferElem) {
    if (!gltf_json_field_u32(ld, bufferElem, string_lit("byteLength"), &out->length)) {
      goto Error;
    }
    String uri;
    if (!gltf_json_field_str(ld, bufferElem, string_lit("uri"), &uri)) {
      goto Error;
    }
    const String assetId = gltf_buffer_asset_id(ld, uri);
    if (string_eq(assetId, ld->assetId)) {
      goto Error; // Cannot load this same file again as a buffer.
    }
    out->entity = asset_lookup(w, man, assetId);
    asset_acquire(w, out->entity);
    ++out;
  }
  *err = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedBuffers;
}

static void gltf_parse_views(GltfLoad* ld, GltfError* err) {
  const JsonVal views = json_field(ld->jDoc, ld->jRoot, string_lit("bufferViews"));
  if (!(ld->viewCount = gltf_json_elem_count(ld, views))) {
    goto Error;
  }
  ld->views     = alloc_array_t(g_alloc_heap, GltfView, ld->viewCount);
  GltfView* out = ld->views;

  json_for_elems(ld->jDoc, views, bufferView) {
    u32 bufferIndex;
    if (!gltf_json_field_u32(ld, bufferView, string_lit("buffer"), &bufferIndex)) {
      goto Error;
    }
    if (bufferIndex >= ld->bufferCount) {
      goto Error;
    }
    const GltfBuffer* buffer     = &ld->buffers[bufferIndex];
    u32               byteOffset = 0, byteLength;
    gltf_json_field_u32(ld, bufferView, string_lit("byteOffset"), &byteOffset);
    if (!gltf_json_field_u32(ld, bufferView, string_lit("byteLength"), &byteLength)) {
      goto Error;
    }
    if (byteOffset + byteLength > buffer->data.size) {
      goto Error;
    }
    *out++ = (GltfView){.data = string_slice(buffer->data, byteOffset, byteLength)};
  }
  *err = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedBufferViews;
}

static void gltf_parse_accessors(GltfLoad* ld, GltfError* err) {
  const JsonVal accessors = json_field(ld->jDoc, ld->jRoot, string_lit("accessors"));
  if (!(ld->accessCount = gltf_json_elem_count(ld, accessors))) {
    goto Error;
  }
  ld->access      = alloc_array_t(g_alloc_heap, GltfAccess, ld->accessCount);
  GltfAccess* out = ld->access;

  json_for_elems(ld->jDoc, accessors, accessor) {
    u32 viewIndex;
    if (!gltf_json_field_u32(ld, accessor, string_lit("bufferView"), &viewIndex)) {
      goto Error;
    }
    if (viewIndex >= ld->viewCount) {
      goto Error;
    }
    u32 byteOffset = 0;
    gltf_json_field_u32(ld, accessor, string_lit("byteOffset"), &byteOffset);
    if (!gltf_json_field_u32(ld, accessor, string_lit("componentType"), (u32*)&out->compType)) {
      goto Error;
    }
    if (!gtlf_access_check_type(out->compType)) {
      goto Error;
    }
    if (!gltf_json_field_u32(ld, accessor, string_lit("count"), &out->count)) {
      goto Error;
    }
    String typeString;
    if (!gltf_json_field_str(ld, accessor, string_lit("type"), &typeString)) {
      goto Error;
    }
    if (!gltf_accessor_check(typeString, &out->compCount)) {
      goto Error;
    }
    const String viewData = ld->views[viewIndex].data;
    if (byteOffset + gltf_comp_size(out->compType) * out->compCount * out->count > viewData.size) {
      goto Error;
    }
    out->data_raw = mem_at_u8(viewData, byteOffset);
    ++out;
  }
  *err = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedAccessors;
}

static void gltf_parse_primitives(GltfLoad* ld, GltfError* err) {
  /**
   * NOTE: This loader only supports a single mesh.
   */
  const JsonVal meshes = json_field(ld->jDoc, ld->jRoot, string_lit("meshes"));
  if (!gltf_json_elem_count(ld, meshes)) {
    goto Error;
  }
  const JsonVal mesh = json_elem_begin(ld->jDoc, meshes);
  if (json_type(ld->jDoc, mesh) != JsonType_Object) {
    goto Error;
  }
  const JsonVal primitives = json_field(ld->jDoc, mesh, string_lit("primitives"));
  if (!(ld->primCount = gltf_json_elem_count(ld, primitives))) {
    goto Error;
  }
  ld->prims     = alloc_array_t(g_alloc_heap, GltfPrim, ld->primCount);
  GltfPrim* out = ld->prims;

  json_for_elems(ld->jDoc, primitives, primitive) {
    if (json_type(ld->jDoc, primitive) != JsonType_Object) {
      goto Error;
    }
    out->mode = GltfPrimMode_Triangles;
    gltf_json_field_u32(ld, primitive, string_lit("mode"), (u32*)&out->mode);
    if (out->mode > GltfPrimMode_Max) {
      goto Error;
    }
    out->accIndices = sentinel_u32; // Indices are optional.
    gltf_json_field_u32(ld, primitive, string_lit("indices"), &out->accIndices);
    const JsonVal attributes = json_field(ld->jDoc, primitive, string_lit("attributes"));
    if (!gltf_json_check(ld, attributes, JsonType_Object)) {
      goto Error;
    }
    if (!gltf_json_field_u32(ld, attributes, string_lit("POSITION"), &out->accPosition)) {
      goto Error;
    }
    out->accTexcoord = sentinel_u32; // Texcoords are optional.
    gltf_json_field_u32(ld, attributes, string_lit("TEXCOORD_0"), &out->accTexcoord);
    out->accNormal = sentinel_u32; // Normals are optional.
    gltf_json_field_u32(ld, attributes, string_lit("NORMAL"), &out->accNormal);
    out->accTangent = sentinel_u32; // Tangents are optional.
    gltf_json_field_u32(ld, attributes, string_lit("TANGENT"), &out->accTangent);
    out->accJoints = sentinel_u32; // Joints are optional.
    gltf_json_field_u32(ld, attributes, string_lit("JOINTS_0"), &out->accJoints);
    out->accWeights = sentinel_u32; // Weights are optional.
    gltf_json_field_u32(ld, attributes, string_lit("WEIGHTS_0"), &out->accWeights);
    ++out;
  }
  *err = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedPrims;
}

static void gltf_parse_skin(GltfLoad* ld, GltfError* err) {
  /**
   * NOTE: This loader only supports a single skin.
   */
  const JsonVal skins = json_field(ld->jDoc, ld->jRoot, string_lit("skins"));
  if (!gltf_json_elem_count(ld, skins)) {
    goto Success; // Skinning is optional.
  }
  const JsonVal skin = json_elem_begin(ld->jDoc, skins);
  if (json_type(ld->jDoc, skin) != JsonType_Object) {
    goto Error;
  }
  if (!gltf_json_field_u32(ld, skin, string_lit("inverseBindMatrices"), &ld->accBindPoseInvMats)) {
    goto Error;
  }
  const JsonVal joints = json_field(ld->jDoc, skin, string_lit("joints"));
  if (!gltf_json_check(ld, joints, JsonType_Array)) {
    goto Error;
  }
  if (!(ld->jointCount = json_elem_count(ld->jDoc, joints))) {
    goto Error;
  }
  if (ld->jointCount > asset_mesh_joints_max) {
    *err = GltfError_JointCountExceedsMaximum;
    return;
  }
  ld->joints = alloc_array_t(g_alloc_heap, GltfJoint, ld->jointCount);

  GltfJoint* outJoint = ld->joints;
  json_for_elems(ld->jDoc, joints, joint) {
    if (UNLIKELY(json_type(ld->jDoc, joint) != JsonType_Number)) {
      goto Error;
    }
    *outJoint++ = (GltfJoint){.nodeIndex = (u32)json_number(ld->jDoc, joint)};
  }
  u32 skeletonNodeIndex;
  if (!gltf_json_field_u32(ld, skin, string_lit("skeleton"), &skeletonNodeIndex)) {
    goto Error;
  }
  if (sentinel_check(ld->rootJointIndex = gltf_node_to_joint_index(ld, skeletonNodeIndex))) {
    goto Error;
  }
Success:
  *err = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedSkin;
}

static void gltf_parse_skeleton_nodes(GltfLoad* ld, GltfError* err) {
  const JsonVal nodes = json_field(ld->jDoc, ld->jRoot, string_lit("nodes"));
  if (!gltf_json_elem_count(ld, nodes)) {
    goto Error;
  }
  u32 nodeIndex = 0, jointIndex;
  json_for_elems(ld->jDoc, nodes, node) {
    if (UNLIKELY(json_type(ld->jDoc, node) != JsonType_Object)) {
      goto Error;
    }
    if (sentinel_check(jointIndex = gltf_node_to_joint_index(ld, nodeIndex))) {
      goto Next; // This node is not part of the skeleton.
    }
    GltfJoint* out = &ld->joints[jointIndex];
    gltf_json_name(ld, node, &out->nameHash);

    out->trans = geo_vector(0);
    gltf_json_field_vec3(ld, node, string_lit("translation"), &out->trans);

    out->rot = geo_vector(0, 0, 0, 1);
    gltf_json_field_vec4(ld, node, string_lit("rotation"), &out->rot);

    out->scale = geo_vector(1, 1, 1);
    gltf_json_field_vec3(ld, node, string_lit("scale"), &out->trans);

    const JsonVal children = json_field(ld->jDoc, node, string_lit("children"));
    if (gltf_json_check(ld, children, JsonType_Array)) {
      out->childData  = gltf_anim_data_begin(ld, alignof(u32));
      out->childCount = json_elem_count(ld->jDoc, children);

      json_for_elems(ld->jDoc, children, child) {
        if (UNLIKELY(json_type(ld->jDoc, child) != JsonType_Number)) {
          goto Error;
        }
        const u32 childJointIndex = gltf_node_to_joint_index(ld, (u32)json_number(ld->jDoc, child));
        *((u32*)dynarray_push(&ld->animData, sizeof(u32)).ptr) = childJointIndex;
      }
    }

  Next:
    ++nodeIndex;
  }
  *err = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedNodes;
}

static bool gltf_anim_target(const String str, AssetMeshAnimTarget* out) {
  static const String g_names[] = {
      string_static("translation"), string_static("rotation"), string_static("scale")};
  for (u32 i = 0; i != array_elems(g_names); ++i) {
    if (string_eq(str, g_names[i])) {
      *out = i;
      return true;
    }
  }
  return false;
}

static void gltf_clear_anim_channels(GltfAnim* anim) {
  for (u32 jointIndex = 0; jointIndex != asset_mesh_joints_max; ++jointIndex) {
    for (AssetMeshAnimTarget target = 0; target != AssetMeshAnimTarget_Count; ++target) {
      anim->channels[jointIndex][target].accInput  = sentinel_u32;
      anim->channels[jointIndex][target].accOutput = sentinel_u32;
    }
  }
}

static void gltf_parse_animations(GltfLoad* ld, GltfError* err) {
  const JsonVal animations = json_field(ld->jDoc, ld->jRoot, string_lit("animations"));
  if (!(ld->animCount = gltf_json_elem_count(ld, animations))) {
    goto Success; // Animations are optional.
  }
  ld->anims         = alloc_array_t(g_alloc_heap, GltfAnim, ld->animCount);
  GltfAnim* outAnim = ld->anims;

  enum { GltfMaxSamplerCount = 1024 };
  u32 samplerAccInput[GltfMaxSamplerCount];
  u32 samplerAccOutput[GltfMaxSamplerCount];
  u32 samplerCnt;

  json_for_elems(ld->jDoc, animations, anim) {
    gltf_clear_anim_channels(outAnim);

    if (json_type(ld->jDoc, anim) != JsonType_Object) {
      goto Error;
    }
    gltf_json_name(ld, anim, &outAnim->nameHash);

    const JsonVal samplers = json_field(ld->jDoc, anim, string_lit("samplers"));
    if (!gltf_json_check(ld, samplers, JsonType_Array)) {
      goto Error;
    }
    samplerCnt = 0;
    json_for_elems(ld->jDoc, samplers, sampler) {
      if (json_type(ld->jDoc, sampler) != JsonType_Object) {
        goto Error;
      }
      if (!gltf_json_field_u32(ld, sampler, string_lit("input"), &samplerAccInput[samplerCnt])) {
        goto Error;
      }
      if (!gltf_json_field_u32(ld, sampler, string_lit("output"), &samplerAccOutput[samplerCnt])) {
        goto Error;
      }
      if (++samplerCnt == GltfMaxSamplerCount) {
        goto Error;
      }
      const JsonVal interpolation = json_field(ld->jDoc, sampler, string_lit("interpolation"));
      if (!gltf_json_check(ld, interpolation, JsonType_String)) {
        continue; // 'interpolation' is optional, default is 'LINEAR'.
      }
      if (!string_eq(json_string(ld->jDoc, interpolation), string_lit("LINEAR"))) {
        *err = GltfError_UnsupportedInterpolationMode;
        return;
      }
    }

    const JsonVal channels = json_field(ld->jDoc, anim, string_lit("channels"));
    if (!gltf_json_elem_count(ld, channels)) {
      goto Error;
    }
    json_for_elems(ld->jDoc, channels, channel) {
      if (json_type(ld->jDoc, channel) != JsonType_Object) {
        goto Error;
      }
      u32 samplerIdx, nodeIdx, jointIdx;
      if (!gltf_json_field_u32(ld, channel, string_lit("sampler"), &samplerIdx)) {
        goto Error;
      }
      if (samplerIdx >= samplerCnt) {
        goto Error;
      }

      const JsonVal target = json_field(ld->jDoc, channel, string_lit("target"));
      if (!gltf_json_check(ld, target, JsonType_Object)) {
        goto Error;
      }
      if (!gltf_json_field_u32(ld, target, string_lit("node"), &nodeIdx)) {
        goto Error;
      }
      if (sentinel_check(jointIdx = gltf_node_to_joint_index(ld, nodeIdx))) {
        goto Error;
      }
      const JsonVal path = json_field(ld->jDoc, target, string_lit("path"));
      if (!gltf_json_check(ld, path, JsonType_String)) {
        goto Error;
      }
      AssetMeshAnimTarget channelTarget;
      if (!gltf_anim_target(json_string(ld->jDoc, path), &channelTarget)) {
        goto Error;
      }
      diag_assert(samplerAccInput[samplerIdx]);
      outAnim->channels[jointIdx][channelTarget] = (GltfAnimChannel){
          .accInput = samplerAccInput[samplerIdx], .accOutput = samplerAccOutput[samplerIdx]};
    }
    ++outAnim;
  }
Success:
  *err = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedAnimation;
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

static GltfMeshMeta gltf_mesh_meta(GltfLoad* ld, GltfError* err) {
  // clang-format off
#define verify(_EXPR_, _ERR_) if (UNLIKELY(!(_EXPR_))) { *err = GltfError_##_ERR_; goto Error; }
  // clang-format on

  verify(ld->primCount, NoPrimitives);

  u32         vertexCount = 0;
  GltfFeature features    = ~0; // Assume we have all features until accessors are missing.
  for (GltfPrim* prim = ld->prims; prim != ld->prims + ld->primCount; ++prim) {
    verify(prim->mode == GltfPrimMode_Triangles, UnsupportedPrimitiveMode);
    verify(gltf_access_check(ld, prim->accPosition, GltfType_f32, 3), MalformedPrimPositions);

    const u32 attrCount = ld->access[prim->accPosition].count;
    if (sentinel_check(prim->accIndices)) {
      // Non-indexed primitive.
      verify((attrCount % 3) == 0, MalformedPrimPositions);
      vertexCount += attrCount;
    } else {
      // Indexed primitive.
      verify(gltf_access_check(ld, prim->accIndices, GltfType_u16, 1), MalformedPrimIndices);
      verify((ld->access[prim->accIndices].count % 3) == 0, MalformedPrimIndices);
      vertexCount += ld->access[prim->accIndices].count;
    }
    if (sentinel_check(prim->accTexcoord)) {
      features &= ~GltfFeature_Texcoords;
    } else {
      verify(gltf_access_check(ld, prim->accTexcoord, GltfType_f32, 2), MalformedPrimTexcoords);
      verify(ld->access[prim->accTexcoord].count == attrCount, MalformedPrimTexcoords);
    }
    if (sentinel_check(prim->accNormal)) {
      features &= ~GltfFeature_Normals;
    } else {
      verify(gltf_access_check(ld, prim->accNormal, GltfType_f32, 3), MalformedPrimNormals);
      verify(ld->access[prim->accNormal].count == attrCount, MalformedPrimNormals);
    }
    if (sentinel_check(prim->accTangent)) {
      features &= ~GltfFeature_Tangents;
    } else {
      verify(gltf_access_check(ld, prim->accTangent, GltfType_f32, 4), MalformedPrimTangents);
      verify(ld->access[prim->accTangent].count == attrCount, MalformedPrimTangents);
    }
    if (sentinel_check(prim->accJoints)) {
      features &= ~GltfFeature_Skinning;
    } else {
      verify(gltf_access_check(ld, prim->accJoints, GltfType_u16, 4), MalformedPrimJoints);
      verify(ld->access[prim->accJoints].count == attrCount, MalformedPrimJoints);
      verify(gltf_access_check(ld, prim->accWeights, GltfType_f32, 4), MalformedPrimWeights);
      verify(ld->access[prim->accWeights].count == attrCount, MalformedPrimWeights);
    }
  }
  return (GltfMeshMeta){.features = features, .vertexCount = vertexCount};

Error:
  return (GltfMeshMeta){0};

#undef verify
}

static void gltf_build_mesh(GltfLoad* ld, AssetMeshComp* out, GltfError* err) {
  GltfMeshMeta meta = gltf_mesh_meta(ld, err);
  if (*err) {
    return;
  }
  AssetMeshBuilder* builder = asset_mesh_builder_create(g_alloc_heap, meta.vertexCount);

  typedef const u16* AccessorU16;
  typedef const f32* AccessorF32;
  AccessorF32        positions, texcoords, normals, tangents, weights;
  AccessorU16        indices, joints;
  u32                attrCount, vertexCount;

  for (GltfPrim* prim = ld->prims; prim != ld->prims + ld->primCount; ++prim) {
    positions = ld->access[prim->accPosition].data_f32;
    attrCount = ld->access[prim->accPosition].count;
    if (meta.features & GltfFeature_Texcoords) {
      texcoords = ld->access[prim->accTexcoord].data_f32;
    }
    if (meta.features & GltfFeature_Normals) {
      normals = ld->access[prim->accNormal].data_f32;
    }
    if (meta.features & GltfFeature_Tangents) {
      tangents = ld->access[prim->accTangent].data_f32;
    }
    if (meta.features & GltfFeature_Skinning) {
      joints  = ld->access[prim->accJoints].data_u16;
      weights = ld->access[prim->accWeights].data_f32;
    }
    const bool indexed = !sentinel_check(prim->accIndices);
    if (indexed) {
      indices     = ld->access[prim->accIndices].data_u16;
      vertexCount = ld->access[prim->accIndices].count;
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
        const u16*       vertJoints = &joints[attr * 4];
        const GeoVector* vertWeight = (const GeoVector*)&weights[attr * 4];
        const u16 j0 = vertJoints[0], j1 = vertJoints[1], j2 = vertJoints[2], j3 = vertJoints[3];
        const u32 jointMax = ld->jointCount;
        if (UNLIKELY(j0 >= jointMax || j1 >= jointMax || j2 >= jointMax || j3 >= jointMax)) {
          *err = GltfError_MalformedPrimJoints;
          goto Cleanup;
        }
        asset_mesh_builder_set_skin(
            builder,
            vertIdx,
            (AssetMeshSkin){.joints = {(u8)j0, (u8)j1, (u8)j2, (u8)j3}, .weights = *vertWeight});
      }
    }
  }
  if (!(meta.features & GltfFeature_Normals)) {
    asset_mesh_compute_flat_normals(builder);
  }
  if (!(meta.features & GltfFeature_Tangents)) {
    asset_mesh_compute_tangents(builder);
  }
  *out = asset_mesh_create(builder);
  *err = GltfError_None;

Cleanup:
  asset_mesh_builder_destroy(builder);
}

static void gltf_build_skeleton(GltfLoad* ld, AssetMeshSkeletonComp* out, GltfError* err) {
  diag_assert(ld->jointCount);

  if (!gltf_access_check(ld, ld->accBindPoseInvMats, GltfType_f32, 16)) {
    goto Error;
  }
  if (ld->access[ld->accBindPoseInvMats].count < ld->jointCount) {
    goto Error;
  }

  // Verify the accessors of all animated channels.
  for (u32 animIndex = 0; animIndex != ld->animCount; ++animIndex) {
    for (u32 jointIndex = 0; jointIndex != ld->jointCount; ++jointIndex) {
      for (AssetMeshAnimTarget target = 0; target != AssetMeshAnimTarget_Count; ++target) {
        const GltfAnimChannel* channel = &ld->anims[animIndex].channels[jointIndex][target];
        if (sentinel_check(channel->accInput)) {
          continue; // Channel is not animated.
        }
        if (!gltf_access_check(ld, channel->accInput, GltfType_f32, 1)) {
          goto Error;
        }
        const u32 requiredComponents = target == AssetMeshAnimTarget_Rotation ? 4 : 3;
        if (!gltf_access_check(ld, channel->accOutput, GltfType_f32, requiredComponents)) {
          goto Error;
        }
        if (ld->access[channel->accInput].count != ld->access[channel->accOutput].count) {
          goto Error;
        }
      }
    }
  }

  // Create the joint output structures.
  AssetMeshJoint* resJoints = alloc_array_t(g_alloc_heap, AssetMeshJoint, ld->jointCount);
  for (u32 jointIndex = 0; jointIndex != ld->jointCount; ++jointIndex) {
    resJoints[jointIndex] = (AssetMeshJoint){
        .childData  = ld->joints[jointIndex].childData,
        .childCount = ld->joints[jointIndex].childCount,
        .nameHash   = ld->joints[jointIndex].nameHash,
    };
  }

  // Create the animation output structures.
  AssetMeshAnim* resAnims =
      ld->animCount ? alloc_array_t(g_alloc_heap, AssetMeshAnim, ld->animCount) : null;
  for (u32 animIndex = 0; animIndex != ld->animCount; ++animIndex) {
    resAnims[animIndex].nameHash = ld->anims[animIndex].nameHash;
    f32 duration                 = 0;

    for (u32 jointIndex = 0; jointIndex != ld->jointCount; ++jointIndex) {
      for (AssetMeshAnimTarget target = 0; target != AssetMeshAnimTarget_Count; ++target) {
        const GltfAnimChannel* srcChannel = &ld->anims[animIndex].channels[jointIndex][target];
        AssetMeshAnimChannel*  resChannel = &resAnims[animIndex].joints[jointIndex][target];

        if (!sentinel_check(srcChannel->accInput)) {
          const f32 channelDur = gltf_access_max_f32(ld, srcChannel->accInput);
          duration             = math_max(duration, channelDur);

          // TODO: Support mirroring (for R to L coord conv) when animating the scale of the root.
          *resChannel = (AssetMeshAnimChannel){
              .frameCount = ld->access[srcChannel->accInput].count,
              .timeData   = gltf_anim_data_push_access(ld, srcChannel->accInput),
              .valueData  = gltf_anim_data_push_access_vec(ld, srcChannel->accOutput),
          };
        } else {
          *resChannel = (AssetMeshAnimChannel){0};
        }
      }
    }
    resAnims[animIndex].duration = duration;
  }

  // Create the default pose output.
  AssetMeshAnimPtr resDefaultPose = gltf_anim_data_begin(ld, alignof(GeoVector));
  for (u32 jointIndex = 0; jointIndex != ld->jointCount; ++jointIndex) {
    const GltfJoint* joint = &ld->joints[jointIndex];

    gltf_anim_data_push_vec(ld, joint->trans);
    gltf_anim_data_push_vec(ld, joint->rot);

    // Mirror the root to convert from a Right-handed coordinate system to a Left-Handed.
    const GeoVector scaleMirror = geo_vector(joint->scale.x, joint->scale.y, -joint->scale.z);
    gltf_anim_data_push_vec(ld, jointIndex == ld->rootJointIndex ? scaleMirror : joint->scale);
  }

  *out = (AssetMeshSkeletonComp){
      .joints          = resJoints,
      .anims           = resAnims,
      .bindPoseInvMats = gltf_anim_data_push_access_mat(ld, ld->accBindPoseInvMats),
      .defaultPose     = resDefaultPose,
      .jointCount      = ld->jointCount,
      .animCount       = ld->animCount,
      .rootJointIndex  = sentinel_check(ld->rootJointIndex) ? 0 : ld->rootJointIndex,
      .animData = alloc_dup(g_alloc_heap, dynarray_at(&ld->animData, 0, ld->animData.size), 1),
  };
  *err = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedAnimation;
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
    case GltfLoadPhase_BuffersAcquire:
      gltf_buffers_acquire(ld, world, manager, &err);
      if (err) {
        goto Error;
      }
      ++ld->phase;
      goto Next;
    case GltfLoadPhase_BuffersWait:
      for (GltfBuffer* buffer = ld->buffers; buffer != ld->buffers + ld->bufferCount; ++buffer) {
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
    case GltfLoadPhase_Parse: {
      gltf_parse_views(ld, &err);
      if (err) {
        goto Error;
      }
      gltf_parse_accessors(ld, &err);
      if (err) {
        goto Error;
      }
      gltf_parse_primitives(ld, &err);
      if (err) {
        goto Error;
      }
      gltf_parse_skin(ld, &err);
      if (err) {
        goto Error;
      }
      gltf_parse_skeleton_nodes(ld, &err);
      if (err) {
        goto Error;
      }
      gltf_parse_animations(ld, &err);
      if (err) {
        goto Error;
      }
      AssetMeshComp resultMesh;
      gltf_build_mesh(ld, &resultMesh, &err);
      if (err) {
        goto Error;
      }
      *ecs_world_add_t(world, entity, AssetMeshComp) = resultMesh;
      if (ld->jointCount) {
        AssetMeshSkeletonComp resultSkeleton;
        gltf_build_skeleton(ld, &resultSkeleton, &err);
        if (err) {
          goto Error;
        }
        *ecs_world_add_t(world, entity, AssetMeshSkeletonComp) = resultSkeleton;
      }
      ecs_world_add_empty_t(world, entity, AssetLoadedComp);
      goto Cleanup;
    }
    }

  Error:
    gltf_load_fail(world, entity, err);

  Cleanup:
    for (GltfBuffer* buffer = ld->buffers; buffer != ld->buffers + ld->bufferCount; ++buffer) {
      if (buffer->entity) {
        asset_release(world, buffer->entity);
      }
    }
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
      .assetId            = id,
      .jDoc               = jsonDoc,
      .jRoot              = jsonRes.type,
      .accBindPoseInvMats = sentinel_u32,
      .rootJointIndex     = sentinel_u32,
      .animData           = dynarray_create(g_alloc_heap, 1, 1, 0));
}
