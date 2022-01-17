#include "asset_graphic.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataMeta;

static void graphic_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    data_reg_enum_t(g_dataReg, AssetGraphicTopology);
    data_reg_const_t(g_dataReg, AssetGraphicTopology, Triangles);
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
    data_reg_const_t(g_dataReg, AssetGraphicBlend, Additive);
    data_reg_const_t(g_dataReg, AssetGraphicBlend, AlphaAdditive);

    data_reg_enum_t(g_dataReg, AssetGraphicWrap);
    data_reg_const_t(g_dataReg, AssetGraphicWrap, Repeat);
    data_reg_const_t(g_dataReg, AssetGraphicWrap, Clamp);

    data_reg_enum_t(g_dataReg, AssetGraphicFilter);
    data_reg_const_t(g_dataReg, AssetGraphicFilter, Nearest);
    data_reg_const_t(g_dataReg, AssetGraphicFilter, Linear);

    data_reg_enum_t(g_dataReg, AssetGraphicAniso);
    data_reg_const_t(g_dataReg, AssetGraphicAniso, None);
    data_reg_const_t(g_dataReg, AssetGraphicAniso, x2);
    data_reg_const_t(g_dataReg, AssetGraphicAniso, x4);
    data_reg_const_t(g_dataReg, AssetGraphicAniso, x8);
    data_reg_const_t(g_dataReg, AssetGraphicAniso, x16);

    data_reg_enum_t(g_dataReg, AssetGraphicDepth);
    data_reg_const_t(g_dataReg, AssetGraphicDepth, None);
    data_reg_const_t(g_dataReg, AssetGraphicDepth, Less);
    data_reg_const_t(g_dataReg, AssetGraphicDepth, LessOrEqual);
    data_reg_const_t(g_dataReg, AssetGraphicDepth, Always);

    data_reg_enum_t(g_dataReg, AssetGraphicCull);
    data_reg_const_t(g_dataReg, AssetGraphicCull, None);
    data_reg_const_t(g_dataReg, AssetGraphicCull, Back);
    data_reg_const_t(g_dataReg, AssetGraphicCull, Front);

    data_reg_struct_t(g_dataReg, AssetGraphicOverride);
    data_reg_field_t(g_dataReg, AssetGraphicOverride, name, data_prim_t(String));
    data_reg_field_t(g_dataReg, AssetGraphicOverride, binding, data_prim_t(u32));
    data_reg_field_t(g_dataReg, AssetGraphicOverride, value, data_prim_t(f64));

    data_reg_struct_t(g_dataReg, AssetGraphicShader);
    data_reg_field_t(g_dataReg, AssetGraphicShader, shaderId, data_prim_t(String));
    data_reg_field_t(
        g_dataReg,
        AssetGraphicShader,
        overrides,
        t_AssetGraphicOverride,
        .container = DataContainer_Array,
        .flags     = DataFlags_Opt);

    data_reg_struct_t(g_dataReg, AssetGraphicSampler);
    data_reg_field_t(g_dataReg, AssetGraphicSampler, textureId, data_prim_t(String));
    data_reg_field_t(
        g_dataReg, AssetGraphicSampler, wrap, t_AssetGraphicWrap, .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, AssetGraphicSampler, filter, t_AssetGraphicFilter, .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, AssetGraphicSampler, anisotropy, t_AssetGraphicAniso, .flags = DataFlags_Opt);

    data_reg_struct_t(g_dataReg, AssetGraphicComp);
    data_reg_field_t(
        g_dataReg,
        AssetGraphicComp,
        shaders,
        t_AssetGraphicShader,
        .container = DataContainer_Array);
    data_reg_field_t(
        g_dataReg,
        AssetGraphicComp,
        samplers,
        t_AssetGraphicSampler,
        .container = DataContainer_Array,
        .flags     = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, AssetGraphicComp, meshId, data_prim_t(String), .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, AssetGraphicComp, vertexCount, data_prim_t(u32), .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, AssetGraphicComp, renderOrder, data_prim_t(i32), .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, AssetGraphicComp, topology, t_AssetGraphicTopology, .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, AssetGraphicComp, rasterizer, t_AssetGraphicRasterizer, .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, AssetGraphicComp, lineWidth, data_prim_t(u32), .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, AssetGraphicComp, blend, t_AssetGraphicBlend, .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, AssetGraphicComp, depth, t_AssetGraphicDepth, .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AssetGraphicComp, cull, t_AssetGraphicCull, .flags = DataFlags_Opt);

    g_dataMeta = data_meta_t(t_AssetGraphicComp);
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetGraphicComp);
ecs_comp_define(AssetGraphicLoadComp) { AssetSource* src; };

static void ecs_destruct_graphic_comp(void* data) {
  AssetGraphicComp* comp = data;
  data_destroy(g_dataReg, g_alloc_heap, g_dataMeta, mem_create(comp, sizeof(AssetGraphicComp)));
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
        g_alloc_heap,
        g_dataMeta,
        mem_create(graphicComp, sizeof(AssetGraphicComp)),
        &result);
    if (result.error) {
      graphic_load_fail(world, entity, result.errorMsg);
      goto Error;
    }

    // Resolve shader references.
    array_ptr_for_t(graphicComp->shaders, AssetGraphicShader, ptr) {
      if (string_is_empty(ptr->shaderId)) {
        graphic_load_fail(world, entity, string_lit("Missing shader asset"));
        goto Error;
      }
      ptr->shader = asset_lookup(world, manager, ptr->shaderId);
    }

    // Resolve texture references.
    array_ptr_for_t(graphicComp->samplers, AssetGraphicSampler, ptr) {
      if (string_is_empty(ptr->textureId)) {
        graphic_load_fail(world, entity, string_lit("Missing texture asset"));
        goto Error;
      }
      ptr->texture = asset_lookup(world, manager, ptr->textureId);
    }

    // Resolve mesh reference.
    if (!string_is_empty(graphicComp->meshId) && graphicComp->vertexCount) {
      graphic_load_fail(world, entity, string_lit("'meshId' can't be combined with 'vertexCount'"));
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
  graphic_datareg_init();

  ecs_register_comp(AssetGraphicComp, .destructor = ecs_destruct_graphic_comp);
  ecs_register_comp(AssetGraphicLoadComp, .destructor = ecs_destruct_graphic_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(UnloadView);

  ecs_register_system(LoadGraphicAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView));
  ecs_register_system(UnloadGraphicAssetSys, ecs_view_id(UnloadView));
}

void asset_load_gra(EcsWorld* world, const EcsEntityId entity, AssetSource* src) {
  ecs_world_add_t(world, entity, AssetGraphicLoadComp, .src = src);
}
