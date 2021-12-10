#include "asset_graphic.h"
#include "asset_shader.h"
#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_utils.h"
#include "ecs_world.h"

#include "repo_internal.h"

typedef struct {
  String             texture;
  EcsEntityId        textureAsset;
  AssetGraphicWrap   wrap;
  AssetGraphicFilter filter;
  AssetGraphicAniso  anisotropy;
} SamplerLoadData;

typedef struct {
  String      shader;
  EcsEntityId shaderAsset;
} ShaderLoadData;

typedef struct {
  struct {
    ShaderLoadData* values;
    usize           count;
  } shaders;
  struct {
    SamplerLoadData* values;
    usize            count;
  } samplers;
  AssetGraphicTopology   topology;
  AssetGraphicRasterizer rasterizer;
  u32                    lineWidth;
  AssetGraphicBlend      blend;
  AssetGraphicDepth      depth;
  AssetGraphicCull       cull;
} GraphicLoadData;

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
    data_reg_const_t(g_dataReg, AssetGraphicDepth, Always);

    data_reg_enum_t(g_dataReg, AssetGraphicCull);
    data_reg_const_t(g_dataReg, AssetGraphicCull, None);
    data_reg_const_t(g_dataReg, AssetGraphicCull, Back);
    data_reg_const_t(g_dataReg, AssetGraphicCull, Front);

    data_reg_struct_t(g_dataReg, SamplerLoadData);
    data_reg_field_t(g_dataReg, SamplerLoadData, texture, data_prim_t(String));
    data_reg_field_t(g_dataReg, SamplerLoadData, wrap, t_AssetGraphicWrap, .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, SamplerLoadData, filter, t_AssetGraphicFilter, .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, SamplerLoadData, anisotropy, t_AssetGraphicAniso, .flags = DataFlags_Opt);

    data_reg_struct_t(g_dataReg, ShaderLoadData);
    data_reg_field_t(g_dataReg, ShaderLoadData, shader, data_prim_t(String));

    data_reg_struct_t(g_dataReg, GraphicLoadData);
    data_reg_field_t(
        g_dataReg, GraphicLoadData, shaders, t_ShaderLoadData, .container = DataContainer_Array);
    data_reg_field_t(
        g_dataReg, GraphicLoadData, samplers, t_SamplerLoadData, .container = DataContainer_Array);
    data_reg_field_t(
        g_dataReg, GraphicLoadData, topology, t_AssetGraphicTopology, .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, GraphicLoadData, rasterizer, t_AssetGraphicRasterizer, .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, GraphicLoadData, lineWidth, data_prim_t(u32), .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, GraphicLoadData, blend, t_AssetGraphicBlend, .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, GraphicLoadData, depth, t_AssetGraphicDepth, .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, GraphicLoadData, cull, t_AssetGraphicCull, .flags = DataFlags_Opt);

    g_dataMeta = data_meta_t(t_GraphicLoadData);
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetGraphicComp);
ecs_comp_define(AssetGraphicLoadingComp) { GraphicLoadData data; };

static void ecs_destruct_graphic_comp(void* data) {
  AssetGraphicComp* comp = data;
  alloc_free_array_t(g_alloc_heap, comp->shaders, comp->shaderCount);
  alloc_free_array_t(g_alloc_heap, comp->samplers, comp->samplerCount);
}

static void ecs_destruct_graphic_loading_comp(void* data) {
  AssetGraphicLoadingComp* comp = data;
  data_destroy(g_dataReg, g_alloc_heap, g_dataMeta, mem_var(comp->data));
}

typedef enum {
  GraphicError_None = 0,
  GraphicError_MalformedJson,
  GraphicError_ExpectedShader,
  GraphicError_ExpectedTexture,

  GraphicError_Count,
} GraphicError;

typedef enum {
  GraphicLoad_Done,
  GraphicLoad_Busy,
} GraphicLoadProg;

static String graphic_error_str(GraphicError res) {
  static const String msgs[] = {
      string_static("None"),
      string_static("Malformed Json"),
      string_static("Expected a Shader asset"),
      string_static("Expected a Texture asset"),
  };
  ASSERT(array_elems(msgs) == GraphicError_Count, "Incorrect number of graphic-error messages");
  return msgs[res];
}

NORETURN static void graphic_report_error_msg(const GraphicError err, const String message) {
  (void)err;
  diag_crash_msg("Failed to parse graphic, error: {}", fmt_text(message));
}

NORETURN static void graphic_report_error(const GraphicError err) {
  graphic_report_error_msg(err, graphic_error_str(err));
}

static GraphicLoadProg graphic_load_asset(
    EcsWorld* world, AssetManagerComp* manager, const String id, EcsEntityId* asset) {
  if (*asset) {
    return ecs_world_has_t(world, *asset, AssetLoadedComp) ? GraphicLoad_Done : GraphicLoad_Busy;
  }
  *asset = asset_lookup(world, manager, id);
  asset_acquire(world, *asset);
  return GraphicLoad_Busy;
}

