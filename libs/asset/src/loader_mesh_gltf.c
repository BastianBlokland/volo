#include "asset_raw.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_base64.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_dynstring.h"
#include "core_float.h"
#include "core_math.h"
#include "core_path.h"
#include "core_stringtable.h"
#include "ecs_entity.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "geo_matrix.h"
#include "json_doc.h"
#include "json_read.h"
#include "trace_tracer.h"

#include "import_mesh_internal.h"
#include "loader_mesh_internal.h"
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

#define gltf_uri_size_max 128
#define gltf_eq_threshold 1e-2f
#define gltf_skin_weight_min 1e-3f
#define gltf_transient_alloc_chunk_size (1 * usize_mebibyte)

#define glb_chunk_count_max 16

typedef enum {
  GltfLoadPhase_BuffersAcquire,
  GltfLoadPhase_BuffersWait,
  GltfLoadPhase_Parse,
} GltfLoadPhase;

typedef struct {
  u32 version, length;
} GlbHeader;

typedef enum {
  GlbChunkType_Json = 0x4E4F534A,
  GlbChunkType_Bin  = 0x004E4942,
} GlbChunkType;

typedef struct {
  u32   length, type;
  void* dataPtr;
} GlbChunk;

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
  GeoVector t;
  GeoQuat   r;
  GeoVector s;
} GltfTransform;

typedef struct {
  u32           nodeIndex;
  u32           parentIndex;
  u32           skinCount;      // Amount of vertices skinned to this joint.
  f32           boundingRadius; // Bounding radius of the vertices skinned to this joint.
  String        name;           // Interned in the global string-table.
  GltfTransform defaultTrans;
  GeoMatrix     bindMat, bindMatInv; // Bind-space to world-space matrix (and inverse).
} GltfJoint;

typedef struct {
  String          name; // Interned in the global string-table.
  f32             duration;
  GltfAnimChannel channels[asset_mesh_joints_max][AssetMeshAnimTarget_Count];
} GltfAnim;

ecs_comp_define(AssetGltfLoadComp) {
  Allocator*    transientAlloc;
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
  GltfTransform sceneTrans;
  u32           accBindInvMats; // Access index [Optional].

  AssetSource* glbDataSource;
  GlbChunk     glbBinChunk;
};

typedef AssetGltfLoadComp GltfLoad;

static void ecs_destruct_gltf_load_comp(void* data) {
  AssetGltfLoadComp* comp = data;
  json_destroy(comp->jDoc);
  if (comp->glbDataSource) {
    asset_repo_close(comp->glbDataSource);
  }
  alloc_chunked_destroy(comp->transientAlloc);
  dynarray_destroy(&comp->animData);
}

typedef enum {
  GltfError_None = 0,
  GltfError_InvalidJson,
  GltfError_MalformedFile,
  GltfError_MalformedGlbHeader,
  GltfError_MalformedGlbChunk,
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
  GltfError_MalformedBindMatrix,
  GltfError_MalformedSceneTransform,
  GltfError_MalformedSkin,
  GltfError_MalformedNodes,
  GltfError_MalformedAnimation,
  GltfError_JointCountExceedsMaximum,
  GltfError_AnimCountExceedsMaximum,
  GltfError_InvalidBuffer,
  GltfError_UnsupportedPrimitiveMode,
  GltfError_UnsupportedInterpolationMode,
  GltfError_UnsupportedGlbVersion,
  GltfError_GlbJsonChunkMissing,
  GltfError_GlbChunkCountExceedsMaximum,
  GltfError_NoPrimitives,
  GltfError_ImportFailed,

  GltfError_Count,
} GltfError;

static String gltf_error_str(const GltfError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Invalid json"),
      string_static("Malformed gltf file"),
      string_static("Malformed glb header"),
      string_static("Malformed glb chunk"),
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
      string_static("Malformed bind matrix"),
      string_static("Malformed scene transform"),
      string_static("Malformed skin"),
      string_static("Malformed nodes"),
      string_static("Malformed animation"),
      string_static("Joint count exceeds maximum"),
      string_static("Animation count exceeds maximum"),
      string_static("Gltf invalid buffer"),
      string_static("Unsupported primitive mode, only triangle primitives supported"),
      string_static("Unsupported interpolation mode, only linear interpolation supported"),
      string_static("Unsupported glb version"),
      string_static("Glb json chunk missing"),
      string_static("Glb chunk count exceeds maximum"),
      string_static("Gltf mesh does not have any primitives"),
      string_static("Import failed"),
  };
  ASSERT(array_elems(g_msgs) == GltfError_Count, "Incorrect number of gltf-error messages");
  return g_msgs[err];
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

static bool gltf_json_elem_u32(GltfLoad* ld, const JsonVal v, const u32 index, u32* out) {
  f32 outFloat;
  if (gltf_json_elem_f32(ld, v, index, &outFloat)) {
    *out = (u32)outFloat;
    return true;
  }
  return false;
}

static bool gltf_json_field_u32(GltfLoad* ld, const JsonVal v, const String name, u32* out) {
  if (!gltf_json_check(ld, v, JsonType_Object)) {
    return false;
  }
  const JsonVal jField = json_field(ld->jDoc, v, string_hash(name));
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
  const JsonVal jField = json_field(ld->jDoc, v, string_hash(name));
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
  const JsonVal jField  = json_field(ld->jDoc, v, string_hash(name));
  bool          success = true;
  for (u32 i = 0; i != 3; ++i) {
    success &= gltf_json_elem_f32(ld, jField, i, &out->comps[i]);
  }
  return success;
}

static bool gltf_json_field_quat(GltfLoad* ld, const JsonVal v, const String name, GeoQuat* out) {
  if (UNLIKELY(json_type(ld->jDoc, v) != JsonType_Object)) {
    return false;
  }
  const JsonVal jField  = json_field(ld->jDoc, v, string_hash(name));
  bool          success = true;
  for (u32 i = 0; i != 4; ++i) {
    success &= gltf_json_elem_f32(ld, jField, i, &out->comps[i]);
  }
  if (success) {
    *out = geo_quat_norm_or_ident(*out);
  }
  return success;
}

/**
 * NOTE: Returned strings are interned in the global string-table.
 */
static void gltf_json_name(GltfLoad* ld, const JsonVal v, String* out) {
  String str = string_empty;
  gltf_json_field_str(ld, v, string_lit("name"), &str);

  if (string_is_empty(str)) {
    *out = string_empty;
    return;
  }

  *out = stringtable_intern(g_stringtable, string_slice(str, 0, math_min(str.size, u8_max)));
}

