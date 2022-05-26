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
 */

typedef enum {
  GltfLoadPhase_Meta,
  GltfLoadPhase_BuffersAcquire,
  GltfLoadPhase_BuffersWait,
  GltfLoadPhase_BufferViews,
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

ecs_comp_define(AssetGltfLoadComp) {
  String        assetId;
  GltfLoadPhase phase;
  JsonDoc*      jDoc;
  JsonVal       jRoot;
  GltfMeta      meta;
  DynArray      buffers;     // GltfBuffer[].
  DynArray      bufferViews; // GltfBufferView[].
};

static void ecs_destruct_gltf_load_comp(void* data) {
  AssetGltfLoadComp* comp = data;
  json_destroy(comp->jDoc);
  dynarray_destroy(&comp->buffers);
  dynarray_destroy(&comp->bufferViews);
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
  GltfError_MissingVersion,
  GltfError_InvalidBuffer,
  GltfError_UnsupportedExtensions,
  GltfError_UnsupportedVersion,

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
      string_static("Gltf version specification missing"),
      string_static("Gltf invalid buffer"),
      string_static("Gltf file requires an unsupported extension"),
      string_static("Unsupported gltf version"),
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

static bool gltf_check_val(AssetGltfLoadComp* load, const JsonVal jVal, const JsonType type) {
  return !sentinel_check(jVal) && json_type(load->jDoc, jVal) == type;
}

static bool
gltf_field_u32(AssetGltfLoadComp* load, const JsonVal jVal, const String name, u32* out) {
  const JsonVal jField = json_field(load->jDoc, jVal, name);
  if (!gltf_check_val(load, jField, JsonType_Number)) {
    return false;
  }
  *out = (u32)json_number(load->jDoc, jField);
  return true;
}

static bool
gltf_field_string(AssetGltfLoadComp* load, const JsonVal jVal, const String name, String* out) {
  const JsonVal jField = json_field(load->jDoc, jVal, name);
  if (!gltf_check_val(load, jField, JsonType_String)) {
    return false;
  }
  *out = json_string(load->jDoc, jField);
  return true;
}

static void gltf_parse_version(AssetGltfLoadComp* load, String str, GltfError* err) {
  str = format_read_u64(str, &load->meta.versionMajor, 10);
  if (string_is_empty(str)) {
    load->meta.versionMinor = 0;
    *err                    = GltfError_None;
    return;
  }
  if (*string_begin(str) != '.') {
    *err = GltfError_MalformedVersion;
    return;
  }
  format_read_u64(string_consume(str, 1), &load->meta.versionMinor, 10);
  *err = GltfError_None;
}

static void gltf_parse_meta(AssetGltfLoadComp* load, GltfError* err) {
  const JsonVal asset = json_field(load->jDoc, load->jRoot, string_lit("asset"));
  if (!gltf_check_val(load, asset, JsonType_Object)) {
    *err = GltfError_MalformedAsset;
    return;
  }
  String versionString;
  if (!gltf_field_string(load, asset, string_lit("version"), &versionString)) {
    *err = GltfError_MissingVersion;
    return;
  }
  gltf_parse_version(load, versionString, err);
  if (*err) {
    return;
  }
  if (load->meta.versionMajor != 2 && load->meta.versionMinor != 0) {
    *err = GltfError_UnsupportedVersion;
    return;
  }
  const JsonVal extensions = json_field(load->jDoc, load->jRoot, string_lit("extensionsRequired"));
  if (!sentinel_check(extensions)) {
    if (!gltf_check_val(load, extensions, JsonType_Array)) {
      *err = GltfError_MalformedRequiredExtensions;
      return;
    }
    // NOTE: No extensions are suppored at this time.
    if (json_elem_count(load->jDoc, extensions) != 0) {
      *err = GltfError_UnsupportedExtensions;
      return;
    }
  }
  *err = GltfError_None;
}

static String gltf_buffer_asset_id(AssetGltfLoadComp* load, const String uri) {
  const String root = path_parent(load->assetId);
  return fmt_write_scratch("{}/{}", fmt_text(root), fmt_text(uri));
}

static void gltf_buffers_acquire(
    AssetGltfLoadComp* load, EcsWorld* world, AssetManagerComp* manager, GltfError* err) {
  const JsonVal buffers = json_field(load->jDoc, load->jRoot, string_lit("buffers"));
  if (!gltf_check_val(load, buffers, JsonType_Array)) {
    goto Error;
  }
  json_for_elems(load->jDoc, buffers, bufferElem) {
    u32 byteLength;
    if (!gltf_field_u32(load, bufferElem, string_lit("byteLength"), &byteLength)) {
      goto Error;
    }
    String uri;
    if (!gltf_field_string(load, bufferElem, string_lit("uri"), &uri)) {
      goto Error;
    }
    const String      id     = gltf_buffer_asset_id(load, uri);
    const EcsEntityId entity = asset_lookup(world, manager, id);
    asset_acquire(world, entity);

    *dynarray_push_t(&load->buffers, GltfBuffer) = (GltfBuffer){
        .length = byteLength,
        .entity = entity,
    };
  }
  *err = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedBuffers;
}

static void gltf_parse_bufferviews(AssetGltfLoadComp* load, GltfError* err) {
  const JsonVal bufferViews = json_field(load->jDoc, load->jRoot, string_lit("bufferViews"));
  if (!gltf_check_val(load, bufferViews, JsonType_Array)) {
    goto Error;
  }
  json_for_elems(load->jDoc, bufferViews, bufferView) {
    u32 bufferIndex;
    if (!gltf_field_u32(load, bufferView, string_lit("buffer"), &bufferIndex)) {
      goto Error;
    }
    if (bufferIndex >= load->buffers.size) {
      goto Error;
    }
    u32 byteOffset;
    if (!gltf_field_u32(load, bufferView, string_lit("byteOffset"), &byteOffset)) {
      byteOffset = 0;
    }
    u32 byteLength;
    if (!gltf_field_u32(load, bufferView, string_lit("byteLength"), &byteLength)) {
      goto Error;
    }
    const String bufferData = dynarray_at_t(&load->buffers, bufferIndex, GltfBuffer)->data;
    if (byteOffset + byteLength > bufferData.size) {
      goto Error;
    }
    *dynarray_push_t(&load->bufferViews, GltfBufferView) = (GltfBufferView){
        .data = string_slice(bufferData, byteOffset, byteLength),
    };
  }
  *err = GltfError_None;
  return;

Error:
  *err = GltfError_MalformedBufferViews;
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
    AssetGltfLoadComp* load   = ecs_view_write_t(itr, AssetGltfLoadComp);

    GltfError err = GltfError_None;
    switch (load->phase) {
    case GltfLoadPhase_Meta: {
      gltf_parse_meta(load, &err);
      if (err) {
        goto Error;
      }
      ++load->phase;
      // Fallthrough.
    }
    case GltfLoadPhase_BuffersAcquire: {
      gltf_buffers_acquire(load, world, manager, &err);
      if (err) {
        goto Error;
      }
      ++load->phase;
      goto Next;
    }
    case GltfLoadPhase_BuffersWait: {
      dynarray_for_t(&load->buffers, GltfBuffer, buffer) {
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
      ++load->phase;
      // Fallthrough.
    }
    case GltfLoadPhase_BufferViews: {
      gltf_parse_bufferviews(load, &err);
      if (err) {
        goto Error;
      }
      ++load->phase;
      // Fallthrough.
    }
    }

  Error:
    gltf_load_fail(world, entity, err);
    dynarray_for_t(&load->buffers, GltfBuffer, buffer) { asset_release(world, buffer->entity); }
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
      .bufferViews = dynarray_create_t(g_alloc_heap, GltfBufferView, 8));
}
