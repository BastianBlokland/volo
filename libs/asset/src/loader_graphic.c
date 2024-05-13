#include "asset_graphic.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"
#include "data.h"
#include "data_schema.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataMeta;

static void graphic_datareg_init(void) {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    DataReg* reg = data_reg_create(g_allocPersist);

    // clang-format off
    data_reg_enum_t(reg, AssetGraphicTopology);
    data_reg_const_t(reg, AssetGraphicTopology, Triangles);
    data_reg_const_t(reg, AssetGraphicTopology, TriangleStrip);
    data_reg_const_t(reg, AssetGraphicTopology, TriangleFan);
    data_reg_const_t(reg, AssetGraphicTopology, Lines);
    data_reg_const_t(reg, AssetGraphicTopology, LineStrip);
    data_reg_const_t(reg, AssetGraphicTopology, Points);

    data_reg_enum_t(reg, AssetGraphicRasterizer);
    data_reg_const_t(reg, AssetGraphicRasterizer, Fill);
    data_reg_const_t(reg, AssetGraphicRasterizer, Lines);
    data_reg_const_t(reg, AssetGraphicRasterizer, Points);

    data_reg_enum_t(reg, AssetGraphicBlend);
    data_reg_const_t(reg, AssetGraphicBlend, None);
    data_reg_const_t(reg, AssetGraphicBlend, Alpha);
    data_reg_const_t(reg, AssetGraphicBlend, AlphaConstant);
    data_reg_const_t(reg, AssetGraphicBlend, Additive);
    data_reg_const_t(reg, AssetGraphicBlend, PreMultiplied);

    data_reg_enum_t(reg, AssetGraphicWrap);
    data_reg_const_t(reg, AssetGraphicWrap, Clamp);
    data_reg_const_t(reg, AssetGraphicWrap, Repeat);
    data_reg_const_t(reg, AssetGraphicWrap, Zero);

    data_reg_enum_t(reg, AssetGraphicFilter);
    data_reg_const_t(reg, AssetGraphicFilter, Linear);
    data_reg_const_t(reg, AssetGraphicFilter, Nearest);

    data_reg_enum_t(reg, AssetGraphicAniso);
    data_reg_const_t(reg, AssetGraphicAniso, None);
    data_reg_const_t(reg, AssetGraphicAniso, x2);
    data_reg_const_t(reg, AssetGraphicAniso, x4);
    data_reg_const_t(reg, AssetGraphicAniso, x8);
    data_reg_const_t(reg, AssetGraphicAniso, x16);

    data_reg_enum_t(reg, AssetGraphicDepth);
    data_reg_const_t(reg, AssetGraphicDepth, Less);
    data_reg_const_t(reg, AssetGraphicDepth, LessOrEqual);
    data_reg_const_t(reg, AssetGraphicDepth, Equal);
    data_reg_const_t(reg, AssetGraphicDepth, Greater);
    data_reg_const_t(reg, AssetGraphicDepth, GreaterOrEqual);
    data_reg_const_t(reg, AssetGraphicDepth, Always);
    data_reg_const_t(reg, AssetGraphicDepth, LessNoWrite);
    data_reg_const_t(reg, AssetGraphicDepth, LessOrEqualNoWrite);
    data_reg_const_t(reg, AssetGraphicDepth, EqualNoWrite);
    data_reg_const_t(reg, AssetGraphicDepth, GreaterNoWrite);
    data_reg_const_t(reg, AssetGraphicDepth, GreaterOrEqualNoWrite);
    data_reg_const_t(reg, AssetGraphicDepth, AlwaysNoWrite);

    data_reg_enum_t(reg, AssetGraphicCull);
    data_reg_const_t(reg, AssetGraphicCull, None);
    data_reg_const_t(reg, AssetGraphicCull, Back);
    data_reg_const_t(reg, AssetGraphicCull, Front);

    data_reg_struct_t(reg, AssetGraphicOverride);
    data_reg_field_t(reg, AssetGraphicOverride, name, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetGraphicOverride, binding, data_prim_t(u8));
    data_reg_field_t(reg, AssetGraphicOverride, value, data_prim_t(f64));

    data_reg_struct_t(reg, AssetGraphicShader);
    data_reg_field_t(reg, AssetGraphicShader, shaderId, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetGraphicShader, overrides, t_AssetGraphicOverride, .container = DataContainer_Array, .flags = DataFlags_Opt);

    data_reg_struct_t(reg, AssetGraphicSampler);
    data_reg_field_t(reg, AssetGraphicSampler, textureId, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetGraphicSampler, wrap, t_AssetGraphicWrap, .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetGraphicSampler, filter, t_AssetGraphicFilter, .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetGraphicSampler, anisotropy, t_AssetGraphicAniso, .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetGraphicSampler, mipBlending, data_prim_t(bool), .flags = DataFlags_Opt);

    data_reg_struct_t(reg, AssetGraphicComp);
    data_reg_field_t(reg, AssetGraphicComp, shaders, t_AssetGraphicShader, .container = DataContainer_Array, .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetGraphicComp, samplers, t_AssetGraphicSampler, .container = DataContainer_Array, .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetGraphicComp, meshId, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetGraphicComp, vertexCount, data_prim_t(u32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetGraphicComp, renderOrder, data_prim_t(i32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetGraphicComp, topology, t_AssetGraphicTopology, .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetGraphicComp, rasterizer, t_AssetGraphicRasterizer, .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetGraphicComp, lineWidth, data_prim_t(u16), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetGraphicComp, depthClamp, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetGraphicComp, depthBiasConstant, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetGraphicComp, depthBiasSlope, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetGraphicComp, blend, t_AssetGraphicBlend, .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetGraphicComp, blendAux, t_AssetGraphicBlend, .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetGraphicComp, blendConstant, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetGraphicComp, depth, t_AssetGraphicDepth, .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetGraphicComp, cull, t_AssetGraphicCull, .flags = DataFlags_Opt);
    // clang-format on

    g_dataMeta = data_meta_t(t_AssetGraphicComp);
    g_dataReg  = reg;
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetGraphicComp);
ecs_comp_define(AssetGraphicLoadComp) { AssetSource* src; };

static void ecs_destruct_graphic_comp(void* data) {
  AssetGraphicComp* comp = data;
  data_destroy(g_dataReg, g_allocHeap, g_dataMeta, mem_create(comp, sizeof(AssetGraphicComp)));
}

static void ecs_destruct_graphic_load_comp(void* data) {
  AssetGraphicLoadComp* comp = data;
  asset_repo_source_close(comp->src);
}

static void graphic_load_fail(EcsWorld* world, const EcsEntityId entity, const String msg) {
  log_e("Failed to parse graphic", log_param("error", fmt_text(msg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(LoadView) { ecs_access_read(AssetGraphicLoadComp); }

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
    const AssetSource* src         = ecs_view_read_t(itr, AssetGraphicLoadComp)->src;
    AssetGraphicComp*  graphicComp = ecs_world_add_t(world, entity, AssetGraphicComp);

    DataReadResult result;
    data_read_json(
        g_dataReg,
        src->data,
        g_allocHeap,
        g_dataMeta,
        mem_create(graphicComp, sizeof(AssetGraphicComp)),
        &result);
    if (result.error) {
      graphic_load_fail(world, entity, result.errorMsg);
      goto Error;
    }

    // Resolve shader references.
    array_ptr_for_t(graphicComp->shaders, AssetGraphicShader, ptr) {
      ptr->shader = asset_lookup(world, manager, ptr->shaderId);
      asset_register_dep(world, entity, ptr->shader);
    }

    // Resolve texture references.
    array_ptr_for_t(graphicComp->samplers, AssetGraphicSampler, ptr) {
      ptr->texture = asset_lookup(world, manager, ptr->textureId);
      asset_register_dep(world, entity, ptr->texture);
    }

    // Resolve mesh reference.
    if (!string_is_empty(graphicComp->meshId) && graphicComp->vertexCount) {
      graphic_load_fail(world, entity, string_lit("'meshId' can't be combined with 'vertexCount'"));
      goto Error;
    }
    if (!string_is_empty(graphicComp->meshId)) {
      graphicComp->mesh = asset_lookup(world, manager, graphicComp->meshId);
      asset_register_dep(world, entity, graphicComp->mesh);
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
  graphic_datareg_init();

  ecs_register_comp(AssetGraphicComp, .destructor = ecs_destruct_graphic_comp);
  ecs_register_comp(AssetGraphicLoadComp, .destructor = ecs_destruct_graphic_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(UnloadView);

  ecs_register_system(LoadGraphicAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView));
  ecs_register_system(UnloadGraphicAssetSys, ecs_view_id(UnloadView));
}

void asset_load_graphic(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  ecs_world_add_t(world, entity, AssetGraphicLoadComp, .src = src);
}

void asset_graphic_jsonschema_write(DynString* str) {
  graphic_datareg_init();

  const DataJsonSchemaFlags schemaFlags = DataJsonSchemaFlags_Compact;
  data_jsonschema_write(g_dataReg, str, g_dataMeta, schemaFlags);
}
