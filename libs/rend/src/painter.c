#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "ecs_utils.h"
#include "gap_window.h"
#include "log_logger.h"
#include "rend_register.h"
#include "rend_settings.h"
#include "scene_camera.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "trace_tracer.h"

#include "builder_internal.h"
#include "fog_internal.h"
#include "light_internal.h"
#include "object_internal.h"
#include "painter_internal.h"
#include "platform_internal.h"
#include "reset_internal.h"
#include "resource_internal.h"
#include "rvk/canvas_internal.h"
#include "rvk/graphic_internal.h"
#include "rvk/image_internal.h"
#include "rvk/mesh_internal.h"
#include "rvk/pass_internal.h"
#include "rvk/repository_internal.h"
#include "rvk/texture_internal.h"
#include "view_internal.h"

ecs_comp_define_public(RendPainterComp);

static void ecs_destruct_painter(void* data) {
  RendPainterComp* comp = data;
  rvk_canvas_destroy(comp->canvas);
}

ecs_view_define(GlobalView) {
  ecs_access_read(RendFogComp);
  ecs_access_read(RendLightRendererComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_without(RendResetComp);
  ecs_access_write(RendPlatformComp);
}

ecs_view_define(ObjView) { ecs_access_read(RendObjectComp); }

ecs_view_define(ResourceView) {
  ecs_access_maybe_read(RendResGraphicComp);
  ecs_access_maybe_read(RendResMeshComp);
  ecs_access_maybe_read(RendResTextureComp);
  ecs_access_with(RendResFinishedComp);
  ecs_access_without(RendResUnloadComp);
  ecs_access_read(RendResComp);
}

ecs_view_define(PainterCreateView) {
  ecs_access_read(GapWindowComp);
  ecs_access_without(RendPainterComp);
}

ecs_view_define(PainterUpdateView) {
  ecs_access_read(GapWindowComp);
  ecs_access_write(RendPainterComp);
  ecs_access_read(RendSettingsComp);

  ecs_access_maybe_read(SceneCameraComp);
  ecs_access_maybe_read(SceneTransformComp);
}

static RvkSize painter_win_size(const GapWindowComp* win) {
  const GapVector winSize = gap_window_param(win, GapParam_WindowSize);
  return rvk_size((u16)winSize.width, (u16)winSize.height);
}

static RendView painter_view_2d_create(const EcsEntityId sceneCameraEntity) {
  const GeoVector      cameraPosition = geo_vector(0);
  const GeoMatrix      viewProjMatrix = geo_matrix_ident();
  const SceneTagFilter sceneFilter    = {0};
  return rend_view_create(sceneCameraEntity, cameraPosition, &viewProjMatrix, sceneFilter);
}

static RendView painter_view_3d_create(
    const GeoMatrix*     cameraMatrix,
    const GeoMatrix*     projMatrix,
    const EcsEntityId    sceneCameraEntity,
    const SceneTagFilter sceneFilter) {
  const GeoVector cameraPosition = geo_matrix_to_translation(cameraMatrix);
  const GeoMatrix viewMatrix     = geo_matrix_inverse(cameraMatrix);
  const GeoMatrix viewProjMatrix = geo_matrix_mul(projMatrix, &viewMatrix);
  return rend_view_create(sceneCameraEntity, cameraPosition, &viewProjMatrix, sceneFilter);
}

typedef struct {
  RvkCanvas*              canvas;
  RendBuilderBuffer*      builder;
  const RendSettingsComp* settings;
  const SceneTimeComp*    time;
  RvkPass*                pass;
  RendView                view;
} RendPaintContext;

static RendPaintContext painter_context(
    RvkCanvas*              canvas,
    RendBuilderBuffer*      builder,
    const RendSettingsComp* settings,
    const SceneTimeComp*    time,
    RvkPass*                pass,
    RendView                view) {

  return (RendPaintContext){
      .canvas   = canvas,
      .builder  = builder,
      .settings = settings,
      .time     = time,
      .pass     = pass,
      .view     = view,
  };
}

typedef enum {
  RendViewType_Main,
  RendViewType_Shadow,
  RendViewType_Fog,
} RendViewType;

static void painter_stage_global_data(
    RendPaintContext*    ctx,
    const GeoMatrix*     cameraMatrix,
    const GeoMatrix*     projMatrix,
    const RvkSize        size,
    const SceneTimeComp* time,
    const RendViewType   viewType) {
  const f32 aspect = (f32)size.width / (f32)size.height;

  typedef struct {
    ALIGNAS(16)
    GeoMatrix view, viewInv;
    GeoMatrix proj, projInv;
    GeoMatrix viewProj, viewProjInv;
    GeoVector camPosition;
    GeoQuat   camRotation;
    GeoVector resolution; // x: width, y: height, z: aspect ratio (width / height), w: unused.
    GeoVector time;       // x: time seconds, y: real-time seconds, z, w: unused.
  } RendPainterGlobalData;
  ASSERT(sizeof(RendPainterGlobalData) == 448, "Size needs to match the size defined in glsl");

  RendPainterGlobalData data = {
      .resolution.x = size.width,
      .resolution.y = size.height,
      .resolution.z = aspect,
      .time.x       = scene_time_seconds(time),
      .time.y       = scene_real_time_seconds(time),
  };

  if (viewType == RendViewType_Main && ctx->settings->flags & RendFlags_DebugCamera) {
    static const f32 g_size     = 300;
    static const f32 g_depthMin = -200;
    static const f32 g_depthMax = 200;

    data.viewInv     = geo_matrix_rotate_x(math_pi_f32 * 0.5f);
    data.view        = geo_matrix_inverse(&data.viewInv);
    data.proj        = geo_matrix_proj_ortho_hor(g_size, aspect, g_depthMin, g_depthMax);
    data.projInv     = geo_matrix_inverse(&data.proj);
    data.viewProj    = geo_matrix_mul(&data.proj, &data.view);
    data.viewProjInv = geo_matrix_inverse(&data.viewProj);
    data.camPosition = geo_vector(0, 0, 0);
    data.camRotation = geo_quat_forward_to_down;
  } else {
    data.viewInv     = *cameraMatrix;
    data.view        = geo_matrix_inverse(cameraMatrix);
    data.proj        = *projMatrix;
    data.projInv     = geo_matrix_inverse(projMatrix);
    data.viewProj    = geo_matrix_mul(&data.proj, &data.view);
    data.viewProjInv = geo_matrix_inverse(&data.viewProj);
    data.camPosition = geo_matrix_to_translation(cameraMatrix);
    data.camRotation = geo_matrix_to_quat(cameraMatrix);
  }
  rvk_pass_stage_global_data(ctx->pass, mem_var(data), 0);
}

static const RvkGraphic* painter_get_graphic(EcsIterator* resourceItr, const EcsEntityId resource) {
  if (!ecs_view_maybe_jump(resourceItr, resource)) {
    return null; // Resource not loaded yet.
  }
  const RendResComp* resComp = ecs_view_read_t(resourceItr, RendResComp);
  if (rend_res_is_failed(resComp)) {
    return null; // Failed to load.
  }
  const RendResGraphicComp* graphicRes = ecs_view_read_t(resourceItr, RendResGraphicComp);
  if (!graphicRes) {
    log_e("Invalid graphic asset", log_param("entity", ecs_entity_fmt(resource)));
    return null;
  }
  return graphicRes->graphic;
}

static const RvkTexture* painter_get_texture(EcsIterator* resourceItr, const EcsEntityId resource) {
  if (!ecs_view_maybe_jump(resourceItr, resource)) {
    return null; // Resource not loaded yet.
  }
  const RendResComp* resComp = ecs_view_read_t(resourceItr, RendResComp);
  if (rend_res_is_failed(resComp)) {
    return null; // Failed to load.
  }
  const RendResTextureComp* textureRes = ecs_view_read_t(resourceItr, RendResTextureComp);
  if (!textureRes) {
    log_e("Invalid texture asset", log_param("entity", ecs_entity_fmt(resource)));
    return null;
  }
  return textureRes->texture;
}

static void painter_push_simple(RendPaintContext* ctx, const RvkRepositoryId id, const Mem data) {
  const RvkRepository* repo    = rvk_canvas_repository(ctx->canvas);
  const RvkGraphic*    graphic = rvk_repository_graphic_get_maybe(repo, id);
  if (graphic) {
    rend_builder_draw_push(ctx->builder, graphic);
    if (data.size) {
      mem_cpy(rend_builder_draw_data(ctx->builder, data.size), data);
    }
    rend_builder_draw_instances(ctx->builder, 1, 0);
    rend_builder_draw_flush(ctx->builder);
  }
}

static SceneTags painter_push_objects_simple(
    RendPaintContext* ctx, EcsView* objView, EcsView* resView, const AssetGraphicPass passId) {
  SceneTags    tagMask     = 0;
  EcsIterator* resourceItr = ecs_view_itr(resView);
  for (EcsIterator* objItr = ecs_view_itr(objView); ecs_view_walk(objItr);) {
    const RendObjectComp* obj = ecs_view_read_t(objItr, RendObjectComp);
    if (!rend_object_instance_count(obj)) {
      continue; // Object has no instances.
    }

    // Retrieve and prepare the object's graphic.
    const EcsEntityId graphicResource = rend_object_resource(obj, RendObjectRes_Graphic);
    const RvkGraphic* graphic         = painter_get_graphic(resourceItr, graphicResource);
    if (!graphic || graphic->passId != passId) {
      continue; // Graphic not loaded or not valid for this pass.
    }

    // If the object uses a 'per draw' texture then retrieve and prepare it.
    const EcsEntityId textureResource = rend_object_resource(obj, RendObjectRes_Texture);
    const RvkTexture* texture         = null;
    if (textureResource && !(texture = painter_get_texture(resourceItr, textureResource))) {
      continue; // Object uses a 'per draw' texture which is not loaded (yet).
    }

    rend_builder_draw_push(ctx->builder, graphic);
    if (texture) {
      // TODO: This cast violates const-correctness.
      rend_builder_draw_image(ctx->builder, (RvkImage*)&texture->image);
    }
    rend_object_draw(obj, &ctx->view, ctx->settings, ctx->builder);
    rend_builder_draw_flush(ctx->builder);

    tagMask |= rend_object_tag_mask(obj);
  }

  return tagMask;
}

static void painter_push_shadow(RendPaintContext* ctx, EcsView* objView, EcsView* resView) {
  RendObjectFlags requiredAny = 0;
  requiredAny |= RendObjectFlags_StandardGeometry; // Include geometry.
  requiredAny |= RendObjectFlags_VfxSprite;        // Include vfx sprites.

  const RvkRepository* repo        = rvk_canvas_repository(ctx->canvas);
  EcsIterator*         resourceItr = ecs_view_itr(resView);

  for (EcsIterator* objItr = ecs_view_itr(objView); ecs_view_walk(objItr);) {
    const RendObjectComp* obj = ecs_view_read_t(objItr, RendObjectComp);
    if (!rend_object_instance_count(obj)) {
      continue; // Object has no instances.
    }
    if (!(rend_object_flags(obj) & requiredAny)) {
      continue; // Object shouldn't be included in the shadow pass.
    }
    const EcsEntityId graphicOriginalRes = rend_object_resource(obj, RendObjectRes_Graphic);
    const RvkGraphic* graphicOriginal    = painter_get_graphic(resourceItr, graphicOriginalRes);
    if (!graphicOriginal) {
      continue; // Graphic not loaded.
    }
    const bool     isVfxSprite = (rend_object_flags(obj) & RendObjectFlags_VfxSprite) != 0;
    const RvkMesh* objMesh     = graphicOriginal->mesh;
    if (!isVfxSprite && !objMesh) {
      continue; // Graphic is not a vfx sprite and does not have a mesh to draw a shadow for.
    }
    RvkImage* objAlphaImg = null;
    enum { AlphaTextureIndex = 2 }; // TODO: Make this configurable from content.
    const bool hasAlphaTexture = (graphicOriginal->samplerMask & (1 << AlphaTextureIndex)) != 0;
    if (graphicOriginal->flags & RvkGraphicFlags_MayDiscard && hasAlphaTexture) {
      const RvkTexture* alphaTexture = graphicOriginal->samplerTextures[AlphaTextureIndex];
      if (!alphaTexture) {
        continue; // Graphic uses discard but has no alpha texture.
      }
      // TODO: This cast violates const-correctness.
      objAlphaImg = (RvkImage*)&alphaTexture->image;
    }
    RvkRepositoryId graphicId;
    if (isVfxSprite) {
      graphicId = RvkRepositoryId_ShadowVfxSpriteGraphic;
    } else if (rend_object_flags(obj) & RendObjectFlags_Skinned) {
      graphicId = RvkRepositoryId_ShadowSkinnedGraphic;
    } else {
      graphicId = objAlphaImg ? RvkRepositoryId_ShadowClipGraphic : RvkRepositoryId_ShadowGraphic;
    }
    const RvkGraphic* shadowGraphic = rvk_repository_graphic_get_maybe(repo, graphicId);
    if (!shadowGraphic) {
      continue; // Shadow graphic not loaded.
    }

    rend_builder_draw_push(ctx->builder, shadowGraphic);
    rend_builder_draw_mesh(ctx->builder, objMesh);
    if (objAlphaImg) {
      rend_builder_draw_image(ctx->builder, objAlphaImg);
      rend_builder_draw_sampler(ctx->builder, (RvkSamplerSpec){.aniso = RvkSamplerAniso_x8});
    }
    rend_object_draw(obj, &ctx->view, ctx->settings, ctx->builder);
    rend_builder_draw_flush(ctx->builder);
  }
}

static void painter_push_fog(RendPaintContext* ctx, const RendFogComp* fog, RvkImage* fogMap) {
  const RvkRepository* repo    = rvk_canvas_repository(ctx->canvas);
  const RvkGraphic*    graphic = rvk_repository_graphic_get_maybe(repo, RvkRepositoryId_FogGraphic);
  if (graphic) {
    typedef struct {
      ALIGNAS(16)
      GeoMatrix fogViewProj;
    } FogData;

    rend_builder_draw_push(ctx->builder, graphic);

    FogData*        data       = rend_builder_draw_data(ctx->builder, sizeof(FogData)).ptr;
    const GeoMatrix fogViewMat = geo_matrix_inverse(rend_fog_trans(fog));
    data->fogViewProj          = geo_matrix_mul(rend_fog_proj(fog), &fogViewMat);

    rend_builder_draw_image(ctx->builder, fogMap);
    rend_builder_draw_instances(ctx->builder, 1, 0);
    rend_builder_draw_flush(ctx->builder);
  }
}

static void painter_push_ambient(RendPaintContext* ctx, const f32 intensity) {
  typedef enum {
    AmbientFlags_AmbientOcclusion     = 1 << 0,
    AmbientFlags_AmbientOcclusionBlur = 1 << 1,
  } AmbientFlags;

  struct {
    ALIGNAS(16)
    GeoVector packed; // x: ambientLight, y: mode, z: flags, w: unused.
  } data;

  AmbientFlags flags = 0;
  if (ctx->settings->flags & RendFlags_AmbientOcclusion) {
    flags |= AmbientFlags_AmbientOcclusion;
  }
  if (ctx->settings->flags & RendFlags_AmbientOcclusionBlur) {
    flags |= AmbientFlags_AmbientOcclusionBlur;
  }

  data.packed.x = intensity;
  data.packed.y = bits_u32_as_f32(ctx->settings->ambientMode);
  data.packed.z = bits_u32_as_f32(flags);

  RvkRepositoryId graphicId;
  if (ctx->settings->ambientMode >= RendAmbientMode_DebugStart) {
    graphicId = RvkRepositoryId_AmbientDebugGraphic;
  } else {
    graphicId = RvkRepositoryId_AmbientGraphic;
  }
  painter_push_simple(ctx, graphicId, mem_var(data));
}

static void painter_push_ambient_occlusion(RendPaintContext* ctx) {
  struct {
    ALIGNAS(16)
    f32       radius;
    f32       power;
    GeoVector kernel[rend_ao_kernel_size];
  } data;

  data.radius = ctx->settings->aoRadius;
  data.power  = ctx->settings->aoPower;

  const Mem kernel = mem_create(ctx->settings->aoKernel, sizeof(GeoVector) * rend_ao_kernel_size);
  mem_cpy(array_mem(data.kernel), kernel);

  painter_push_simple(ctx, RvkRepositoryId_AmbientOcclusionGraphic, mem_var(data));
}

static void painter_push_tonemapping(RendPaintContext* ctx) {
  struct {
    ALIGNAS(16)
    f32 exposure;
    u32 mode;
    f32 bloomIntensity;
  } data;

  data.exposure       = ctx->settings->exposure;
  data.mode           = ctx->settings->tonemapper;
  data.bloomIntensity = ctx->settings->flags & RendFlags_Bloom ? ctx->settings->bloomIntensity : 0;

  painter_push_simple(ctx, RvkRepositoryId_TonemapperGraphic, mem_var(data));
}

static void
painter_push_debug_image_viewer(RendPaintContext* ctx, RvkImage* image, const f32 exposure) {
  const RvkRepository* repo = rvk_canvas_repository(ctx->canvas);
  const RvkGraphic*    graphic;
  if (image->type == RvkImageType_ColorSourceCube) {
    graphic = rvk_repository_graphic_get_maybe(repo, RvkRepositoryId_DebugImageViewerCubeGraphic);
  } else {
    graphic = rvk_repository_graphic_get_maybe(repo, RvkRepositoryId_DebugImageViewerGraphic);
  }
  if (graphic) {
    typedef struct {
      ALIGNAS(16)
      u16 imageChannels;
      f16 lod;
      u32 flags;
      f32 exposure;
      f32 aspect;
    } ImageViewerData;

    enum {
      ImageViewerFlags_FlipY       = 1 << 0,
      ImageViewerFlags_AlphaIgnore = 1 << 1,
      ImageViewerFlags_AlphaOnly   = 1 << 2,
    };

    u32 flags = 0;
    if (image->type != RvkImageType_ColorSource && image->type != RvkImageType_ColorSourceCube) {
      /**
       * Volo is using source textures with the image origin at the bottom left (as opposed to the
       * conventional top left). This is an historical mistake that should be corrected but until
       * that time we need to flip non-source (attachments) images as they are using top-left.
       */
      flags |= ImageViewerFlags_FlipY;
    }
    if (ctx->settings->debugViewerFlags & RendDebugViewer_AlphaIgnore) {
      flags |= ImageViewerFlags_AlphaIgnore;
    }
    if (ctx->settings->debugViewerFlags & RendDebugViewer_AlphaOnly) {
      flags |= ImageViewerFlags_AlphaOnly;
    }

    rend_builder_draw_push(ctx->builder, graphic);

    ImageViewerData* data = rend_builder_draw_data(ctx->builder, sizeof(ImageViewerData)).ptr;
    data->imageChannels   = rvk_format_info(image->vkFormat).channels;
    data->lod             = float_f32_to_f16(ctx->settings->debugViewerLod);
    data->flags           = flags;
    data->exposure        = exposure;
    data->aspect          = (f32)image->size.width / (f32)image->size.height;

    RvkSamplerSpec sampler = {.filter = RvkSamplerFilter_Nearest};
    if (ctx->settings->debugViewerFlags & RendDebugViewer_Interpolate) {
      sampler.filter = RvkSamplerFilter_Linear;
    }
    rend_builder_draw_image(ctx->builder, image);
    rend_builder_draw_sampler(ctx->builder, sampler);
    rend_builder_draw_instances(ctx->builder, 1, 0);
    rend_builder_draw_flush(ctx->builder);
  }
}

static void
painter_push_debug_mesh_viewer(RendPaintContext* ctx, const f32 aspect, const RvkMesh* mesh) {
  const RvkRepository*  repo      = rvk_canvas_repository(ctx->canvas);
  const RvkRepositoryId graphicId = RvkRepositoryId_DebugMeshViewerGraphic;
  const RvkGraphic*     graphic   = rvk_repository_graphic_get_maybe(repo, graphicId);
  if (graphic) {
    typedef struct {
      ALIGNAS(16)
      GeoMatrix viewProj;
    } MeshViewerData;

    const GeoVector meshCenter = geo_box_center(&mesh->positionRawBounds);
    const f32       meshSize   = math_max(1.0f, geo_box_size(&mesh->positionRawBounds).y);

    const GeoVector pos       = geo_vector(0, -meshCenter.y + meshSize * 0.15f);
    const f32       orthoSize = meshSize * 1.75f;
    const f32       rotY      = scene_real_time_seconds(ctx->time) * math_deg_to_rad * 10.0f;
    const f32       rotX      = -10.0f * math_deg_to_rad;
    const GeoMatrix projMat   = geo_matrix_proj_ortho_hor(orthoSize, aspect, -100.0f, 100.0f);
    const GeoMatrix rotYMat   = geo_matrix_rotate_y(rotY);
    const GeoMatrix rotXMat   = geo_matrix_rotate_x(rotX);
    const GeoMatrix rotMat    = geo_matrix_mul(&rotXMat, &rotYMat);
    const GeoMatrix posMat    = geo_matrix_translate(pos);
    const GeoMatrix viewMat   = geo_matrix_mul(&posMat, &rotMat);

    rend_builder_draw_push(ctx->builder, graphic);

    MeshViewerData* data = rend_builder_draw_data(ctx->builder, sizeof(MeshViewerData)).ptr;
    data->viewProj       = geo_matrix_mul(&projMat, &viewMat);

    rend_builder_draw_mesh(ctx->builder, mesh);
    rend_builder_draw_instances(ctx->builder, 1, 0);
    rend_builder_draw_flush(ctx->builder);
  }
}

static void painter_push_debug_resource_viewer(
    EcsWorld*         world,
    RendPaintContext* ctx,
    const f32         aspect,
    EcsView*          resView,
    const EcsEntityId resEntity) {

  rend_res_request(world, resEntity);

  EcsIterator* itr = ecs_view_maybe_at(resView, resEntity);
  if (itr) {
    const RendResTextureComp* textureComp = ecs_view_read_t(itr, RendResTextureComp);
    if (textureComp) {
      const f32 exposure = 1.0f;
      // TODO: This cast violates const-correctness.
      painter_push_debug_image_viewer(ctx, (RvkImage*)&textureComp->texture->image, exposure);
    }
    const RendResMeshComp* meshComp = ecs_view_read_t(itr, RendResMeshComp);
    if (meshComp) {
      painter_push_debug_mesh_viewer(ctx, aspect, meshComp->mesh);
    }
  }
}

static void
painter_push_debug_wireframe(RendPaintContext* ctx, EcsView* objView, EcsView* resView) {
  EcsIterator* resourceItr = ecs_view_itr(resView);
  for (EcsIterator* objItr = ecs_view_itr(objView); ecs_view_walk(objItr);) {
    const RendObjectComp* obj = ecs_view_read_t(objItr, RendObjectComp);
    if (!rend_object_instance_count(obj)) {
      continue; // Object has no instances.
    }
    const EcsEntityId graphicRes = rend_object_resource(obj, RendObjectRes_DebugWireframeGraphic);
    if (!graphicRes) {
      continue; // Object has no debug wireframe graphic.
    }
    const RvkGraphic* graphic = painter_get_graphic(resourceItr, graphicRes);
    if (!graphic) {
      continue; // Wireframe graphic is not loaded.
    }

    const EcsEntityId graphicOrgRes = rend_object_resource(obj, RendObjectRes_Graphic);
    const RvkGraphic* graphicOrg    = painter_get_graphic(resourceItr, graphicOrgRes);
    if (!graphicOrg || !graphicOrg->mesh) {
      continue; // Graphic is not loaded or has no mesh.
    }

    // If the object uses a 'per draw' texture then retrieve and prepare it.
    const EcsEntityId textureRes = rend_object_resource(obj, RendObjectRes_Texture);
    const RvkTexture* texture    = null;
    if (textureRes && !(texture = painter_get_texture(resourceItr, textureRes))) {
      continue; // Object uses a 'per draw' texture which is not loaded (yet).
    }

    rend_builder_draw_push(ctx->builder, graphic);
    rend_builder_draw_mesh(ctx->builder, graphicOrg->mesh);
    if (texture) {
      // TODO: This cast violates const-correctness.
      rend_builder_draw_image(ctx->builder, (RvkImage*)&texture->image);
    }
    rend_object_draw(obj, &ctx->view, ctx->settings, ctx->builder);
    rend_builder_draw_flush(ctx->builder);
  }
}

static void painter_push_debug_skinning(RendPaintContext* ctx, EcsView* objView, EcsView* resView) {
  EcsIterator* resourceItr = ecs_view_itr(resView);
  for (EcsIterator* objItr = ecs_view_itr(objView); ecs_view_walk(objItr);) {
    const RendObjectComp* obj = ecs_view_read_t(objItr, RendObjectComp);
    if (!rend_object_instance_count(obj)) {
      continue; // Object has no instances.
    }
    const EcsEntityId graphicRes = rend_object_resource(obj, RendObjectRes_DebugSkinningGraphic);
    if (!graphicRes) {
      continue; // Object has no debug skinning graphic.
    }
    const RvkGraphic* graphic = painter_get_graphic(resourceItr, graphicRes);
    if (!graphic) {
      continue; // Skinning graphic is not loaded.
    }

    const EcsEntityId graphicOrgRes = rend_object_resource(obj, RendObjectRes_Graphic);
    const RvkGraphic* graphicOrg    = painter_get_graphic(resourceItr, graphicOrgRes);
    if (!graphicOrg || !graphicOrg->mesh) {
      continue; // Graphic is not loaded or has no mesh.
    }

    rend_builder_draw_push(ctx->builder, graphic);
    rend_builder_draw_mesh(ctx->builder, graphicOrg->mesh);
    rend_object_draw(obj, &ctx->view, ctx->settings, ctx->builder);
    rend_builder_draw_flush(ctx->builder);
  }
}

static bool rend_canvas_paint_2d(
    RendPainterComp*        painter,
    RendPlatformComp*       platform,
    const RendSettingsComp* set,
    const SceneTimeComp*    time,
    const GapWindowComp*    win,
    const EcsEntityId       camEntity,
    EcsView*                objView,
    EcsView*                resView) {

  if (!rvk_canvas_begin(painter->canvas, set, painter_win_size(win))) {
    return false; // Canvas not ready for rendering.
  }
  trace_begin("rend_paint_2d", TraceColor_Red);

  RendBuilderBuffer* builder = rend_builder_buffer(platform->builder);

  const RendView mainView = painter_view_2d_create(camEntity);

  RvkImage*     swapchainImage = rvk_canvas_swapchain_image(painter->canvas);
  const RvkSize swapchainSize  = swapchainImage->size;

  RvkPass*  postPass = platform->passes[AssetGraphicPass_Post];
  RvkImage* postRes  = rvk_canvas_attach_acquire_color(painter->canvas, postPass, 0, swapchainSize);
  {
    rend_builder_pass_push(builder, postPass);

    rvk_canvas_img_clear_color(painter->canvas, postRes, geo_color_black);

    RendPaintContext ctx = painter_context(painter->canvas, builder, set, time, postPass, mainView);
    rvk_pass_stage_attach_color(postPass, postRes, 0);
    painter_push_objects_simple(&ctx, objView, resView, AssetGraphicPass_Post);
    rend_builder_pass_flush(builder);

    // TODO: Render into the swapchain directly if the swapchain format matches the pass format.
    rvk_canvas_img_blit(painter->canvas, postRes, swapchainImage);
    rvk_canvas_attach_release(painter->canvas, postRes);
  }

  trace_end();
  rvk_canvas_end(painter->canvas);
  return true;
}

static bool rend_canvas_paint_3d(
    EcsWorld*                    world,
    RendPainterComp*             painter,
    RendPlatformComp*            platform,
    const RendSettingsComp*      set,
    const SceneTimeComp*         time,
    const RendLightRendererComp* light,
    const RendFogComp*           fog,
    const GapWindowComp*         win,
    const EcsEntityId            camEntity,
    const SceneCameraComp*       cam,
    const SceneTransformComp*    camTrans,
    EcsView*                     objView,
    EcsView*                     resView) {

  const RvkSize winSize   = painter_win_size(win);
  const f32     winAspect = (f32)winSize.width / (f32)winSize.height;

  if (!rvk_canvas_begin(painter->canvas, set, winSize)) {
    return false; // Canvas not ready for rendering.
  }
  trace_begin("rend_paint_3d", TraceColor_Red);

  RendBuilderBuffer* builder = rend_builder_buffer(platform->builder);

  const GeoMatrix      camMat   = camTrans ? scene_transform_matrix(camTrans) : geo_matrix_ident();
  const GeoMatrix      projMat  = cam ? scene_camera_proj(cam, winAspect)
                                      : geo_matrix_proj_ortho_hor(2.0, winAspect, -100, 100);
  const SceneTagFilter filter   = cam ? cam->filter : (SceneTagFilter){0};
  const RendView       mainView = painter_view_3d_create(&camMat, &projMat, camEntity, filter);

  RvkImage*     swapchainImage = rvk_canvas_swapchain_image(painter->canvas);
  const RvkSize swapchainSize  = swapchainImage->size;

  // Geometry pass.
  const RvkSize geoSize  = rvk_size_scale(swapchainSize, set->resolutionScale);
  RvkPass*      geoPass  = platform->passes[AssetGraphicPass_Geometry];
  RvkImage*     geoData0 = rvk_canvas_attach_acquire_color(painter->canvas, geoPass, 0, geoSize);
  RvkImage*     geoData1 = rvk_canvas_attach_acquire_color(painter->canvas, geoPass, 1, geoSize);
  RvkImage*     geoDepth = rvk_canvas_attach_acquire_depth(painter->canvas, geoPass, geoSize);
  SceneTags     geoTagMask;
  {
    trace_begin("rend_paint_geo", TraceColor_White);
    rend_builder_pass_push(builder, geoPass);

    RendPaintContext ctx = painter_context(painter->canvas, builder, set, time, geoPass, mainView);
    rvk_pass_stage_attach_color(geoPass, geoData0, 0);
    rvk_pass_stage_attach_color(geoPass, geoData1, 1);
    rvk_pass_stage_attach_depth(geoPass, geoDepth);
    painter_stage_global_data(&ctx, &camMat, &projMat, geoSize, time, RendViewType_Main);
    geoTagMask = painter_push_objects_simple(&ctx, objView, resView, AssetGraphicPass_Geometry);

    rend_builder_pass_flush(builder);
    trace_end();
  }

  // Make a copy of the geometry depth to read from while still writing to the original.
  // TODO: Instead of a straight copy considering performing linearization at the same time.
  RvkImage* geoDepthRead = rvk_canvas_attach_acquire_copy(painter->canvas, geoDepth);

  // Decal pass.
  RvkPass* decalPass = platform->passes[AssetGraphicPass_Decal];
  if (set->flags & RendFlags_Decals) {

    trace_begin("rend_paint_decals", TraceColor_White);
    rend_builder_pass_push(builder, decalPass);

    // Copy the gbufer data1 image to be able to read the gbuffer normal and tags.
    RvkImage* geoData1Cpy = rvk_canvas_attach_acquire_copy(painter->canvas, geoData1);

    RendPaintContext ctx =
        painter_context(painter->canvas, builder, set, time, decalPass, mainView);
    rvk_pass_stage_global_image(decalPass, geoData1Cpy, 0);
    rvk_pass_stage_global_image(decalPass, geoDepthRead, 1);
    rvk_pass_stage_attach_color(decalPass, geoData0, 0);
    rvk_pass_stage_attach_color(decalPass, geoData1, 1);
    rvk_pass_stage_attach_depth(decalPass, geoDepth);
    painter_stage_global_data(&ctx, &camMat, &projMat, geoSize, time, RendViewType_Main);
    painter_push_objects_simple(&ctx, objView, resView, AssetGraphicPass_Decal);

    rend_builder_pass_flush(builder);
    trace_end();

    rvk_canvas_attach_release(painter->canvas, geoData1Cpy);
  }

  // Fog pass.
  const bool    fogActive = rend_fog_active(fog);
  RvkPass*      fogPass   = platform->passes[AssetGraphicPass_Fog];
  const u16     fogRes    = set->fogResolution;
  const RvkSize fogSize   = fogActive ? (RvkSize){fogRes, fogRes} : (RvkSize){1, 1};
  RvkImage*     fogBuffer = rvk_canvas_attach_acquire_color(painter->canvas, fogPass, 0, fogSize);
  if (fogActive) {
    trace_begin("rend_paint_fog", TraceColor_White);
    rend_builder_pass_push(builder, fogPass);

    const GeoMatrix*     fogTrans  = rend_fog_trans(fog);
    const GeoMatrix*     fogProj   = rend_fog_proj(fog);
    const SceneTagFilter fogFilter = {0};
    const RendView       fogView = painter_view_3d_create(fogTrans, fogProj, camEntity, fogFilter);

    RendPaintContext ctx = painter_context(painter->canvas, builder, set, time, fogPass, fogView);
    rvk_pass_stage_attach_color(fogPass, fogBuffer, 0);
    painter_stage_global_data(&ctx, fogTrans, fogProj, fogSize, time, RendViewType_Fog);
    painter_push_objects_simple(&ctx, objView, resView, AssetGraphicPass_Fog);

    rend_builder_pass_flush(builder);
    trace_end();
  } else {
    rvk_canvas_img_clear_color(painter->canvas, fogBuffer, geo_color_white);
  }

  // Fog-blur pass.
  RvkPass* fogBlurPass = platform->passes[AssetGraphicPass_FogBlur];
  if (fogActive && set->fogBlurSteps) {
    trace_begin("rend_paint_fog_blur", TraceColor_White);

    RendPaintContext ctx =
        painter_context(painter->canvas, builder, set, time, fogBlurPass, mainView);

    struct {
      ALIGNAS(16)
      f32 sampleScale;
    } blurData = {.sampleScale = set->fogBlurScale};

    RvkImage* tmp = rvk_canvas_attach_acquire_copy_uninit(painter->canvas, fogBuffer);
    for (u32 i = 0; i != set->fogBlurSteps; ++i) {
      // Horizontal pass.
      rend_builder_pass_push(builder, fogBlurPass);
      rvk_pass_stage_global_image(fogBlurPass, fogBuffer, 0);
      rvk_pass_stage_attach_color(fogBlurPass, tmp, 0);
      painter_push_simple(&ctx, RvkRepositoryId_FogBlurHorGraphic, mem_var(blurData));
      rend_builder_pass_flush(builder);

      // Vertical pass.
      rend_builder_pass_push(builder, fogBlurPass);
      rvk_pass_stage_global_image(fogBlurPass, tmp, 0);
      rvk_pass_stage_attach_color(fogBlurPass, fogBuffer, 0);
      painter_push_simple(&ctx, RvkRepositoryId_FogBlurVerGraphic, mem_var(blurData));
      rend_builder_pass_flush(builder);
    }
    rvk_canvas_attach_release(painter->canvas, tmp);
    trace_end();
  }

  // Shadow pass.
  const bool    shadowsActive = set->flags & RendFlags_Shadows && rend_light_has_shadow(light);
  const RvkSize shadowSize =
      shadowsActive ? (RvkSize){set->shadowResolution, set->shadowResolution} : (RvkSize){1, 1};
  RvkPass*  shadowPass  = platform->passes[AssetGraphicPass_Shadow];
  RvkImage* shadowDepth = rvk_canvas_attach_acquire_depth(painter->canvas, shadowPass, shadowSize);
  if (shadowsActive) {
    trace_begin("rend_paint_shadows", TraceColor_White);
    rend_builder_pass_push(builder, shadowPass);

    const GeoMatrix* shadTrans  = rend_light_shadow_trans(light);
    const GeoMatrix* shadProj   = rend_light_shadow_proj(light);
    SceneTagFilter   shadFilter = {
        .required = filter.required | SceneTags_ShadowCaster,
        .illegal  = filter.illegal,
    };
    if (!(set->flags & RendFlags_VfxShadows)) {
      shadFilter.illegal |= SceneTags_Vfx;
    }
    const RendView   shadView = painter_view_3d_create(shadTrans, shadProj, camEntity, shadFilter);
    RendPaintContext ctx =
        painter_context(painter->canvas, builder, set, time, shadowPass, shadView);
    rvk_pass_stage_attach_depth(shadowPass, shadowDepth);
    painter_stage_global_data(&ctx, shadTrans, shadProj, shadowSize, time, RendViewType_Shadow);
    painter_push_shadow(&ctx, objView, resView);

    rend_builder_pass_flush(builder);
    trace_end();
  } else {
    rvk_canvas_img_clear_depth(painter->canvas, shadowDepth, 0);
  }

  // Ambient occlusion.
  const RvkSize aoSize   = set->flags & RendFlags_AmbientOcclusion
                               ? rvk_size_scale(geoSize, set->aoResolutionScale)
                               : (RvkSize){1, 1};
  RvkPass*      aoPass   = platform->passes[AssetGraphicPass_AmbientOcclusion];
  RvkImage*     aoBuffer = rvk_canvas_attach_acquire_color(painter->canvas, aoPass, 0, aoSize);
  if (set->flags & RendFlags_AmbientOcclusion) {
    trace_begin("rend_paint_ao", TraceColor_White);
    rend_builder_pass_push(builder, aoPass);

    RendPaintContext ctx = painter_context(painter->canvas, builder, set, time, aoPass, mainView);
    rvk_pass_stage_global_image(aoPass, geoData1, 0);
    rvk_pass_stage_global_image(aoPass, geoDepthRead, 1);
    rvk_pass_stage_attach_color(aoPass, aoBuffer, 0);
    painter_stage_global_data(&ctx, &camMat, &projMat, aoSize, time, RendViewType_Main);
    painter_push_ambient_occlusion(&ctx);

    rend_builder_pass_flush(builder);
    trace_end();
  } else {
    rvk_canvas_img_clear_color(painter->canvas, aoBuffer, geo_color_white);
  }

  // Forward pass.
  RvkPass*  fwdPass  = platform->passes[AssetGraphicPass_Forward];
  RvkImage* fwdColor = rvk_canvas_attach_acquire_color(painter->canvas, fwdPass, 0, geoSize);
  {
    trace_begin("rend_paint_forward", TraceColor_White);
    rend_builder_pass_push(builder, fwdPass);

    if (set->flags & RendFlags_DebugCamera && set->skyMode == RendSkyMode_None) {
      // NOTE: The debug camera-mode does not draw to the whole image; thus we need to clear it.
      rvk_canvas_img_clear_color(painter->canvas, fwdColor, geo_color_black);
    }
    RendPaintContext ctx = painter_context(painter->canvas, builder, set, time, fwdPass, mainView);
    if (ctx.settings->ambientMode >= RendAmbientMode_DebugStart) {
      // Disable lighting when using any of the debug ambient modes.
      ctx.view.filter.illegal |= SceneTags_Light;
    }
    rvk_pass_stage_global_image(fwdPass, geoData0, 0);
    rvk_pass_stage_global_image(fwdPass, geoData1, 1);
    rvk_pass_stage_global_image(fwdPass, geoDepthRead, 2);
    rvk_pass_stage_global_image(fwdPass, aoBuffer, 3);
    rvk_pass_stage_global_shadow(fwdPass, shadowDepth, 4);
    rvk_pass_stage_attach_color(fwdPass, fwdColor, 0);
    rvk_pass_stage_attach_depth(fwdPass, geoDepth);
    painter_stage_global_data(&ctx, &camMat, &projMat, geoSize, time, RendViewType_Main);
    painter_push_ambient(&ctx, rend_light_ambient_intensity(light));
    switch ((u32)set->skyMode) {
    case RendSkyMode_Gradient:
      painter_push_simple(&ctx, RvkRepositoryId_SkyGradientGraphic, mem_empty);
      break;
    case RendSkyMode_CubeMap:
      painter_push_simple(&ctx, RvkRepositoryId_SkyCubeMapGraphic, mem_empty);
      break;
    }
    if (geoTagMask & SceneTags_Selected) {
      painter_push_simple(&ctx, RvkRepositoryId_OutlineGraphic, mem_empty);
    }
    painter_push_objects_simple(&ctx, objView, resView, AssetGraphicPass_Forward);
    if (fogActive) {
      painter_push_fog(&ctx, fog, fogBuffer);
    }
    if (set->flags & RendFlags_DebugWireframe) {
      painter_push_debug_wireframe(&ctx, objView, resView);
    }
    if (set->flags & RendFlags_DebugSkinning) {
      painter_push_debug_skinning(&ctx, objView, resView);
    }

    rend_builder_pass_flush(builder);
    trace_end();
  }

  rvk_canvas_attach_release(painter->canvas, geoData0);
  rvk_canvas_attach_release(painter->canvas, geoData1);
  rvk_canvas_attach_release(painter->canvas, geoDepthRead);
  rvk_canvas_attach_release(painter->canvas, aoBuffer);

  // Distortion.
  const RvkSize distSize = set->flags & RendFlags_Distortion
                               ? rvk_size_scale(geoSize, set->distortionResolutionScale)
                               : (RvkSize){1, 1};
  RvkPass*      distPass = platform->passes[AssetGraphicPass_Distortion];
  RvkImage* distBuffer   = rvk_canvas_attach_acquire_color(painter->canvas, distPass, 0, distSize);
  if (set->flags & RendFlags_Distortion) {
    trace_begin("rend_paint_distortion", TraceColor_White);
    rend_builder_pass_push(builder, distPass);

    RvkImage* distDepth;
    if (distSize.data == geoSize.data) {
      distDepth = geoDepth;
    } else {
      distDepth = rvk_canvas_attach_acquire_depth(painter->canvas, distPass, distSize);
      rvk_canvas_img_blit(painter->canvas, geoDepth, distDepth);
    }

    RendPaintContext ctx = painter_context(painter->canvas, builder, set, time, distPass, mainView);
    rvk_pass_stage_attach_color(distPass, distBuffer, 0);
    rvk_pass_stage_attach_depth(distPass, distDepth);

    painter_stage_global_data(&ctx, &camMat, &projMat, distSize, time, RendViewType_Main);
    painter_push_objects_simple(&ctx, objView, resView, AssetGraphicPass_Distortion);

    rend_builder_pass_flush(builder);
    trace_end();

    if (distSize.data != geoSize.data) {
      rvk_canvas_attach_release(painter->canvas, distDepth);
    }
  } else {
    rvk_canvas_img_clear_color(painter->canvas, distBuffer, geo_color_black);
  }

  rvk_canvas_attach_release(painter->canvas, geoDepth);

  // Bloom pass.
  RvkPass*  bloomPass = platform->passes[AssetGraphicPass_Bloom];
  RvkImage* bloomOutput;
  if (set->flags & RendFlags_Bloom && set->bloomIntensity > f32_epsilon) {
    trace_begin("rend_paint_bloom", TraceColor_White);

    RendPaintContext ctx =
        painter_context(painter->canvas, builder, set, time, bloomPass, mainView);
    RvkSize   size = geoSize;
    RvkImage* images[6];
    diag_assert(set->bloomSteps <= array_elems(images));

    for (u32 i = 0; i != set->bloomSteps; ++i) {
      size      = rvk_size_scale(size, 0.5f);
      images[i] = rvk_canvas_attach_acquire_color(painter->canvas, bloomPass, 0, size);
    }

    struct {
      ALIGNAS(16)
      f32 filterRadius;
    } bloomData = {.filterRadius = set->bloomRadius};

    // Render down samples.
    for (u32 i = 0; i != set->bloomSteps; ++i) {
      rend_builder_pass_push(builder, bloomPass);
      rvk_pass_stage_global_image(bloomPass, i == 0 ? fwdColor : images[i - 1], 0);
      rvk_pass_stage_attach_color(bloomPass, images[i], 0);
      painter_push_simple(&ctx, RvkRepositoryId_BloomDownGraphic, mem_empty);
      rend_builder_pass_flush(builder);
    }

    // Render up samples.
    for (u32 i = set->bloomSteps; i-- > 1;) {
      rend_builder_pass_push(builder, bloomPass);
      rvk_pass_stage_global_image(bloomPass, images[i], 0);
      rvk_pass_stage_attach_color(bloomPass, images[i - 1], 0);
      painter_push_simple(&ctx, RvkRepositoryId_BloomUpGraphic, mem_var(bloomData));
      rend_builder_pass_flush(builder);
    }

    // Keep the largest image as the output, release the others.
    bloomOutput = images[0];
    for (u32 i = 1; i != set->bloomSteps; ++i) {
      rvk_canvas_attach_release(painter->canvas, images[i]);
    }
    trace_end();
  } else {
    bloomOutput = rvk_canvas_attach_acquire_color(painter->canvas, bloomPass, 0, (RvkSize){1, 1});
    rvk_canvas_img_clear_color(painter->canvas, bloomOutput, geo_color_white);
  }

  // Post pass.
  RvkPass*  postPass = platform->passes[AssetGraphicPass_Post];
  RvkImage* postRes  = rvk_canvas_attach_acquire_color(painter->canvas, postPass, 0, swapchainSize);
  {
    trace_begin("rend_paint_post", TraceColor_White);
    rend_builder_pass_push(builder, postPass);

    RendPaintContext ctx = painter_context(painter->canvas, builder, set, time, postPass, mainView);
    rvk_pass_stage_global_image(postPass, fwdColor, 0);
    rvk_pass_stage_global_image(postPass, bloomOutput, 1);
    rvk_pass_stage_global_image(postPass, distBuffer, 2);
    rvk_pass_stage_global_image(postPass, fogBuffer, 3);
    rvk_pass_stage_attach_color(postPass, postRes, 0);
    painter_stage_global_data(&ctx, &camMat, &projMat, swapchainSize, time, RendViewType_Main);
    painter_push_tonemapping(&ctx);
    painter_push_objects_simple(&ctx, objView, resView, AssetGraphicPass_Post);

    if (set->flags & RendFlags_DebugFog) {
      const f32 exposure = 1.0f;
      painter_push_debug_image_viewer(&ctx, fogBuffer, exposure);
    } else if (set->flags & RendFlags_DebugShadow) {
      const f32 exposure = 0.5f;
      painter_push_debug_image_viewer(&ctx, shadowDepth, exposure);
    } else if (set->flags & RendFlags_DebugDistortion) {
      const f32 exposure = 100.0f;
      painter_push_debug_image_viewer(&ctx, distBuffer, exposure);
    } else if (set->debugViewerResource) {
      painter_push_debug_resource_viewer(world, &ctx, winAspect, resView, set->debugViewerResource);
    }

    rend_builder_pass_flush(builder);
    trace_end();

    // TODO: Render into the swapchain directly if the swapchain format matches the pass format.
    rvk_canvas_img_blit(painter->canvas, postRes, swapchainImage);
    rvk_canvas_attach_release(painter->canvas, postRes);
  }

  rvk_canvas_attach_release(painter->canvas, fogBuffer);
  rvk_canvas_attach_release(painter->canvas, fwdColor);
  rvk_canvas_attach_release(painter->canvas, shadowDepth);
  rvk_canvas_attach_release(painter->canvas, bloomOutput);
  rvk_canvas_attach_release(painter->canvas, distBuffer);

  // Finish the frame.
  trace_end();
  rvk_canvas_end(painter->canvas);
  return true;
}

ecs_system_define(RendPainterCreateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  RendPlatformComp* plat = ecs_view_write_t(globalItr, RendPlatformComp);

  EcsView* painterView = ecs_world_view_t(world, PainterCreateView);
  for (EcsIterator* itr = ecs_view_itr(painterView); ecs_view_walk(itr);) {
    const EcsEntityId    entity = ecs_view_entity(itr);
    const GapWindowComp* win    = ecs_view_read_t(itr, GapWindowComp);
    if (gap_window_events(win) & GapWindowEvents_Initializing) {
      continue;
    }
    const bool            hasCam = ecs_world_has_t(world, entity, SceneCameraComp);
    const RendPainterType type   = hasCam ? RendPainterType_3D : RendPainterType_2D;

    RendPainterComp* p = ecs_world_add_t(world, entity, RendPainterComp, .type = type);
    switch (type) {
    case RendPainterType_2D:
      p->canvas = rvk_canvas_create(plat->device, win, &plat->passes[AssetGraphicPass_Post], 1);
      break;
    case RendPainterType_3D:
      p->canvas = rvk_canvas_create(plat->device, win, plat->passes, AssetGraphicPass_Count);
      break;
    }

    if (!ecs_world_has_t(world, entity, RendSettingsComp)) {
      RendSettingsComp* settings = ecs_world_add_t(world, entity, RendSettingsComp);
      rend_settings_to_default(settings);
    }
  }
}

ecs_system_define(RendPainterDrawSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  RendPlatformComp*            platform = ecs_view_write_t(globalItr, RendPlatformComp);
  const SceneTimeComp*         time     = ecs_view_read_t(globalItr, SceneTimeComp);
  const RendLightRendererComp* light    = ecs_view_read_t(globalItr, RendLightRendererComp);
  const RendFogComp*           fog      = ecs_view_read_t(globalItr, RendFogComp);

  EcsView* painterView = ecs_world_view_t(world, PainterUpdateView);
  EcsView* objView     = ecs_world_view_t(world, ObjView);
  EcsView* resView     = ecs_world_view_t(world, ResourceView);

  for (EcsIterator* itr = ecs_view_itr(painterView); ecs_view_walk(itr);) {
    const EcsEntityId         entity   = ecs_view_entity(itr);
    const GapWindowComp*      win      = ecs_view_read_t(itr, GapWindowComp);
    RendPainterComp*          painter  = ecs_view_write_t(itr, RendPainterComp);
    const RendSettingsComp*   settings = ecs_view_read_t(itr, RendSettingsComp);
    const SceneCameraComp*    cam      = ecs_view_read_t(itr, SceneCameraComp);
    const SceneTransformComp* camTrans = ecs_view_read_t(itr, SceneTransformComp);

    switch (painter->type) {
    case RendPainterType_2D:
      rend_canvas_paint_2d(painter, platform, settings, time, win, entity, objView, resView);
      break;
    case RendPainterType_3D:
      rend_canvas_paint_3d(
          world,
          painter,
          platform,
          settings,
          time,
          light,
          fog,
          win,
          entity,
          cam,
          camTrans,
          objView,
          resView);
      break;
    }
  }
}

ecs_module_init(rend_painter_module) {
  ecs_register_comp(RendPainterComp, .destructor = ecs_destruct_painter);

  ecs_register_view(GlobalView);
  ecs_register_view(ObjView);
  ecs_register_view(ResourceView);
  ecs_register_view(PainterCreateView);
  ecs_register_view(PainterUpdateView);

  ecs_register_system(
      RendPainterCreateSys, ecs_view_id(GlobalView), ecs_view_id(PainterCreateView));

  ecs_register_system(
      RendPainterDrawSys,
      ecs_view_id(GlobalView),
      ecs_view_id(PainterUpdateView),
      ecs_view_id(ObjView),
      ecs_view_id(ResourceView));

  ecs_order(RendPainterDrawSys, RendOrder_Draw);
}

void rend_painter_teardown(EcsWorld* world, const EcsEntityId entity) {
  ecs_world_remove_t(world, entity, RendPainterComp);
}
