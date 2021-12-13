#include "asset_graphic.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_utils.h"
#include "ecs_world.h"

#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataMeta;

static void gfx_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    data_reg_enum_t(g_dataReg, AssetGfxTopology);
    data_reg_const_t(g_dataReg, AssetGfxTopology, Triangles);
    data_reg_const_t(g_dataReg, AssetGfxTopology, Lines);
    data_reg_const_t(g_dataReg, AssetGfxTopology, LineStrip);

    data_reg_enum_t(g_dataReg, AssetGfxRasterizer);
    data_reg_const_t(g_dataReg, AssetGfxRasterizer, Fill);
    data_reg_const_t(g_dataReg, AssetGfxRasterizer, Lines);
    data_reg_const_t(g_dataReg, AssetGfxRasterizer, Points);

    data_reg_enum_t(g_dataReg, AssetGfxBlend);
    data_reg_const_t(g_dataReg, AssetGfxBlend, None);
    data_reg_const_t(g_dataReg, AssetGfxBlend, Alpha);
    data_reg_const_t(g_dataReg, AssetGfxBlend, Additive);
    data_reg_const_t(g_dataReg, AssetGfxBlend, AlphaAdditive);

    data_reg_enum_t(g_dataReg, AssetGfxWrap);
    data_reg_const_t(g_dataReg, AssetGfxWrap, Repeat);
    data_reg_const_t(g_dataReg, AssetGfxWrap, Clamp);

    data_reg_enum_t(g_dataReg, AssetGfxFilter);
    data_reg_const_t(g_dataReg, AssetGfxFilter, Nearest);
    data_reg_const_t(g_dataReg, AssetGfxFilter, Linear);

    data_reg_enum_t(g_dataReg, AssetGfxAniso);
    data_reg_const_t(g_dataReg, AssetGfxAniso, None);
    data_reg_const_t(g_dataReg, AssetGfxAniso, x2);
    data_reg_const_t(g_dataReg, AssetGfxAniso, x4);
    data_reg_const_t(g_dataReg, AssetGfxAniso, x8);
    data_reg_const_t(g_dataReg, AssetGfxAniso, x16);

    data_reg_enum_t(g_dataReg, AssetGfxDepth);
    data_reg_const_t(g_dataReg, AssetGfxDepth, None);
    data_reg_const_t(g_dataReg, AssetGfxDepth, Less);
    data_reg_const_t(g_dataReg, AssetGfxDepth, Always);

    data_reg_enum_t(g_dataReg, AssetGfxCull);
    data_reg_const_t(g_dataReg, AssetGfxCull, None);
    data_reg_const_t(g_dataReg, AssetGfxCull, Back);
    data_reg_const_t(g_dataReg, AssetGfxCull, Front);

    data_reg_struct_t(g_dataReg, AssetGfxSampler);
    data_reg_field_t(g_dataReg, AssetGfxSampler, textureId, data_prim_t(String));
    data_reg_field_t(g_dataReg, AssetGfxSampler, wrap, t_AssetGfxWrap, .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AssetGfxSampler, filter, t_AssetGfxFilter, .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, AssetGfxSampler, anisotropy, t_AssetGfxAniso, .flags = DataFlags_Opt);

    data_reg_struct_t(g_dataReg, AssetGfxShader);
    data_reg_field_t(g_dataReg, AssetGfxShader, shaderId, data_prim_t(String));

    data_reg_struct_t(g_dataReg, AssetGfxComp);
    data_reg_field_t(
        g_dataReg, AssetGfxComp, shaders, t_AssetGfxShader, .container = DataContainer_Array);
    data_reg_field_t(
        g_dataReg,
        AssetGfxComp,
        samplers,
        t_AssetGfxSampler,
        .container = DataContainer_Array,
        .flags     = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AssetGfxComp, meshId, data_prim_t(String));
    data_reg_field_t(g_dataReg, AssetGfxComp, topology, t_AssetGfxTopology, .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, AssetGfxComp, rasterizer, t_AssetGfxRasterizer, .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AssetGfxComp, lineWidth, data_prim_t(u32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AssetGfxComp, blend, t_AssetGfxBlend, .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AssetGfxComp, depth, t_AssetGfxDepth, .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AssetGfxComp, cull, t_AssetGfxCull, .flags = DataFlags_Opt);

    g_dataMeta = data_meta_t(t_AssetGfxComp);
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetGfxComp);
ecs_comp_define(AssetGfxLoadComp) { AssetSource* src; };

static void ecs_destruct_gfx_comp(void* data) {
  AssetGfxComp* comp = data;
  data_destroy(g_dataReg, g_alloc_heap, g_dataMeta, mem_create(comp, sizeof(AssetGfxComp)));
}

static void ecs_destruct_gfx_load_comp(void* data) {
  AssetGfxLoadComp* comp = data;
  asset_source_close(comp->src);
}

NORETURN static void gfx_report_error(const String message) {
  diag_crash_msg("Failed to parse gfx, error: {}", fmt_text(message));
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); };
ecs_view_define(LoadView) { ecs_access_read(AssetGfxLoadComp); };

ecs_view_define(UnloadView) {
  ecs_access_read(AssetGfxComp);
  ecs_access_without(AssetLoadedComp);
};

/**
 * Load gfx-assets.
 */
ecs_system_define(LoadGfxAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }
  EcsView* LoadView = ecs_world_view_t(world, LoadView);
  for (EcsIterator* itr = ecs_view_itr(LoadView); ecs_view_walk(itr);) {
    const EcsEntityId  entity  = ecs_view_entity(itr);
    const AssetSource* src     = ecs_view_read_t(itr, AssetGfxLoadComp)->src;
    AssetGfxComp*      gfxComp = ecs_world_add_t(world, entity, AssetGfxComp);

    DataReadResult result;
    data_read_json(
        g_dataReg,
        src->data,
        g_alloc_heap,
        g_dataMeta,
        mem_create(gfxComp, sizeof(AssetGfxComp)),
        &result);
    if (result.error) {
      gfx_report_error(result.errorMsg);
    }

    // Resolve shader references.
    for (usize i = 0; i != gfxComp->shaders.count; ++i) {
      gfxComp->shaders.values[i].shader =
          asset_lookup(world, manager, gfxComp->shaders.values[i].shaderId);
    }

    // Resolve texture references.
    for (usize i = 0; i != gfxComp->samplers.count; ++i) {
      gfxComp->samplers.values[i].texture =
          asset_lookup(world, manager, gfxComp->samplers.values[i].textureId);
    }

    // Resolve mesh reference.
    gfxComp->mesh = asset_lookup(world, manager, gfxComp->meshId);

    ecs_world_remove_t(world, entity, AssetGfxLoadComp);
    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  }
}

/**
 * Remove any gfx-asset components for unloaded assets.
 */
ecs_system_define(UnloadGfxAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetGfxComp);
  }
}

ecs_module_init(asset_gfx_module) {
  gfx_datareg_init();

  ecs_register_comp(AssetGfxComp, .destructor = ecs_destruct_gfx_comp);
  ecs_register_comp(AssetGfxLoadComp, .destructor = ecs_destruct_gfx_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(UnloadView);

  ecs_register_system(LoadGfxAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView));
  ecs_register_system(UnloadGfxAssetSys, ecs_view_id(UnloadView));
}

void asset_load_gfx(EcsWorld* world, EcsEntityId assetEntity, AssetSource* src) {
  ecs_world_add_t(world, assetEntity, AssetGfxLoadComp, .src = src);
}
