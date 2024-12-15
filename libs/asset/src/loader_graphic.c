#include "asset_graphic.h"
#include "core_alloc.h"
#include "data_read.h"
#include "data_utils.h"
#include "ecs_entity.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "data_internal.h"
#include "manager_internal.h"
#include "repo_internal.h"

DataMeta g_assetGraphicDefMeta;

ecs_comp_define_public(AssetGraphicComp);
ecs_comp_define(AssetGraphicInitComp);

static void ecs_destruct_graphic_comp(void* data) {
  AssetGraphicComp* comp = data;
  data_destroy(
      g_dataReg, g_allocHeap, g_assetGraphicDefMeta, mem_create(comp, sizeof(AssetGraphicComp)));
}

static void graphic_load_fail(
    EcsWorld* world, const EcsEntityId entity, const String id, const String message) {
  log_e(
      "Failed to parse graphic",
      log_param("id", fmt_text(id)),
      log_param("entity", ecs_entity_fmt(entity)),
      log_param("error", fmt_text(message)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }

ecs_view_define(InitView) {
  ecs_access_with(AssetGraphicInitComp);
  ecs_access_write(AssetGraphicComp);
  ecs_access_read(AssetComp);
}

ecs_view_define(UnloadView) {
  ecs_access_read(AssetGraphicComp);
  ecs_access_without(AssetGraphicInitComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Initialize graphic-assets.
 */
ecs_system_define(InitGraphicAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }
  EcsView* initView = ecs_world_view_t(world, InitView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId entity      = ecs_view_entity(itr);
    const String      id          = asset_id(ecs_view_read_t(itr, AssetComp));
    AssetGraphicComp* graphicComp = ecs_view_write_t(itr, AssetGraphicComp);
    const Mem         graphicMem  = mem_create(graphicComp, sizeof(AssetGraphicComp));

    if (!asset_data_patch_refs(world, manager, g_assetGraphicDefMeta, graphicMem)) {
      graphic_load_fail(world, entity, id, string_lit("Unable to resolve asset-reference"));
      goto Error;
    }
    if (graphicComp->mesh.id && graphicComp->vertexCount) {
      graphic_load_fail(
          world, entity, id, string_lit("'mesh' can't be combined with 'vertexCount'"));
      goto Error;
    }

    ecs_world_remove_t(world, entity, AssetGraphicInitComp);
    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    continue;

  Error:
    // NOTE: 'AssetGraphicComp' will be cleaned up by 'UnloadGraphicAssetSys'.
    ecs_world_remove_t(world, entity, AssetGraphicInitComp);
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
  ecs_register_comp_empty(AssetGraphicInitComp);

  ecs_register_view(ManagerView);
  ecs_register_view(InitView);
  ecs_register_view(UnloadView);

  ecs_register_system(InitGraphicAssetSys, ecs_view_id(ManagerView), ecs_view_id(InitView));
  ecs_register_system(UnloadGraphicAssetSys, ecs_view_id(UnloadView));
}

void asset_data_init_graphic(void) {
  // clang-format off
  data_reg_enum_t(g_dataReg, AssetGraphicPass);
  data_reg_const_t(g_dataReg, AssetGraphicPass, Geometry);
  data_reg_const_t(g_dataReg, AssetGraphicPass, Decal);
  data_reg_const_t(g_dataReg, AssetGraphicPass, Fog);
  data_reg_const_t(g_dataReg, AssetGraphicPass, FogBlur);
  data_reg_const_t(g_dataReg, AssetGraphicPass, Shadow);
  data_reg_const_t(g_dataReg, AssetGraphicPass, AmbientOcclusion);
  data_reg_const_t(g_dataReg, AssetGraphicPass, Forward);
  data_reg_const_t(g_dataReg, AssetGraphicPass, Distortion);
  data_reg_const_t(g_dataReg, AssetGraphicPass, Bloom);
  data_reg_const_t(g_dataReg, AssetGraphicPass, Post);

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
  data_reg_field_t(g_dataReg, AssetGraphicShader, program, g_assetRefType);
  data_reg_field_t(g_dataReg, AssetGraphicShader, overrides, t_AssetGraphicOverride, .container = DataContainer_HeapArray, .flags = DataFlags_Opt);

  data_reg_struct_t(g_dataReg, AssetGraphicSampler);
  data_reg_field_t(g_dataReg, AssetGraphicSampler, texture, g_assetRefType);
  data_reg_field_t(g_dataReg, AssetGraphicSampler, wrap, t_AssetGraphicWrap, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetGraphicSampler, filter, t_AssetGraphicFilter, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetGraphicSampler, anisotropy, t_AssetGraphicAniso, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetGraphicSampler, mipBlending, data_prim_t(bool), .flags = DataFlags_Opt);

  data_reg_struct_t(g_dataReg, AssetGraphicComp);
  data_reg_field_t(g_dataReg, AssetGraphicComp, pass, t_AssetGraphicPass);
  data_reg_field_t(g_dataReg, AssetGraphicComp, passOrder, data_prim_t(i32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetGraphicComp, shaders, t_AssetGraphicShader, .container = DataContainer_HeapArray, .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetGraphicComp, samplers, t_AssetGraphicSampler, .container = DataContainer_HeapArray, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetGraphicComp, mesh, g_assetRefType, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetGraphicComp, vertexCount, data_prim_t(u32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
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

  g_assetGraphicDefMeta = data_meta_t(t_AssetGraphicComp);
}

void asset_load_graphic(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;
  (void)id;

  AssetGraphicComp* graphicComp = ecs_world_add_t(world, entity, AssetGraphicComp);
  const Mem         graphicMem  = mem_create(graphicComp, sizeof(AssetGraphicComp));

  DataReadResult result;
  if (src->format == AssetFormat_GraphicBin) {
    data_read_bin(g_dataReg, src->data, g_allocHeap, g_assetGraphicDefMeta, graphicMem, &result);
  } else {
    data_read_json(g_dataReg, src->data, g_allocHeap, g_assetGraphicDefMeta, graphicMem, &result);
  }
  if (result.error) {
    graphic_load_fail(world, entity, id, result.errorMsg);
    goto Ret;
    // NOTE: 'AssetGraphicComp' will be cleaned up by 'UnloadGraphicAssetSys'.
  }

  if (src->format != AssetFormat_GraphicBin) {
    asset_cache(world, entity, g_assetGraphicDefMeta, graphicMem);
  }

  ecs_world_add_empty_t(world, entity, AssetGraphicInitComp);

Ret:
  asset_repo_source_close(src);
}

String asset_graphic_pass_name(const AssetGraphicPass pass) {
  static const String g_names[] = {
      string_static("Geometry"),
      string_static("Decal"),
      string_static("Fog"),
      string_static("FogBlur"),
      string_static("Shadow"),
      string_static("AmbientOcclusion"),
      string_static("Forward"),
      string_static("Distortion"),
      string_static("Bloom"),
      string_static("Post"),
  };
  ASSERT(array_elems(g_names) == AssetGraphicPass_Count, "Incorrect number of names");
  return g_names[pass];
}

String asset_graphic_topology_name(const AssetGraphicTopology topology) {
  static const String g_names[] = {
      string_static("Triangles"),
      string_static("TriangleStrip"),
      string_static("TriangleFan"),
      string_static("Lines"),
      string_static("LineStrip"),
      string_static("Points"),
  };
  ASSERT(array_elems(g_names) == AssetGraphicTopology_Count, "Incorrect number of names");
  return g_names[topology];
}

String asset_graphic_rasterizer_name(const AssetGraphicRasterizer rasterizer) {
  static const String g_names[] = {
      string_static("Fill"),
      string_static("Lines"),
      string_static("Points"),
  };
  ASSERT(array_elems(g_names) == AssetGraphicRasterizer_Count, "Incorrect number of names");
  return g_names[rasterizer];
}

String asset_graphic_blend_name(const AssetGraphicBlend blend) {
  static const String g_names[] = {
      string_static("None"),
      string_static("Alpha"),
      string_static("AlphaConstant"),
      string_static("Additive"),
      string_static("PreMultiplied"),
  };
  ASSERT(array_elems(g_names) == AssetGraphicBlend_Count, "Incorrect number of names");
  return g_names[blend];
}

String asset_graphic_depth_name(const AssetGraphicDepth depth) {
  static const String g_names[] = {
      string_static("Less"),
      string_static("LessOrEqual"),
      string_static("Equal"),
      string_static("Greater"),
      string_static("GreaterOrEqual"),
      string_static("Always"),
      string_static("LessNoWrite"),
      string_static("LessOrEqualNoWrite"),
      string_static("EqualNoWrite"),
      string_static("GreaterNoWrite"),
      string_static("GreaterOrEqualNoWrite"),
      string_static("AlwaysNoWrite"),
  };
  ASSERT(array_elems(g_names) == AssetGraphicDepth_Count, "Incorrect number of names");
  return g_names[depth];
}

String asset_graphic_cull_name(const AssetGraphicCull cull) {
  static const String g_names[] = {
      string_static("Back"),
      string_static("Front"),
      string_static("None"),
  };
  ASSERT(array_elems(g_names) == AssetGraphicCull_Count, "Incorrect number of names");
  return g_names[cull];
}