static GraphicLoadProg
graphic_load_shaders(EcsWorld* world, AssetManagerComp* manager, GraphicLoadData* data) {
  GraphicLoadProg prog = GraphicLoad_Done;
  for (usize i = 0; i != data->shaders.count; ++i) {
    ShaderLoadData* shaderData = &data->shaders.values[i];

    prog |= graphic_load_asset(world, manager, shaderData->shader, &shaderData->shaderAsset);
    if (!prog && !ecs_world_has_t(world, shaderData->shaderAsset, AssetShaderComp)) {
      graphic_report_error(GraphicError_ExpectedShader);
    }
  }
  return prog;
}

static GraphicLoadProg
graphic_load_samplers(EcsWorld* world, AssetManagerComp* manager, GraphicLoadData* data) {
  GraphicLoadProg prog = GraphicLoad_Done;
  for (usize i = 0; i != data->samplers.count; ++i) {
    SamplerLoadData* samplerData = &data->samplers.values[i];

    prog |= graphic_load_asset(world, manager, samplerData->texture, &samplerData->textureAsset);
    if (!prog && !ecs_world_has_t(world, samplerData->textureAsset, AssetTextureComp)) {
      graphic_report_error(GraphicError_ExpectedTexture);
    }
  }
  return prog;
}

static void graphic_comp_create(EcsWorld* world, EcsEntityId entity, GraphicLoadData* data) {
  AssetGraphicComp* comp = ecs_world_add_t(
      world,
      entity,
      AssetGraphicComp,
      .shaders      = alloc_array_t(g_alloc_heap, AssetGraphicShader, data->shaders.count),
      .shaderCount  = data->shaders.count,
      .samplers     = alloc_array_t(g_alloc_heap, AssetGraphicSampler, data->samplers.count),
      .samplerCount = data->samplers.count,
      .topology     = data->topology,
      .rasterizer   = data->rasterizer,
      .lineWidth    = data->lineWidth,
      .blend        = data->blend,
      .depth        = data->depth,
      .cull         = data->cull);

  for (usize i = 0; i != data->shaders.count; i++) {
    comp->shaders[i] = (AssetGraphicShader){.shader = data->shaders.values[i].shaderAsset};
  }

  for (usize i = 0; i != data->samplers.count; i++) {
    comp->samplers[i] = (AssetGraphicSampler){
        .texture    = data->samplers.values[i].textureAsset,
        .wrap       = data->samplers.values[i].wrap,
        .filter     = data->samplers.values[i].filter,
        .anisotropy = data->samplers.values[i].anisotropy,
    };
  }
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); };
ecs_view_define(LoadView) { ecs_access_write(AssetGraphicLoadingComp); };

ecs_view_define(UnloadView) {
  ecs_access_read(AssetGraphicComp);
  ecs_access_without(AssetLoadedComp);
};

/**
 * Create graphic-asset components for loading graphics.
 */
ecs_system_define(LoadGraphicAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }
  EcsView* LoadView = ecs_world_view_t(world, LoadView);
  for (EcsIterator* itr = ecs_view_itr(LoadView); ecs_view_walk(itr);) {
    const EcsEntityId entity   = ecs_view_entity(itr);
    GraphicLoadData*  loadData = &ecs_view_write_t(itr, AssetGraphicLoadingComp)->data;

    GraphicLoadProg prog = GraphicLoad_Done;
    prog |= graphic_load_shaders(world, manager, loadData);
    prog |= graphic_load_samplers(world, manager, loadData);

    if (prog == GraphicLoad_Done) {
      ecs_world_remove_t(world, entity, AssetGraphicLoadingComp);
      graphic_comp_create(world, entity, loadData);
      ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    }
  }
}

/**
 * Remove any graphic-asset components for unloaded assets.
 */
ecs_system_define(UnloadGraphicAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId       entity = ecs_view_entity(itr);
    const AssetGraphicComp* asset  = ecs_view_read_t(itr, AssetGraphicComp);
    ecs_world_remove_t(world, entity, AssetGraphicComp);

    // Release the shader assets.
    for (usize i = 0; i != asset->shaderCount; ++i) {
      asset_release(world, asset->shaders[i].shader);
    }
    // Release the texture assets.
    for (usize i = 0; i != asset->samplerCount; ++i) {
      asset_release(world, asset->samplers[i].texture);
    }
  }
}

ecs_module_init(asset_graphic_module) {
  graphic_datareg_init();

  ecs_register_comp(AssetGraphicComp, .destructor = ecs_destruct_graphic_comp);
  ecs_register_comp(AssetGraphicLoadingComp, .destructor = ecs_destruct_graphic_loading_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(UnloadView);

  ecs_register_system(LoadGraphicAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView));
  ecs_register_system(UnloadGraphicAssetSys, ecs_view_id(UnloadView));
}

void asset_load_gfx(EcsWorld* world, EcsEntityId assetEntity, AssetSource* src) {
  GraphicLoadData loadData;
  DataReadResult  readResult;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataMeta, mem_var(loadData), &readResult);
  if (readResult.error) {
    graphic_report_error_msg(GraphicError_MalformedJson, readResult.errorMsg);
  }
  asset_source_close(src);
  ecs_world_add_t(world, assetEntity, AssetGraphicLoadingComp, .data = loadData);
}
