#include "asset_material.h"
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
  String              texture;
  EcsEntityId         textureAsset;
  AssetMaterialWrap   wrap;
  AssetMaterialFilter filter;
  AssetMaterialAniso  anisotropy;
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
  AssetMaterialTopology   topology;
  AssetMaterialRasterizer rasterizer;
  u32                     lineWidth;
  AssetMaterialBlend      blend;
  AssetMaterialDepth      depth;
  AssetMaterialCull       cull;
} MaterialLoadData;

static DataReg* g_dataReg;
static DataMeta g_dataMeta;

static void mat_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    data_reg_enum_t(g_dataReg, AssetMaterialTopology);
    data_reg_const_t(g_dataReg, AssetMaterialTopology, Triangles);
    data_reg_const_t(g_dataReg, AssetMaterialTopology, Lines);
    data_reg_const_t(g_dataReg, AssetMaterialTopology, LineStrip);

    data_reg_enum_t(g_dataReg, AssetMaterialRasterizer);
    data_reg_const_t(g_dataReg, AssetMaterialRasterizer, Fill);
    data_reg_const_t(g_dataReg, AssetMaterialRasterizer, Lines);
    data_reg_const_t(g_dataReg, AssetMaterialRasterizer, Points);

    data_reg_enum_t(g_dataReg, AssetMaterialBlend);
    data_reg_const_t(g_dataReg, AssetMaterialBlend, None);
    data_reg_const_t(g_dataReg, AssetMaterialBlend, Alpha);
    data_reg_const_t(g_dataReg, AssetMaterialBlend, Additive);
    data_reg_const_t(g_dataReg, AssetMaterialBlend, AlphaAdditive);

    data_reg_enum_t(g_dataReg, AssetMaterialWrap);
    data_reg_const_t(g_dataReg, AssetMaterialWrap, Repeat);
    data_reg_const_t(g_dataReg, AssetMaterialWrap, Clamp);

    data_reg_enum_t(g_dataReg, AssetMaterialFilter);
    data_reg_const_t(g_dataReg, AssetMaterialFilter, Nearest);
    data_reg_const_t(g_dataReg, AssetMaterialFilter, Linear);

    data_reg_enum_t(g_dataReg, AssetMaterialAniso);
    data_reg_const_t(g_dataReg, AssetMaterialAniso, None);
    data_reg_const_t(g_dataReg, AssetMaterialAniso, x2);
    data_reg_const_t(g_dataReg, AssetMaterialAniso, x4);
    data_reg_const_t(g_dataReg, AssetMaterialAniso, x8);
    data_reg_const_t(g_dataReg, AssetMaterialAniso, x16);

    data_reg_enum_t(g_dataReg, AssetMaterialDepth);
    data_reg_const_t(g_dataReg, AssetMaterialDepth, None);
    data_reg_const_t(g_dataReg, AssetMaterialDepth, Less);
    data_reg_const_t(g_dataReg, AssetMaterialDepth, Always);

    data_reg_enum_t(g_dataReg, AssetMaterialCull);
    data_reg_const_t(g_dataReg, AssetMaterialCull, None);
    data_reg_const_t(g_dataReg, AssetMaterialCull, Back);
    data_reg_const_t(g_dataReg, AssetMaterialCull, Front);

    data_reg_struct_t(g_dataReg, SamplerLoadData);
    data_reg_field_t(g_dataReg, SamplerLoadData, texture, data_prim_t(String));
    data_reg_field_t(g_dataReg, SamplerLoadData, wrap, t_AssetMaterialWrap, .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, SamplerLoadData, filter, t_AssetMaterialFilter, .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, SamplerLoadData, anisotropy, t_AssetMaterialAniso, .flags = DataFlags_Opt);

    data_reg_struct_t(g_dataReg, ShaderLoadData);
    data_reg_field_t(g_dataReg, ShaderLoadData, shader, data_prim_t(String));

    data_reg_struct_t(g_dataReg, MaterialLoadData);
    data_reg_field_t(
        g_dataReg, MaterialLoadData, shaders, t_ShaderLoadData, .container = DataContainer_Array);
    data_reg_field_t(
        g_dataReg, MaterialLoadData, samplers, t_SamplerLoadData, .container = DataContainer_Array);
    data_reg_field_t(
        g_dataReg, MaterialLoadData, topology, t_AssetMaterialTopology, .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, MaterialLoadData, rasterizer, t_AssetMaterialRasterizer, .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, MaterialLoadData, lineWidth, data_prim_t(u32), .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, MaterialLoadData, blend, t_AssetMaterialBlend, .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, MaterialLoadData, depth, t_AssetMaterialDepth, .flags = DataFlags_Opt);
    data_reg_field_t(
        g_dataReg, MaterialLoadData, cull, t_AssetMaterialCull, .flags = DataFlags_Opt);

    g_dataMeta = data_meta_t(t_MaterialLoadData);
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetMaterialComp);
ecs_comp_define(AssetMaterialLoadingComp) { MaterialLoadData data; };

