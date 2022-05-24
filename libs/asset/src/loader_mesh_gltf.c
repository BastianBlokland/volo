#include "asset_mesh.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_path.h"
#include "ecs_world.h"
#include "json_read.h"
#include "log_logger.h"

#include "mesh_utils_internal.h"
#include "repo_internal.h"

/**
 * GLTF (GL Transmission Format) 2.0.
 * Format specification: https://www.khronos.org/registry/glTF/specs/2.0/glTF-2.0.html
 */

typedef enum {
  GltfLoadPhase_MetaData,
} GltfLoadPhase;

typedef struct {
  u64 versionMajor, versionMinor;
} GltfMetaData;

ecs_comp_define(AssetGltfLoadComp) {
  String        bufferLoadRoot;
  JsonDoc*      jsonDoc;
  JsonVal       jsonRoot;
  GltfLoadPhase phase;
};

static void ecs_destruct_gltf_load_comp(void* data) {
  AssetGltfLoadComp* comp = data;
  json_destroy(comp->jsonDoc);
}

typedef enum {
  GltfError_None = 0,
  GltfError_InvalidJson,
  GltfError_MalformedStructure,
  GltfError_MalformedAsset,
  GltfError_MissingVersion,
  GltfError_UnsupportedVersion,

  GltfError_Count,
} GltfError;

static String gltf_error_str(const GltfError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Invalid json"),
      string_static("Malformed gltf structure"),
      string_static("Gltf asset metadata malformed"),
      string_static("Gltf version specification missing"),
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

static void gltf_parse_metadata(AssetGltfLoadComp* load, GltfMetaData* outMeta, GltfError* err) {
  const JsonVal assetField = json_field(load->jsonDoc, load->jsonRoot, string_lit("asset"));
  if (sentinel_check(assetField) || json_type(load->jsonDoc, assetField) != JsonType_Object) {
    *err = GltfError_MalformedAsset;
    return;
  }
  const JsonVal versionField = json_field(load->jsonDoc, assetField, string_lit("version"));
  if (sentinel_check(versionField) || json_type(load->jsonDoc, versionField) != JsonType_String) {
    *err = GltfError_MissingVersion;
    return;
  }
  *outMeta = (GltfMetaData){0};

  String versionString = json_string(load->jsonDoc, versionField);
  versionString        = format_read_u64(versionString, &outMeta->versionMajor, 10);
  if (!string_is_empty(versionString) && *string_begin(versionString) == '.') {
    format_read_u64(string_consume(versionString, 1), &outMeta->versionMinor, 10);
  }
  *err = GltfError_None;
}

ecs_view_define(LoadView) { ecs_access_write(AssetGltfLoadComp); }

/**
 * Update all active loads.
 */
ecs_system_define(GltfLoadAssetSys) {
  EcsView* loadView = ecs_world_view_t(world, LoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId  entity = ecs_view_entity(itr);
    AssetGltfLoadComp* load   = ecs_view_write_t(itr, AssetGltfLoadComp);

    GltfError err = GltfError_None;

    switch (load->phase) {
    case GltfLoadPhase_MetaData: {
      GltfMetaData meta;
      gltf_parse_metadata(load, &meta, &err);
      if (err) {
        goto Error;
      }
      if (meta.versionMajor != 2 && meta.versionMinor != 0) {
        err = GltfError_UnsupportedVersion;
        goto Error;
      }
      ++load->phase;
      // Fallthrough.
    } break;
    }

  Error:
    gltf_load_fail(world, entity, err);
    ecs_world_remove_t(world, entity, AssetGltfLoadComp);
  }
}

ecs_module_init(asset_gltf_module) {
  ecs_register_comp(AssetGltfLoadComp, .destructor = ecs_destruct_gltf_load_comp);

  ecs_register_view(LoadView);

  ecs_register_system(GltfLoadAssetSys, ecs_view_id(LoadView));
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
    gltf_load_fail(world, entity, GltfError_MalformedStructure);
    json_destroy(jsonDoc);
    return;
  }

  ecs_world_add_t(
      world,
      entity,
      AssetGltfLoadComp,
      .bufferLoadRoot = path_parent(id),
      .jsonDoc        = jsonDoc,
      .jsonRoot       = jsonRes.type);
}