static void gltf_json_transform(GltfLoad* ld, const JsonVal v, GltfTransform* out) {
  out->t = geo_vector(0);
  gltf_json_field_vec3(ld, v, string_lit("translation"), &out->t);

  out->r = geo_quat_ident;
  gltf_json_field_quat(ld, v, string_lit("rotation"), &out->r);

  out->s = geo_vector(1, 1, 1);
  gltf_json_field_vec3(ld, v, string_lit("scale"), &out->s);
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

static AssetMeshDataPtr gltf_data_begin(GltfLoad* ld, const u32 align) {
  // Insert padding to reach the requested alignment.
  dynarray_push(&ld->animData, bits_padding_32((u32)ld->animData.size, align));
  return (u32)ld->animData.size;
}

static AssetMeshDataPtr gltf_data_push_u32(GltfLoad* ld, const u32 val) {
  const AssetMeshDataPtr res                             = gltf_data_begin(ld, alignof(u32));
  *((u32*)dynarray_push(&ld->animData, sizeof(u32)).ptr) = val;
  return res;
}

static AssetMeshDataPtr gltf_data_push_f32(GltfLoad* ld, const f32 val) {
  const AssetMeshDataPtr res                             = gltf_data_begin(ld, alignof(f32));
  *((f32*)dynarray_push(&ld->animData, sizeof(f32)).ptr) = val;
  return res;
}

static AssetMeshDataPtr gltf_data_push_trans(GltfLoad* ld, const GltfTransform val) {
  const AssetMeshDataPtr res = gltf_data_begin(ld, alignof(GeoVector));
  *((GeoVector*)dynarray_push(&ld->animData, sizeof(GeoVector)).ptr) = val.t;
  *((GeoQuat*)dynarray_push(&ld->animData, sizeof(GeoQuat)).ptr)     = val.r;
  *((GeoVector*)dynarray_push(&ld->animData, sizeof(GeoVector)).ptr) = val.s;
  return res;
}

static AssetMeshDataPtr gltf_data_push_matrix(GltfLoad* ld, const GeoMatrix val) {
  const AssetMeshDataPtr res = gltf_data_begin(ld, alignof(GeoMatrix));
  *((GeoMatrix*)dynarray_push(&ld->animData, sizeof(GeoMatrix)).ptr) = val;
  return res;
}

static AssetMeshDataPtr gltf_data_push_string(GltfLoad* ld, const String val) {
  diag_assert(val.size <= u8_max);
  const AssetMeshDataPtr res                           = gltf_data_begin(ld, alignof(u8));
  *((u8*)dynarray_push(&ld->animData, sizeof(u8)).ptr) = (u8)val.size;
  mem_cpy(dynarray_push(&ld->animData, val.size), val);
  return res;
}

MAYBE_UNUSED static AssetMeshDataPtr gltf_data_push_access(GltfLoad* ld, const u32 acc) {
  const u32 elemSize         = gltf_comp_size(ld->access[acc].compType) * ld->access[acc].compCount;
  const AssetMeshDataPtr res = gltf_data_begin(ld, bits_nextpow2(elemSize));
  const Mem accessorMem = mem_create(ld->access[acc].data_raw, elemSize * ld->access[acc].count);
  mem_cpy(dynarray_push(&ld->animData, accessorMem.size), accessorMem);
  return res;
}

static AssetMeshDataPtr gltf_data_push_access_vec(GltfLoad* ld, const u32 acc) {
  diag_assert(ld->access[acc].compType == GltfType_f32);
  const u32 compCount      = ld->access[acc].compCount;
  const u32 totalCompCount = compCount * ld->access[acc].count;

  const AssetMeshDataPtr res = gltf_data_begin(ld, alignof(GeoVector));
  for (u32 i = 0; i != totalCompCount; i += compCount) {
    mem_cpy(
        dynarray_push(&ld->animData, sizeof(f32) * 4),
        mem_create(&ld->access[acc].data_f32[i], sizeof(f32) * compCount));
  }
  return res;
}

static AssetMeshDataPtr
gltf_data_push_access_norm16(GltfLoad* ld, const u32 acc, const f32 refValue) {
  diag_assert(ld->access[acc].compType == GltfType_f32);
  diag_assert(ld->access[acc].compCount == 1);

  const f32              refValueInv = refValue > 0 ? (1.0f / refValue) : 0.0f;
  const AssetMeshDataPtr res         = gltf_data_begin(ld, 16); // Always 16 byte aligned.
  for (u32 i = 0; i != ld->access[acc].count; ++i) {
    const f32 valNorm = ld->access[acc].data_f32[i] * refValueInv;
    *(u16*)dynarray_push(&ld->animData, sizeof(u16)).ptr = (u16)(valNorm * u16_max);
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

/**
 * "data" URL scheme.
 * Spec: https://www.rfc-editor.org/rfc/inline-errata/rfc2397.html
 * NOTE: Only base64 encoded binary data is supported at this time.
 */
static Mem gtlf_uri_data_resolve(GltfLoad* ld, String uri) {
  static const String g_prefix = string_static("data:application/octet-stream;base64,");
  if (!string_starts_with(uri, g_prefix)) {
    return mem_empty;
  }
  uri = mem_consume(uri, g_prefix.size);

  const usize size = base64_decoded_size(uri);
  if (!size) {
    return mem_empty;
  }
  const Mem res = alloc_alloc(ld->transientAlloc, size, 16);
  if (!mem_valid(res)) {
    return mem_empty; // Transient allocator ran out of space. TODO: Report proper error.
  }
  DynString writer = dynstring_create_over(res);
  if (!base64_decode(&writer, uri)) {
    return mem_empty;
  }
  return res;
}

static void gltf_buffers_acquire(GltfLoad* ld, EcsWorld* w, AssetManagerComp* man, GltfError* err) {
  const JsonVal buffers = json_field_lit(ld->jDoc, ld->jRoot, "buffers");
  if (!(ld->bufferCount = gltf_json_elem_count(ld, buffers))) {
    goto Error;
  }
  ld->buffers = alloc_array_t(ld->transientAlloc, GltfBuffer, ld->bufferCount);
  mem_set(mem_create(ld->buffers, sizeof(GltfBuffer) * ld->bufferCount), 0);
  GltfBuffer* out = ld->buffers;

  json_for_elems(ld->jDoc, buffers, bufferElem) {
    if (!gltf_json_field_u32(ld, bufferElem, string_lit("byteLength"), &out->length)) {
      goto Error;
    }
    String uri;
    if (gltf_json_field_str(ld, bufferElem, string_lit("uri"), &uri)) {
      if (string_starts_with(uri, string_lit("data:"))) {
        /**
         * Data URI.
         */
        const Mem data = gtlf_uri_data_resolve(ld, uri);
        if (data.size < out->length) {
          goto Error; // Too little data contained in the data-uri.
        }
        out->entity = 0;
        out->data   = mem_slice(data, 0, out->length);
      } else {
        /**
         * External buffer.
         */
        if (uri.size > gltf_uri_size_max) {
          goto Error; // Buffer uri exceeds maximum.
        }
        const String assetId = gltf_buffer_asset_id(ld, uri);
        if (string_eq(assetId, ld->assetId)) {
          goto Error; // Cannot load this same file again as a buffer.
        }
        out->entity = asset_lookup(w, man, assetId);
        asset_acquire(w, out->entity);
      }
    } else {
      /**
       * Glb binary chunk.
       */
      if (ld->glbBinChunk.length < out->length) {
        goto Error; // Too little data in the glb binary chunk.
      }
      out->entity = 0;
      out->data   = mem_create(ld->glbBinChunk.dataPtr, out->length);
    }
    ++out;
  }
  *err = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedBuffers;
}

static void gltf_parse_views(GltfLoad* ld, GltfError* err) {
  const JsonVal views = json_field_lit(ld->jDoc, ld->jRoot, "bufferViews");
  if (!(ld->viewCount = gltf_json_elem_count(ld, views))) {
    goto Error;
  }
  ld->views     = alloc_array_t(ld->transientAlloc, GltfView, ld->viewCount);
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
  const JsonVal accessors = json_field_lit(ld->jDoc, ld->jRoot, "accessors");
  if (!(ld->accessCount = gltf_json_elem_count(ld, accessors))) {
    goto Error;
  }
  ld->access      = alloc_array_t(ld->transientAlloc, GltfAccess, ld->accessCount);
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
  const JsonVal meshes = json_field_lit(ld->jDoc, ld->jRoot, "meshes");
  if (!gltf_json_elem_count(ld, meshes)) {
    goto Error;
  }
  const JsonVal mesh = json_elem_begin(ld->jDoc, meshes);
  if (json_type(ld->jDoc, mesh) != JsonType_Object) {
    goto Error;
  }
  const JsonVal primitives = json_field_lit(ld->jDoc, mesh, "primitives");
  if (!(ld->primCount = gltf_json_elem_count(ld, primitives))) {
    goto Error;
  }
  ld->prims     = alloc_array_t(ld->transientAlloc, GltfPrim, ld->primCount);
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
    const JsonVal attributes = json_field_lit(ld->jDoc, primitive, "attributes");
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

static void gltf_parse_scene_transform(GltfLoad* ld, GltfError* err) {
  ld->sceneTrans.t = geo_vector(0);
  ld->sceneTrans.r = geo_quat_ident;
  ld->sceneTrans.s = geo_vector(1, 1, 1);

  const JsonVal scenes = json_field_lit(ld->jDoc, ld->jRoot, "scenes");
  if (!gltf_json_elem_count(ld, scenes)) {
    goto Success; // Scene transform is optional.
  }
  const JsonVal scene = json_elem_begin(ld->jDoc, scenes);
  if (!gltf_json_check(ld, scene, JsonType_Object)) {
    goto Error;
  }
  const JsonVal rootNodes = json_field_lit(ld->jDoc, scene, "nodes");
  u32           rootNodeIndex;
  if (!gltf_json_elem_u32(ld, rootNodes, 0, &rootNodeIndex)) {
    goto Success; // Scene transform is optional.
  }
  const JsonVal nodes = json_field_lit(ld->jDoc, ld->jRoot, "nodes");
  if (gltf_json_elem_count(ld, nodes) <= rootNodeIndex) {
    goto Error;
  }
  const JsonVal rootNode = json_elem(ld->jDoc, nodes, rootNodeIndex);
  gltf_json_transform(ld, rootNode, &ld->sceneTrans);

Success:
  // Mirror z to convert from a right-handed coordinate system.
  ld->sceneTrans.r = geo_quat_mul_comps(ld->sceneTrans.r, geo_vector(-1, -1, -1, 1));
  ld->sceneTrans.s = geo_vector_mul_comps(ld->sceneTrans.s, geo_vector(1, 1, -1));
  *err             = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedSceneTransform;
}

static void gltf_parse_skin(GltfLoad* ld, GltfError* err) {
  /**
   * NOTE: This loader only supports a single skin.
   */
  const JsonVal skins = json_field_lit(ld->jDoc, ld->jRoot, "skins");
  if (!gltf_json_elem_count(ld, skins)) {
    goto Success; // Skinning is optional.
  }
  const JsonVal skin = json_elem_begin(ld->jDoc, skins);
  if (json_type(ld->jDoc, skin) != JsonType_Object) {
    goto Error;
  }
  if (!gltf_json_field_u32(ld, skin, string_lit("inverseBindMatrices"), &ld->accBindInvMats)) {
    goto Error;
  }
  const JsonVal joints = json_field_lit(ld->jDoc, skin, "joints");
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
  ld->joints = alloc_array_t(ld->transientAlloc, GltfJoint, ld->jointCount);

  GltfJoint* outJoint = ld->joints;
  json_for_elems(ld->jDoc, joints, joint) {
    if (UNLIKELY(json_type(ld->jDoc, joint) != JsonType_Number)) {
      goto Error;
    }
    *outJoint++ = (GltfJoint){.nodeIndex = (u32)json_number(ld->jDoc, joint)};
  }

Success:
  *err = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedSkin;
}

static bool gltf_skeleton_is_topologically_sorted(GltfLoad* ld) {
  if (!ld->jointCount) {
    return true;
  }
  u8 processed[bits_to_bytes(asset_mesh_joints_max) + 1] = {0};
  for (u32 jointIndex = 0; jointIndex != ld->jointCount; ++jointIndex) {
    bitset_set(bitset_from_var(processed), jointIndex);
    if (!bitset_test(bitset_from_var(processed), ld->joints[jointIndex].parentIndex)) {
      return false;
    }
  }
  return true;
}

static void gltf_parse_skeleton_nodes(GltfLoad* ld, GltfError* err) {
  const JsonVal nodes = json_field_lit(ld->jDoc, ld->jRoot, "nodes");
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

    gltf_json_name(ld, node, &out->name);
    gltf_json_transform(ld, node, &out->defaultTrans);

    const JsonVal children = json_field_lit(ld->jDoc, node, "children");
    if (gltf_json_check(ld, children, JsonType_Array)) {

      json_for_elems(ld->jDoc, children, child) {
        if (UNLIKELY(json_type(ld->jDoc, child) != JsonType_Number)) {
          goto Error;
        }
        const u32 childJointIndex = gltf_node_to_joint_index(ld, (u32)json_number(ld->jDoc, child));
        if (!sentinel_check(childJointIndex)) {
          // Child is part of the skeleton: Set this joint as its parent.
          ld->joints[childJointIndex].parentIndex = jointIndex;
        }
      }
    }

  Next:
    ++nodeIndex;
  }

  // Verify that the joint parents appear earlier then their children.
  if (!gltf_skeleton_is_topologically_sorted(ld)) {
    goto Error;
  }

  *err = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedNodes;
}

static void gltf_parse_bind_matrices(GltfLoad* ld, GltfError* err) {
  if (!ld->jointCount) {
    return;
  }
  if (!gltf_access_check(ld, ld->accBindInvMats, GltfType_f32, 16)) {
    *err = GltfError_MalformedBindMatrix;
    return;
  }
  if (ld->access[ld->accBindInvMats].count < ld->jointCount) {
    *err = GltfError_MalformedBindMatrix;
    return;
  }
  const f32* bindInvMatData = ld->access[ld->accBindInvMats].data_f32;
  for (u32 jointIndex = 0; jointIndex != ld->jointCount; ++jointIndex) {
    GltfJoint* joint = &ld->joints[jointIndex];

    // Copy the raw gltf inverse bind matrix.
    mem_cpy(array_mem(joint->bindMatInv.comps), mem_create(bindInvMatData + jointIndex * 16, 64));

    /**
     * Gltf also uses column-major 4x4 f32 matrices, the only post-processing needed is converting
     * from a right-handed to a left-handed coordinate system.
     */
    static const GeoMatrix g_negZMat = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, -1, 0}, {0, 0, 0, 1}}};
    joint->bindMatInv                = geo_matrix_mul(&joint->bindMatInv, &g_negZMat);

    // TODO: Add error when the matrix is non invertible?
    joint->bindMat = geo_matrix_inverse(&joint->bindMatInv);
  }
}