static void ecs_destruct_material_comp(void* data) {
  AssetMaterialComp* comp = data;
  alloc_free_array_t(g_alloc_heap, comp->shaders, comp->shaderCount);
  alloc_free_array_t(g_alloc_heap, comp->samplers, comp->samplerCount);
}

static void ecs_destruct_material_loading_comp(void* data) {
  AssetMaterialLoadingComp* comp = data;
  data_destroy(g_dataReg, g_alloc_heap, g_dataMeta, mem_var(comp->data));
}

typedef enum {
  MatError_None = 0,
  MatError_MalformedJson,
  MatError_ExpectedShader,
  MatError_ExpectedTexture,

  MatError_Count,
} MatError;

typedef enum {
  MatLoad_Done,
  MatLoad_Busy,
} MatLoadProg;

static String mat_error_str(MatError res) {
  static const String msgs[] = {
      string_static("None"),
      string_static("Malformed Json"),
      string_static("Expected a Shader asset"),
      string_static("Expected a Texture asset"),
  };
  ASSERT(array_elems(msgs) == MatError_Count, "Incorrect number of material-error messages");
  return msgs[res];
}

NORETURN static void mat_report_error_msg(const MatError err, const String message) {
  (void)err;
  diag_crash_msg("Failed to parse material, error: {}", fmt_text(message));
}

NORETURN static void mat_report_error(const MatError err) {
  mat_report_error_msg(err, mat_error_str(err));
}

static MatLoadProg
mat_load_asset(EcsWorld* world, AssetManagerComp* manager, const String id, EcsEntityId* asset) {
  if (*asset) {
    return ecs_world_has_t(world, *asset, AssetLoadedComp) ? MatLoad_Done : MatLoad_Busy;
  }
  *asset = asset_lookup(world, manager, id);
  asset_acquire(world, *asset);
  return MatLoad_Busy;
}

static MatLoadProg
mat_load_shaders(EcsWorld* world, AssetManagerComp* manager, MaterialLoadData* data) {
  MatLoadProg prog = MatLoad_Done;
  for (usize i = 0; i != data->shaders.count; ++i) {
    ShaderLoadData* shaderData = &data->shaders.values[i];

    prog |= mat_load_asset(world, manager, shaderData->shader, &shaderData->shaderAsset);
    if (!prog && !ecs_world_has_t(world, shaderData->shaderAsset, AssetShaderComp)) {
      mat_report_error(MatError_ExpectedShader);
    }
  }
  return prog;
}

static MatLoadProg
mat_load_samplers(EcsWorld* world, AssetManagerComp* manager, MaterialLoadData* data) {
  MatLoadProg prog = MatLoad_Done;
  for (usize i = 0; i != data->samplers.count; ++i) {
    SamplerLoadData* samplerData = &data->samplers.values[i];

    prog |= mat_load_asset(world, manager, samplerData->texture, &samplerData->textureAsset);
    if (!prog && !ecs_world_has_t(world, samplerData->textureAsset, AssetTextureComp)) {
      mat_report_error(MatError_ExpectedTexture);
    }
  }
  return prog;
}

