#include "asset_graphic.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "data.h"
#include "data_schema.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

DataMeta g_assetGraphicDataDef;

ecs_comp_define_public(AssetGraphicComp);
ecs_comp_define(AssetGraphicLoadComp) { AssetSource* src; };

static void ecs_destruct_graphic_comp(void* data) {
  AssetGraphicComp* comp = data;
  data_destroy(
      g_dataReg, g_allocHeap, g_assetGraphicDataDef, mem_create(comp, sizeof(AssetGraphicComp)));
}

static void ecs_destruct_graphic_load_comp(void* data) {
  AssetGraphicLoadComp* comp = data;
  asset_repo_source_close(comp->src);
}

static void graphic_load_fail(
    EcsWorld* world, const EcsEntityId entity, const String id, const String message) {
  log_e(
      "Failed to parse graphic",
      log_param("id", fmt_text(id)),
      log_param("error", fmt_text(message)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }

ecs_view_define(LoadView) {
  ecs_access_read(AssetComp);
  ecs_access_read(AssetGraphicLoadComp);
}

ecs_view_define(UnloadView) {
  ecs_access_read(AssetGraphicComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Load graphic-assets.
 */
ecs_system_define(LoadGraphicAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }
  EcsView* loadView = ecs_world_view_t(world, LoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId  entity      = ecs_view_entity(itr);
    const String       id          = asset_id(ecs_view_read_t(itr, AssetComp));
    const AssetSource* src         = ecs_view_read_t(itr, AssetGraphicLoadComp)->src;
    AssetGraphicComp*  graphicComp = ecs_world_add_t(world, entity, AssetGraphicComp);

    DataReadResult result;
    data_read_json(
        g_dataReg,
        src->data,
        g_allocHeap,
        g_assetGraphicDataDef,
        mem_create(graphicComp, sizeof(AssetGraphicComp)),
        &result);
    if (result.error) {
      graphic_load_fail(world, entity, id, result.errorMsg);
      goto Error;
    }

    // Resolve shader references.
    array_ptr_for_t(graphicComp->shaders, AssetGraphicShader, ptr) {
      ptr->shader = asset_lookup(world, manager, ptr->shaderId);
    }

    // Resolve texture references.
    array_ptr_for_t(graphicComp->samplers, AssetGraphicSampler, ptr) {
      ptr->texture = asset_lookup(world, manager, ptr->textureId);
    }

    // Resolve mesh reference.
    if (!string_is_empty(graphicComp->meshId) && graphicComp->vertexCount) {
      graphic_load_fail(
          world, entity, id, string_lit("'meshId' can't be combined with 'vertexCount'"));
      goto Error;
    }
    if (!string_is_empty(graphicComp->meshId)) {
      graphicComp->mesh = asset_lookup(world, manager, graphicComp->meshId);
    }

    ecs_world_remove_t(world, entity, AssetGraphicLoadComp);
    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    continue;

  Error:
    // NOTE: 'AssetGraphicComp' will be cleaned up by 'UnloadGraphicAssetSys'.
    ecs_world_remove_t(world, entity, AssetGraphicLoadComp);
  }
}

/**
 * Remove any graphic-asset components for unloaded assets.
 */
ecs_system_define(UnloadGraphicAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetGraphicComp);
  }
}

ecs_module_init(asset_graphic_module) {
  ecs_register_comp(AssetGraphicComp, .destructor = ecs_destruct_graphic_comp);
  ecs_register_comp(AssetGraphicLoadComp, .destructor = ecs_destruct_graphic_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(UnloadView);

  ecs_register_system(LoadGraphicAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView));
  ecs_register_system(UnloadGraphicAssetSys, ecs_view_id(UnloadView));
}

void asset_data_init_graphic(void) {
  // clang-format off
  data_reg_enum_t(g_dataReg, AssetGraphicTopology);
  data_reg_const_t(g_dataReg, AssetGraphicTopology, Triangles);
  data_reg_const_t(g_dataReg, AssetGraphicTopology, TriangleStrip);
  data_reg_const_t(g_dataReg, AssetGraphicTopology, TriangleFan);
  data_reg_const_t(g_dataReg, AssetGraphicTopology, Lines);
  data_reg_const_t(g_dataReg, AssetGraphicTopology, LineStrip);
  data_reg_const_t(g_dataReg, AssetGraphicTopology, Points);

  data_reg_enum_t(g_dataReg, AssetGraphicRasterizer);
  data_reg_const_t(g_dataReg, AssetGraphicRasterizer, Fill);
  data_reg_const_t(g_dataReg, AssetGraphicRasterizer, Lines);
  data_reg_const_t(g_dataReg, AssetGraphicRasterizer, Points);

  data_reg_enum_t(g_dataReg, AssetGraphicBlend);
  data_reg_const_t(g_dataReg, AssetGraphicBlend, None);
  data_reg_const_t(g_dataReg, AssetGraphicBlend, Alpha);
  data_reg_const_t(g_dataReg, AssetGraphicBlend, AlphaConstant);
  data_reg_const_t(g_dataReg, AssetGraphicBlend, Additive);
  data_reg_const_t(g_dataReg, AssetGraphicBlend, PreMultiplied);

  data_reg_enum_t(g_dataReg, AssetGraphicWrap);
  data_reg_const_t(g_dataReg, AssetGraphicWrap, Clamp);
  data_reg_const_t(g_dataReg, AssetGraphicWrap, Repeat);
  data_reg_const_t(g_dataReg, AssetGraphicWrap, Zero);

  data_reg_enum_t(g_dataReg, AssetGraphicFilter);
  data_reg_const_t(g_dataReg, AssetGraphicFilter, Linear);
  data_reg_const_t(g_dataReg, AssetGraphicFilter, Nearest);

  data_reg_enum_t(g_dataReg, AssetGraphicAniso);
  data_reg_const_t(g_dataReg, AssetGraphicAniso, None);
  data_reg_const_t(g_dataReg, AssetGraphicAniso, x2);
  data_reg_const_t(g_dataReg, AssetGraphicAniso, x4);
  data_reg_const_t(g_dataReg, AssetGraphicAniso, x8);
  data_reg_const_t(g_dataReg, AssetGraphicAniso, x16);

  data_reg_enum_t(g_dataReg, AssetGraphicDepth);
  data_reg_const_t(g_dataReg, AssetGraphicDepth, Less);
  data_reg_const_t(g_dataReg, AssetGraphicDepth, LessOrEqual);
  data_reg_const_t(g_dataReg, AssetGraphicDepth, Equal);
  data_reg_const_t(g_dataReg, AssetGraphicDepth, Greater);
  data_reg_const_t(g_dataReg, AssetGraphicDepth, GreaterOrEqual);
  data_reg_const_t(g_dataReg, AssetGraphicDepth, Always);
  data_reg_const_t(g_dataReg, AssetGraphicDepth, LessNoWrite);
  data_reg_const_t(g_dataReg, AssetGraphicDepth, LessOrEqualNoWrite);
  data_reg_const_t(g_dataReg, AssetGraphicDepth, EqualNoWrite);
  data_reg_const_t(g_dataReg, AssetGraphicDepth, GreaterNoWrite);
  data_reg_const_t(g_dataReg, AssetGraphicDepth, GreaterOrEqualNoWrite);
  data_reg_const_t(g_dataReg, AssetGraphicDepth, AlwaysNoWrite);

  data_reg_enum_t(g_dataReg, AssetGraphicCull);
  data_reg_const_t(g_dataReg, AssetGraphicCull, None);
  data_reg_const_t(g_dataReg, AssetGraphicCull, Back);
  data_reg_const_t(g_dataReg, AssetGraphicCull, Front);

  data_reg_struct_t(g_dataReg, AssetGraphicOverride);
  data_reg_field_t(g_dataReg, AssetGraphicOverride, name, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetGraphicOverride, binding, data_prim_t(u8));
  data_reg_field_t(g_dataReg, AssetGraphicOverride, value, data_prim_t(f64));

  data_reg_struct_t(g_dataReg, AssetGraphicShader);
  data_reg_field_t(g_dataReg, AssetGraphicShader, shaderId, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetGraphicShader, overrides, t_AssetGraphicOverride, .container = DataContainer_Array, .flags = DataFlags_Opt);

  data_reg_struct_t(g_dataReg, AssetGraphicSampler);
  data_reg_field_t(g_dataReg, AssetGraphicSampler, textureId, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetGraphicSampler, wrap, t_AssetGraphicWrap, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetGraphicSampler, filter, t_AssetGraphicFilter, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetGraphicSampler, anisotropy, t_AssetGraphicAniso, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetGraphicSampler, mipBlending, data_prim_t(bool), .flags = DataFlags_Opt);

  data_reg_struct_t(g_dataReg, AssetGraphicComp);
  data_reg_field_t(g_dataReg, AssetGraphicComp, shaders, t_AssetGraphicShader, .container = DataContainer_Array, .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetGraphicComp, samplers, t_AssetGraphicSampler, .container = DataContainer_Array, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetGraphicComp, meshId, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetGraphicComp, vertexCount, data_prim_t(u32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetGraphicComp, renderOrder, data_prim_t(i32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetGraphicComp, topology, t_AssetGraphicTopology, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetGraphicComp, rasterizer, t_AssetGraphicRasterizer, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetGraphicComp, lineWidth, data_prim_t(u16), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetGraphicComp, depthClamp, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetGraphicComp, depthBiasConstant, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetGraphicComp, depthBiasSlope, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetGraphicComp, blend, t_AssetGraphicBlend, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetGraphicComp, blendAux, t_AssetGraphicBlend, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetGraphicComp, blendConstant, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetGraphicComp, depth, t_AssetGraphicDepth, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetGraphicComp, cull, t_AssetGraphicCull, .flags = DataFlags_Opt);
  // clang-format on

  g_assetGraphicDataDef = data_meta_t(t_AssetGraphicComp);
}

void asset_load_graphic(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  ecs_world_add_t(world, entity, AssetGraphicLoadComp, .src = src);
}

void asset_graphic_jsonschema_write(DynString* str) {
  const DataJsonSchemaFlags schemaFlags = DataJsonSchemaFlags_Compact;
  data_jsonschema_write(g_dataReg, str, g_assetGraphicDataDef, schemaFlags);
}