static bool gltf_anim_target(const String str, AssetMeshAnimTarget* out) {
  static const String g_names[] = {
      string_static("translation"),
      string_static("rotation"),
      string_static("scale"),
  };
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
  const JsonVal animations = json_field_lit(ld->jDoc, ld->jRoot, "animations");
  if (!(ld->animCount = gltf_json_elem_count(ld, animations))) {
    goto Success; // Animations are optional.
  }
  if (ld->animCount > asset_mesh_anims_max) {
    *err = GltfError_AnimCountExceedsMaximum;
    return;
  }
  ld->anims         = alloc_array_t(ld->transientAlloc, GltfAnim, ld->animCount);
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
    gltf_json_name(ld, anim, &outAnim->name);

    const JsonVal samplers = json_field_lit(ld->jDoc, anim, "samplers");
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
      const JsonVal interpolation = json_field_lit(ld->jDoc, sampler, "interpolation");
      if (!gltf_json_check(ld, interpolation, JsonType_String)) {
        continue; // 'interpolation' is optional, default is 'LINEAR'.
      }
      if (!string_eq(json_string(ld->jDoc, interpolation), string_lit("LINEAR"))) {
        *err = GltfError_UnsupportedInterpolationMode;
        return;
      }
    }

    const JsonVal channels = json_field_lit(ld->jDoc, anim, "channels");
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

      const JsonVal target = json_field_lit(ld->jDoc, channel, "target");
      if (!gltf_json_check(ld, target, JsonType_Object)) {
        goto Error;
      }
      if (!gltf_json_field_u32(ld, target, string_lit("node"), &nodeIdx)) {
        goto Error;
      }
      if (sentinel_check(jointIdx = gltf_node_to_joint_index(ld, nodeIdx))) {
        continue; // Channel animates a node that is not part of the skeleton.
      }
      const JsonVal path = json_field_lit(ld->jDoc, target, "path");
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