static void mat_comp_create(EcsWorld* world, EcsEntityId entity, MaterialLoadData* data) {
  AssetMaterialComp* comp = ecs_world_add_t(
      world,
      entity,
      AssetMaterialComp,
      .shaders      = alloc_array_t(g_alloc_heap, AssetMaterialShader, data->shaders.count),
      .shaderCount  = data->shaders.count,
      .samplers     = alloc_array_t(g_alloc_heap, AssetMaterialSampler, data->samplers.count),
      .samplerCount = data->samplers.count,
      .topology     = data->topology,
      .rasterizer   = data->rasterizer,
      .lineWidth    = data->lineWidth,
      .blend        = data->blend,
      .depth        = data->depth,
      .cull         = data->cull);

  for (usize i = 0; i != data->shaders.count; i++) {
    comp->shaders[i] = (AssetMaterialShader){.shader = data->shaders.values[i].shaderAsset};
  }

  for (usize i = 0; i != data->samplers.count; i++) {
    comp->samplers[i] = (AssetMaterialSampler){
        .texture    = data->samplers.values[i].textureAsset,
        .wrap       = data->samplers.values[i].wrap,
        .filter     = data->samplers.values[i].filter,
        .anisotropy = data->samplers.values[i].anisotropy,
    };
  }
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); };
ecs_view_define(LoadView) { ecs_access_write(AssetMaterialLoadingComp); };

ecs_view_define(UnloadView) {
  ecs_access_read(AssetMaterialComp);
  ecs_access_without(AssetLoadedComp);
};

/**
 * Create material-asset components for loading materials.
 */
ecs_system_define(LoadMaterialAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }
  EcsView* LoadView = ecs_world_view_t(world, LoadView);
  for (EcsIterator* itr = ecs_view_itr(LoadView); ecs_view_walk(itr);) {
    const EcsEntityId entity   = ecs_view_entity(itr);
    MaterialLoadData* loadData = &ecs_view_write_t(itr, AssetMaterialLoadingComp)->data;

    MatLoadProg prog = MatLoad_Done;
    prog |= mat_load_shaders(world, manager, loadData);
    prog |= mat_load_samplers(world, manager, loadData);

    if (prog == MatLoad_Done) {
      ecs_world_remove_t(world, entity, AssetMaterialLoadingComp);
      mat_comp_create(world, entity, loadData);
      ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    }
  }
}

/**
 * Remove any material-asset components for unloaded assets.
 */
ecs_system_define(UnloadMaterialAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId        entity = ecs_view_entity(itr);
    const AssetMaterialComp* asset  = ecs_view_read_t(itr, AssetMaterialComp);
    ecs_world_remove_t(world, entity, AssetMaterialComp);

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

ecs_module_init(asset_material_module) {
  mat_datareg_init();

  ecs_register_comp(AssetMaterialComp, .destructor = ecs_destruct_material_comp);
  ecs_register_comp(AssetMaterialLoadingComp, .destructor = ecs_destruct_material_loading_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(UnloadView);

  ecs_register_system(LoadMaterialAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView));
  ecs_register_system(UnloadMaterialAssetSys, ecs_view_id(UnloadView));
}

void asset_load_mat(EcsWorld* world, EcsEntityId assetEntity, AssetSource* src) {
  MaterialLoadData loadData;
  DataReadResult   readResult;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataMeta, mem_var(loadData), &readResult);
  if (readResult.error) {
    mat_report_error_msg(MatError_MalformedJson, readResult.errorMsg);
  }
  asset_source_close(src);
  ecs_world_add_t(world, assetEntity, AssetMaterialLoadingComp, .data = loadData);
}