typedef enum {
  GltfIndexMode_None,
  GltfIndexMode_u16,
  GltfIndexMode_u32,
} GltfIndexMode;

static bool gtlf_check_index_mode(GltfLoad* ld, const GltfPrim* prim, GltfIndexMode* out) {
  if (sentinel_check(prim->accIndices)) {
    *out = GltfIndexMode_None;
    return true;
  }
  if (gltf_access_check(ld, prim->accIndices, GltfType_u16, 1)) {
    *out = GltfIndexMode_u16;
    return true;
  }
  if (gltf_access_check(ld, prim->accIndices, GltfType_u32, 1)) {
    *out = GltfIndexMode_u32;
    return true;
  }
  return false;
}

static GltfMeshMeta gltf_mesh_meta(GltfLoad* ld, GltfError* err) {
  // clang-format off
#define verify(_EXPR_, _ERR_) if (UNLIKELY(!(_EXPR_))) { *err = GltfError_##_ERR_; goto Error; }
  // clang-format on

  verify(ld->primCount, NoPrimitives);

  GltfFeature features    = ~0; // Assume we have all features until accessors are missing.
  u32         vertexCount = 0;
  for (GltfPrim* prim = ld->prims; prim != ld->prims + ld->primCount; ++prim) {
    verify(prim->mode == GltfPrimMode_Triangles, UnsupportedPrimitiveMode);
    verify(gltf_access_check(ld, prim->accPosition, GltfType_f32, 3), MalformedPrimPositions);

    GltfIndexMode indexMode;
    verify(gtlf_check_index_mode(ld, prim, &indexMode), MalformedPrimIndices);

    const u32 attrCount = ld->access[prim->accPosition].count;
    if (indexMode == GltfIndexMode_None) {
      // Non-indexed primitive.
      verify((attrCount % 3) == 0, MalformedPrimPositions);
      vertexCount += attrCount;
    } else {
      // Indexed primitive.
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
      const bool validJoints = gltf_access_check(ld, prim->accJoints, GltfType_u8, 4) ||
                               gltf_access_check(ld, prim->accJoints, GltfType_u16, 4);
      verify(validJoints, MalformedPrimJoints);
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

static void gltf_vertex_skin(
    GltfLoad* ld, const GltfPrim* prim, const u32 attr, AssetMeshSkin* out, GltfError* err) {
  /**
   * Retrieve the 4 joint influences (joint-index + weight) for a vertex.
   */
  for (u32 i = 0; i != 4; ++i) {
    const f32 weight = ld->access[prim->accWeights].data_f32[attr * 4 + i];
    if (weight < gltf_skin_weight_min) {
      out->weights.comps[i] = 0;
      out->joints[i]        = 0;
      continue; // Joint unused in skin.
    }
    out->weights.comps[i] = weight;
    switch (ld->access[prim->accJoints].compType) {
    case GltfType_u8:
      out->joints[i] = ld->access[prim->accJoints].data_u8[attr * 4 + i];
      break;
    case GltfType_u16:
      out->joints[i] = (u8)ld->access[prim->accJoints].data_u16[attr * 4 + i];
      break;
    default:
      diag_crash();
    }
    if (UNLIKELY(out->joints[i] >= ld->jointCount)) {
      *err = GltfError_MalformedPrimJoints;
      return;
    }
  }
  *err = GltfError_None;
}

/**
 * Update joint meta-data for the given skinned vertex.
 */
static void
gltf_track_skinned_vertex(GltfLoad* ld, const AssetMeshVertex* vertex, const AssetMeshSkin* skin) {
  for (u32 i = 0; i != 4; ++i) {
    const f32 jointWeight = skin->weights.comps[i];
    const u8  jointIndex  = skin->joints[i];
    if (jointWeight < gltf_skin_weight_min) {
      continue; // Joint unused in skin.
    }
    GltfJoint* joint = &ld->joints[jointIndex];

    const GeoVector jointPos = geo_matrix_to_translation(&joint->bindMat);
    const GeoVector toVert   = geo_vector_sub(vertex->position, jointPos);
    const f32       dist     = geo_vector_mag(toVert);

    ++joint->skinCount;
    joint->boundingRadius = math_max(joint->boundingRadius, dist);
  }
}

static void gltf_build_mesh(
    GltfLoad* ld, const AssetImportMesh* importData, AssetMeshComp* out, GltfError* err) {
  GltfMeshMeta meta = gltf_mesh_meta(ld, err);
  if (*err) {
    return;
  }
  AssetMeshBuilder* builder = asset_mesh_builder_create(g_allocHeap, meta.vertexCount);

  const GeoMatrix vertexImportTrans = geo_matrix_trs(
      importData->vertexTranslation, importData->vertexRotation, importData->vertexScale);

  typedef const f32* AccessorF32;
  AccessorF32        positions, texcoords, normals, tangents;
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
    GltfIndexMode indexMode;
    gtlf_check_index_mode(ld, prim, &indexMode);

    vertexCount = indexMode == GltfIndexMode_None ? attrCount : ld->access[prim->accIndices].count;
    for (u32 i = 0; i != vertexCount; ++i) {
      u32 attr;
      switch (indexMode) {
      case GltfIndexMode_None:
        attr = i;
        break;
      case GltfIndexMode_u16:
        attr = ld->access[prim->accIndices].data_u16[i];
        break;
      case GltfIndexMode_u32:
        attr = ld->access[prim->accIndices].data_u32[i];
        break;
      }
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
      AssetMeshVertex vertex = {
          .position = geo_vector(vertPos[0], vertPos[1], vertPos[2] * -1.0f),
          .normal   = geo_vector(vertNrm[0], vertNrm[1], vertNrm[2] * -1.0f),
          .tangent  = geo_vector(vertTan[0], vertTan[1], vertTan[2] * -1.0f, vertTan[3]),
          .texcoord = geo_vector(vertTex[0], 1.0f - vertTex[1]),
      };
      asset_mesh_vertex_transform(&vertex, &vertexImportTrans);
      asset_mesh_vertex_quantize(&vertex);

      const AssetMeshIndex vertexIdx = asset_mesh_builder_push(builder, &vertex);

      if (meta.features & GltfFeature_Skinning) {
        AssetMeshSkin skin;
        gltf_vertex_skin(ld, prim, attr, &skin, err);
        if (*err) {
          goto Cleanup;
        }
        asset_mesh_builder_set_skin(builder, vertexIdx, skin);
        gltf_track_skinned_vertex(ld, &vertex, &skin);
      }
    }
  }
  if (!(meta.features & GltfFeature_Normals) || importData->flatNormals) {
    asset_mesh_compute_flat_normals(builder);
  }
  if (!(meta.features & GltfFeature_Tangents) || importData->flatNormals) {
    asset_mesh_compute_tangents(builder);
  }
  *out = asset_mesh_create(builder);
  *err = GltfError_None;

Cleanup:
  asset_mesh_builder_destroy(builder);
}

static f32 gltf_anim_duration(GltfLoad* ld, const GltfAnim* anim) {
  f32 duration = 0.0f;
  for (u32 jointIndex = 0; jointIndex != ld->jointCount; ++jointIndex) {
    for (AssetMeshAnimTarget target = 0; target != AssetMeshAnimTarget_Count; ++target) {
      const GltfAnimChannel* channel = &anim->channels[jointIndex][target];
      if (sentinel_check(channel->accInput)) {
        continue; // Channel is not animated.
      }
      if (!gltf_access_check(ld, channel->accInput, GltfType_f32, 1)) {
        continue; // Input is of incorrect type; import will fail during skeleton building.
      }
      const f32 channelDur = gltf_access_max_f32(ld, channel->accInput);
      duration             = math_max(duration, channelDur);
    }
  }
  return duration;
}

static void gltf_process_remove_frame(GltfLoad* ld, AssetMeshAnimChannel* ch, const u32 frame) {
  const usize toMove = --ch->frameCount - frame;
  if (toMove) {
    // Move time data.
    {
      const usize size = sizeof(u16);
      const Mem   dst  = dynarray_at(&ld->animData, ch->timeData + frame * size, toMove * size);
      const Mem src = dynarray_at(&ld->animData, ch->timeData + (frame + 1) * size, toMove * size);
      mem_move(dst, src);
    }
    // Move value data.
    {
      const usize size = sizeof(GeoVector);
      const Mem   dst  = dynarray_at(&ld->animData, ch->valueData + frame * size, toMove * size);
      const Mem src = dynarray_at(&ld->animData, ch->valueData + (frame + 1) * size, toMove * size);
      mem_move(dst, src);
    }
  }
}

static void gltf_process_anim_channel(
    GltfLoad* ld, AssetMeshAnimChannel* ch, const AssetMeshAnimTarget target, const f32 duration) {

  typedef bool (*EqFunc)(GeoVector, GeoVector, f32);
  const EqFunc eq = target == AssetMeshAnimTarget_Rotation ? geo_vector_equal : geo_vector_equal3;
  const f32    eqThres = gltf_eq_threshold;

  static const f32 g_minTimeSec = 1.0f / 30.0f; // A single frame at 30hz.
  const f32 minTimeFrac = duration > f32_epsilon ? math_min(g_minTimeSec / duration, 1.0f) : 0.0f;
  const u16 minTimeFracU16 = math_max((u16)(u16_max * minTimeFrac), 1);

  GeoVector* vData = dynarray_at(&ld->animData, ch->valueData, sizeof(GeoVector)).ptr;
  u16*       tData = dynarray_at(&ld->animData, ch->timeData, sizeof(u16)).ptr;

  /**
   * If a channel consists of all identical frames we can skip the interpolation.
   * TODO: Instead of just truncating the frame count we should avoid including data for the removed
   * frames at all.
   */
  if (ch->frameCount > 1) {
    bool allEq = true;
    for (u32 i = 1; i != ch->frameCount; ++i) {
      if (!eq(vData[0], vData[i], gltf_eq_threshold)) {
        allEq = false;
        break;
      }
    }
    if (allEq) {
      ch->frameCount = 1;
    }
  }

  /**
   * Remove redundant frames:
   * - frames that have the same position/rotation/scale as the previous and the next.
   * - frames that are too short (less then a 30hz frame).
   */
  if (ch->frameCount >= 2 && eq(vData[0], vData[1], eqThres)) {
    gltf_process_remove_frame(ld, ch, 0);
  }
  if (ch->frameCount >= 2 && eq(vData[ch->frameCount - 1], vData[ch->frameCount - 2], eqThres)) {
    gltf_process_remove_frame(ld, ch, ch->frameCount - 1);
  }
  for (u32 i = 1; i < (ch->frameCount - 1); ++i) {
    if (eq(vData[i], vData[i - 1], eqThres) && eq(vData[i], vData[i + 1], eqThres)) {
      gltf_process_remove_frame(ld, ch, i);
      continue;
    }
    if ((tData[i] - tData[i - 1]) < minTimeFracU16 || (tData[i + 1] - tData[i]) < minTimeFracU16) {
      gltf_process_remove_frame(ld, ch, i);
      continue;
    }
  }
}

static void gltf_process_anim_channel_rot(GltfLoad* ld, const AssetMeshAnimChannel* ch) {
  GeoQuat* rotPoses = dynarray_at(&ld->animData, ch->valueData, sizeof(GeoQuat)).ptr;

  /**
   * Normalize all the quaternions and compensate for double-cover so they can be directly
   * interpolated.
   */

  for (u32 i = 0; i != ch->frameCount; ++i) {
    rotPoses[i] = geo_quat_norm_or_ident(rotPoses[i]);
    if (i && geo_quat_dot(rotPoses[i], rotPoses[i - 1]) < 0) {
      // Compensate for quaternion double-cover (two quaternions representing the same rotation).
      rotPoses[i] = geo_quat_flip(rotPoses[i]);
    }
  }
}

static bool gtlf_process_any_joint_scaled(GltfLoad* ld, const AssetMeshAnim* anims) {
  static const GeoVector g_one = {.x = 1, .y = 1, .z = 1};

  for (u32 animIndex = 0; animIndex != ld->animCount; ++animIndex) {
    for (u32 jointIndex = 0; jointIndex != ld->jointCount; ++jointIndex) {
      const AssetMeshAnimTarget   tgt = AssetMeshAnimTarget_Scale;
      const AssetMeshAnimChannel* ch  = &anims[animIndex].joints[jointIndex][tgt];
      const GeoVector* data = dynarray_at(&ld->animData, ch->valueData, sizeof(GeoVector)).ptr;
      for (u32 frame = 0; frame != ch->frameCount; ++frame) {
        if (!geo_vector_equal3(data[frame], g_one, gltf_eq_threshold)) {
          return true;
        }
      }
    }
  }
  return false;
}

static void gltf_build_skeleton(
    GltfLoad* ld, const AssetImportMesh* importData, AssetMeshSkeletonComp* out, GltfError* err) {
  diag_assert(ld->jointCount);

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

  // Output the joint parent indices.
  AssetMeshDataPtr resParents = gltf_data_begin(ld, alignof(u32));
  for (u32 jointIndex = 0; jointIndex != ld->jointCount; ++jointIndex) {
    gltf_data_push_u32(ld, ld->joints[jointIndex].parentIndex);
  }

  // Output the skinned-vertex counts per joint.
  AssetMeshDataPtr resSkinCounts = gltf_data_begin(ld, alignof(u32));
  for (u32 jointIndex = 0; jointIndex != ld->jointCount; ++jointIndex) {
    gltf_data_push_u32(ld, ld->joints[jointIndex].skinCount);
  }

  // Output the bounding radius per joint.
  AssetMeshDataPtr resBoundingRadius = gltf_data_begin(ld, alignof(f32));
  for (u32 jointIndex = 0; jointIndex != ld->jointCount; ++jointIndex) {
    gltf_data_push_f32(ld, ld->joints[jointIndex].boundingRadius);
  }

  // Output the joint name-hashes.
  AssetMeshDataPtr resNameHashes = gltf_data_begin(ld, alignof(StringHash));
  for (u32 jointIndex = 0; jointIndex != ld->jointCount; ++jointIndex) {
    const StringHash importedJointNameHash = importData->joints[jointIndex].nameHash;
    gltf_data_push_u32(ld, importedJointNameHash);
  }

  // Output the joint names.
  AssetMeshDataPtr resNames = gltf_data_begin(ld, alignof(u8));
  for (u32 jointIndex = 0; jointIndex != ld->jointCount; ++jointIndex) {
    const StringHash importedJointNameHash = importData->joints[jointIndex].nameHash;
    gltf_data_push_string(ld, stringtable_lookup(g_stringtable, importedJointNameHash));
  }

  // Create the animation output structures.
  AssetMeshAnim* resAnims =
      ld->animCount ? alloc_array_t(g_allocHeap, AssetMeshAnim, ld->animCount) : null;

  if (resAnims) {
    // Zero init to avoid having garbage in the unused joint slots.
    mem_set(mem_create(resAnims, sizeof(AssetMeshAnim) * ld->animCount), 0);
  }

  for (u32 i = 0; i != importData->animCount; ++i) {
    AssetMeshAnim*         resAnim    = &resAnims[i];
    const AssetImportAnim* importAnim = &importData->anims[i];
    const GltfAnim*        anim       = &ld->anims[importAnim->index];

    resAnim->name = stringtable_lookup(g_stringtable, importAnim->nameHash);

    const f32 durationOrg = anim->duration;

    for (u32 jointIndex = 0; jointIndex != ld->jointCount; ++jointIndex) {
      bool anyTargetAnimated = false;
      for (AssetMeshAnimTarget target = 0; target != AssetMeshAnimTarget_Count; ++target) {
        const GltfAnimChannel* srcChannel = &anim->channels[jointIndex][target];
        AssetMeshAnimChannel*  resChannel = &resAnim->joints[jointIndex][target];

        if (!sentinel_check(srcChannel->accInput) && importAnim->mask[jointIndex] > f32_epsilon) {
          *resChannel = (AssetMeshAnimChannel){
              .frameCount = ld->access[srcChannel->accInput].count,
              .timeData   = gltf_data_push_access_norm16(ld, srcChannel->accInput, durationOrg),
              .valueData  = gltf_data_push_access_vec(ld, srcChannel->accOutput),
          };
          if (target == AssetMeshAnimTarget_Rotation) {
            gltf_process_anim_channel_rot(ld, resChannel);
          }
          gltf_process_anim_channel(ld, resChannel, target, durationOrg);
          anyTargetAnimated |= resChannel->frameCount > 0;
        } else {
          *resChannel = (AssetMeshAnimChannel){0};
        }
      }
      if (anyTargetAnimated) {
        resAnim->mask[jointIndex] = math_clamp_f32(importAnim->mask[jointIndex], 0.0f, 1.0f);
      }
    }
    resAnim->flags    = importAnim->flags;
    resAnim->duration = importAnim->duration;
    resAnim->time     = math_clamp_f32(importAnim->time, 0.0f, importAnim->duration);
    resAnim->speedMin = math_max(importAnim->speed - importAnim->speedVariance * 0.5f, 0.0f);
    resAnim->speedMax = importAnim->speed + importAnim->speedVariance * 0.5f;
    resAnim->weight   = importAnim->weight;
  }

  // Remove all scale channels if all of the channels use the identity scale.
  // TODO: Instead of truncating the frameCount to zero we should skip the all the channel data.
  if (!gtlf_process_any_joint_scaled(ld, resAnims)) {
    for (u32 animIndex = 0; animIndex != ld->animCount; ++animIndex) {
      for (u32 jointIndex = 0; jointIndex != ld->jointCount; ++jointIndex) {
        resAnims[animIndex].joints[jointIndex][AssetMeshAnimTarget_Scale].frameCount = 0;
      }
    }
  }

  // Create the default pose output.
  AssetMeshDataPtr resDefaultPose = gltf_data_begin(ld, alignof(GeoVector));
  for (const GltfJoint* joint = ld->joints; joint != ld->joints + ld->jointCount; ++joint) {
    gltf_data_push_trans(ld, joint->defaultTrans);
  }

  // Create the bind matrix output.
  AssetMeshDataPtr resBindMatInv = gltf_data_begin(ld, alignof(GeoMatrix));
  for (const GltfJoint* joint = ld->joints; joint != ld->joints + ld->jointCount; ++joint) {
    gltf_data_push_matrix(ld, joint->bindMatInv);
  }

  // Create the root-transform output.
  const GltfTransform rootTrans = {
      .t = importData->rootTranslation,
      .r = importData->rootRotation,
      .s = importData->rootScale,
  };
  AssetMeshDataPtr resRootTransform = gltf_data_push_trans(ld, rootTrans);

  // Pad animData so the size is always a multiple of 16.
  mem_set(dynarray_push(&ld->animData, bits_padding(ld->animData.size, 16)), 0);

  *out = (AssetMeshSkeletonComp){
      .anims.values    = resAnims,
      .anims.count     = ld->animCount,
      .bindMatInv      = resBindMatInv,
      .defaultPose     = resDefaultPose,
      .rootTransform   = resRootTransform,
      .parentIndices   = resParents,
      .skinCounts      = resSkinCounts,
      .boundingRadius  = resBoundingRadius,
      .jointNameHashes = resNameHashes,
      .jointNames      = resNames,
      .jointCount      = ld->jointCount,
      .data            = data_mem_create(
          alloc_dup(g_allocHeap, dynarray_at(&ld->animData, 0, ld->animData.size), 16)),
  };
  *err = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedAnimation;
}

static bool gltf_import(const AssetImportEnvComp* importEnv, GltfLoad* ld, AssetImportMesh* out) {
  diag_assert(ld->jointCount <= asset_mesh_joints_max);

  out->flatNormals = false;

  out->vertexTranslation = geo_vector(0);
  out->vertexRotation    = geo_quat_ident;
  out->vertexScale       = geo_vector(1.0f, 1.0f, 1.0f);

  out->rootTranslation = ld->sceneTrans.t;
  out->rootRotation    = ld->sceneTrans.r;
  out->rootScale       = ld->sceneTrans.s;

  out->jointCount = ld->jointCount;
  for (u32 jointIndex = 0; jointIndex != ld->jointCount; ++jointIndex) {
    diag_assert(!string_is_empty(ld->joints[jointIndex].name));
    out->joints[jointIndex].nameHash    = string_hash(ld->joints[jointIndex].name);
    out->joints[jointIndex].parentIndex = ld->joints[jointIndex].parentIndex;
  }

  out->animCount = ld->animCount;
  for (u32 animIndex = 0; animIndex != ld->animCount; ++animIndex) {
    GltfAnim* anim = &ld->anims[animIndex];

    out->anims[animIndex].index = animIndex;
    out->anims[animIndex].layer = (i32)animIndex;
    out->anims[animIndex].flags = 0;

    diag_assert(!string_is_empty(anim->name));
    out->anims[animIndex].nameHash = string_hash(anim->name);

    anim->duration                 = gltf_anim_duration(ld, anim);
    out->anims[animIndex].duration = anim->duration;

    out->anims[animIndex].time          = 0.0f;
    out->anims[animIndex].speed         = 1.0f;
    out->anims[animIndex].speedVariance = 0.0f;
    out->anims[animIndex].weight        = 1.0f;

    for (u32 jointIndex = 0; jointIndex != asset_mesh_joints_max; ++jointIndex) {
      const bool slotUsed                    = jointIndex < ld->jointCount;
      out->anims[animIndex].mask[jointIndex] = slotUsed ? 1.0f : 0.0f;
    }
  }

  return asset_import_mesh(importEnv, ld->assetId, out);
}

ecs_view_define(LoadGlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_read(AssetImportEnvComp);
}

ecs_view_define(LoadView) {
  ecs_access_write(AssetGltfLoadComp);
  ecs_access_read(AssetComp);
}

ecs_view_define(BufferView) { ecs_access_read(AssetRawComp); }

/**
 * Update all active loads.
 */
ecs_system_define(GltfLoadAssetSys) {
  EcsView*     globalView = ecs_world_view_t(world, LoadGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not initialized.
  }
  AssetManagerComp*         manager   = ecs_view_write_t(globalItr, AssetManagerComp);
  const AssetImportEnvComp* importEnv = ecs_view_read_t(globalItr, AssetImportEnvComp);

  EcsView*     loadView  = ecs_world_view_t(world, LoadView);
  EcsIterator* bufferItr = ecs_view_itr(ecs_world_view_t(world, BufferView));

  AssetImportMesh importData;

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
        if (!buffer->entity) {
          continue; // Internal buffer (glb binary chunk).
        }
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
      gltf_parse_scene_transform(ld, &err);
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
      gltf_parse_bind_matrices(ld, &err);
      if (err) {
        goto Error;
      }
      gltf_parse_animations(ld, &err);
      if (err) {
        goto Error;
      }
      if (!gltf_import(importEnv, ld, &importData)) {
        err = GltfError_ImportFailed;
        goto Error;
      }

#ifdef VOLO_TRACE
      const String traceMsg = path_filename(asset_id(ecs_view_read_t(itr, AssetComp)));
#endif
      trace_begin_msg("asset_gltf_build", TraceColor_Blue, "{}", fmt_text(traceMsg));

      AssetMeshBundle meshBundle;
      gltf_build_mesh(ld, &importData, &meshBundle.mesh, &err);

      trace_end();
      if (err) {
        goto Error;
      }
      *ecs_world_add_t(world, entity, AssetMeshComp) = meshBundle.mesh;
      if (ld->jointCount) {
        AssetMeshSkeletonComp resultSkeleton;
        gltf_build_skeleton(ld, &importData, &resultSkeleton, &err);
        if (err) {
          goto Error;
        }
        meshBundle.skeleton  = ecs_world_add_t(world, entity, AssetMeshSkeletonComp);
        *meshBundle.skeleton = resultSkeleton;
      } else {
        meshBundle.skeleton = null;
      }

      asset_mark_load_success(world, entity);

      asset_cache(world, entity, g_assetMeshBundleMeta, mem_var(meshBundle));
      goto Cleanup;
    }
    }

  Error:
    asset_mark_load_failure(world, entity, ld->assetId, gltf_error_str(err), (i32)err);

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

ecs_module_init(asset_mesh_gltf_module) {
  ecs_register_comp(AssetGltfLoadComp, .destructor = ecs_destruct_gltf_load_comp);

  ecs_register_view(LoadGlobalView);
  ecs_register_view(LoadView);
  ecs_register_view(BufferView);

  ecs_register_system(
      GltfLoadAssetSys,
      ecs_view_id(LoadGlobalView),
      ecs_view_id(LoadView),
      ecs_view_id(BufferView));
}

static GltfLoad* gltf_load(
    EcsWorld*                 w,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         e,
    const Mem                 data) {
  (void)importEnv;

  JsonDoc*   jsonDoc = json_create(g_allocHeap, 512);
  JsonResult jsonRes;
  json_read(jsonDoc, data, JsonReadFlags_HashOnlyFieldNames, &jsonRes);

  if (UNLIKELY(jsonRes.type != JsonResultType_Success)) {
    asset_mark_load_failure(w, e, id, json_error_str(jsonRes.error), (i32)GltfError_InvalidJson);
    json_destroy(jsonDoc);
    return null;
  }

  if (UNLIKELY(json_type(jsonDoc, jsonRes.type) != JsonType_Object)) {
    const String err = gltf_error_str(GltfError_MalformedFile);
    asset_mark_load_failure(w, e, id, err, (i32)GltfError_MalformedFile);
    json_destroy(jsonDoc);
    return null;
  }

  Allocator* transientAlloc =
      alloc_chunked_create(g_allocHeap, alloc_bump_create, gltf_transient_alloc_chunk_size);

  return ecs_world_add_t(
      w,
      e,
      AssetGltfLoadComp,
      .transientAlloc = transientAlloc,
      .assetId        = id,
      .jDoc           = jsonDoc,
      .jRoot          = jsonRes.val,
      .accBindInvMats = sentinel_u32,
      .animData       = dynarray_create(g_allocHeap, 1, 1, 0));
}

void asset_load_mesh_gltf(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {

  gltf_load(world, importEnv, id, entity, src->data);
  asset_repo_close(src);
}

static Mem glb_read_header(Mem data, GlbHeader* out, GltfError* err) {
  if (UNLIKELY(data.size < sizeof(u32) * 3)) {
    *err = GltfError_MalformedGlbHeader;
    return data;
  }
  u32 magic;
  data = mem_consume_le_u32(data, &magic);
  if (UNLIKELY(magic != 0x46546C67 /* ascii: 'glTF' */)) {
    *err = GltfError_MalformedGlbHeader;
    return data;
  }
  data = mem_consume_le_u32(data, &out->version);
  data = mem_consume_le_u32(data, &out->length);
  return data;
}

static Mem glb_read_chunk(Mem data, GlbChunk* out, GltfError* err) {
  if (UNLIKELY(data.size < sizeof(u32) * 2)) {
    *err = GltfError_MalformedGlbChunk;
    return data;
  }
  data = mem_consume_le_u32(data, &out->length);
  data = mem_consume_le_u32(data, &out->type);
  if (UNLIKELY(data.size < out->length)) {
    *err = GltfError_MalformedGlbChunk;
    return data;
  }
  if (UNLIKELY(!bits_aligned(out->length, 4))) {
    *err = GltfError_MalformedGlbChunk;
    return data;
  }
  out->dataPtr = data.ptr;
  return mem_consume(data, out->length);
}

void asset_load_mesh_glb(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;

  GltfError err = GltfError_None;

  GlbHeader header;
  Mem       data = glb_read_header(src->data, &header, &err);
  if (UNLIKELY(err)) {
    asset_mark_load_failure(world, entity, id, gltf_error_str(err), (i32)err);
    goto Failed;
  }
  if (UNLIKELY(header.version != 2)) {
    err = GltfError_UnsupportedGlbVersion;
    asset_mark_load_failure(world, entity, id, gltf_error_str(err), (i32)err);
    goto Failed;
  }
  if (UNLIKELY(header.length != src->data.size)) {
    err = GltfError_MalformedFile;
    asset_mark_load_failure(world, entity, id, gltf_error_str(err), (i32)err);
    goto Failed;
  }

  GlbChunk chunks[glb_chunk_count_max];
  u32      chunkCount = 0;
  while (data.size) {
    if (UNLIKELY(chunkCount == glb_chunk_count_max)) {
      err = GltfError_GlbChunkCountExceedsMaximum;
      asset_mark_load_failure(world, entity, id, gltf_error_str(err), (i32)err);
      goto Failed;
    }
    data = glb_read_chunk(data, &chunks[chunkCount++], &err);
    if (UNLIKELY(err)) {
      asset_mark_load_failure(world, entity, id, gltf_error_str(err), (i32)err);
      goto Failed;
    }
  }

  if (UNLIKELY(!chunkCount || chunks[0].type != GlbChunkType_Json)) {
    err = GltfError_GlbJsonChunkMissing;
    asset_mark_load_failure(world, entity, id, gltf_error_str(err), (i32)err);
    goto Failed;
  }

  const Mem gltfData = mem_create(chunks[0].dataPtr, chunks[0].length);
  GltfLoad* ld       = gltf_load(world, importEnv, id, entity, gltfData);
  if (UNLIKELY(!ld)) {
    goto Failed;
  }

  if (chunkCount > 1 && chunks[1].type == GlbChunkType_Bin) {
    ld->glbBinChunk   = chunks[1];
    ld->glbDataSource = src;
  } else {
    asset_repo_close(src);
  }
  return; // Success;

Failed:
  asset_repo_close(src);
}
