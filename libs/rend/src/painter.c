#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "ecs_entity.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "log_logger.h"
#include "rend_register.h"
#include "scene_camera.h"
#include "scene_time.h"
#include "scene_transform.h"

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
#include "rvk/mesh_internal.h"
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
  RendBuilder*            builder;
  const RendSettingsComp* settings;
  const SceneTimeComp*    time;
  RendView                view;
} RendPaintContext;

static RendPaintContext painter_context(
    RendBuilder*            builder,
    const RendSettingsComp* settings,
    const SceneTimeComp*    time,
    const RendView          view) {

  return (RendPaintContext){
      .builder  = builder,
      .settings = settings,
      .time     = time,
      .view     = view,
  };
}

typedef enum {
  RendViewType_Main,
  RendViewType_Shadow,
  RendViewType_Fog,
} RendViewType;

static void painter_set_global_data(
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

  const u32              dataSize = sizeof(RendPainterGlobalData);
  RendPainterGlobalData* data     = rend_builder_global_data(ctx->builder, dataSize, 0).ptr;

  *data = (RendPainterGlobalData){
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

    data->viewInv     = geo_matrix_rotate_x(math_pi_f32 * 0.5f);
    data->view        = geo_matrix_inverse(&data->viewInv);
    data->proj        = geo_matrix_proj_ortho_hor(g_size, aspect, g_depthMin, g_depthMax);
    data->projInv     = geo_matrix_inverse(&data->proj);
    data->viewProj    = geo_matrix_mul(&data->proj, &data->view);
    data->viewProjInv = geo_matrix_inverse(&data->viewProj);
    data->camPosition = geo_vector(0, 0, 0);
    data->camRotation = geo_quat_forward_to_down;
  } else {
    data->viewInv     = *cameraMatrix;
    data->view        = geo_matrix_inverse(cameraMatrix);
    data->proj        = *projMatrix;
    data->projInv     = geo_matrix_inverse(projMatrix);
    data->viewProj    = geo_matrix_mul(&data->proj, &data->view);
    data->viewProjInv = geo_matrix_inverse(&data->viewProj);
    data->camPosition = geo_matrix_to_translation(cameraMatrix);
    data->camRotation = geo_matrix_to_quat(cameraMatrix);
  }
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
  const RvkRepository* repo    = rend_builder_repository(ctx->builder);
  const RvkGraphic*    graphic = rvk_repository_graphic_get(repo, id);
  if (graphic) {
    rend_builder_draw_push(ctx->builder, graphic);
    if (data.size) {
      mem_cpy(rend_builder_draw_data(ctx->builder, (u32)data.size), data);
    }
    rend_builder_draw_instances(ctx->builder, 0 /* dataStride */, 1 /* count */);
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
      rend_builder_draw_image_frozen(ctx->builder, &texture->image);
    }
    rend_object_draw(obj, &ctx->view, ctx->settings, ctx->builder);
    rend_builder_draw_flush(ctx->builder);

    tagMask |= rend_object_tag_mask(obj);
  }

  return tagMask;
}

static void painter_push_shadow(RendPaintContext* ctx, EcsView* objView, EcsView* resView) {
  const RvkRepository* repo     = rend_builder_repository(ctx->builder);
  const RvkTexture*    whiteTex = rvk_repository_texture_get(repo, RvkRepositoryId_WhiteTexture);
  if (!whiteTex) {
    return; // Texture not loaded (yet).
  }
  EcsIterator* resourceItr = ecs_view_itr(resView);
  for (EcsIterator* objItr = ecs_view_itr(objView); ecs_view_walk(objItr);) {
    const RendObjectComp* obj = ecs_view_read_t(objItr, RendObjectComp);
    if (!rend_object_instance_count(obj)) {
      continue; // Object has no instances.
    }
    const EcsEntityId graphicRes = rend_object_resource(obj, RendObjectRes_GraphicShadow);
    if (!graphicRes) {
      continue; // Object has no shadow graphic.
    }
    const RvkGraphic* graphic = painter_get_graphic(resourceItr, graphicRes);
    if (!graphic) {
      continue; // Shadow graphic is not loaded.
    }
    if (UNLIKELY(graphic->passId != AssetGraphicPass_Shadow)) {
      log_e("Shadow's can only be drawn from the shadow pass");
      continue;
    }

    const EcsEntityId graphicOrgRes = rend_object_resource(obj, RendObjectRes_Graphic);
    const RvkGraphic* graphicOrg    = painter_get_graphic(resourceItr, graphicOrgRes);
    if (!graphicOrg) {
      continue; // Graphic is not loaded.
    }

    rend_builder_draw_push(ctx->builder, graphic);
    rend_builder_draw_mesh(ctx->builder, graphicOrg->mesh);

    const RvkTexture* alphaTex;
    const u8          alphaTexIndex = rend_object_alpha_tex_index(obj);
    if (sentinel_check(alphaTexIndex) || !(graphicOrg->samplerMask & (1 << alphaTexIndex))) {
      alphaTex = whiteTex;
    } else {
      alphaTex = graphicOrg->samplerTextures[alphaTexIndex];
    }
    rend_builder_draw_image_frozen(ctx->builder, &alphaTex->image);
    rend_builder_draw_sampler(ctx->builder, (RvkSamplerSpec){.aniso = RvkSamplerAniso_x8});

    rend_object_draw(obj, &ctx->view, ctx->settings, ctx->builder);
    rend_builder_draw_flush(ctx->builder);
  }
}

static void painter_push_fog(RendPaintContext* ctx, const RendFogComp* fog, RvkImage* fogMap) {
  const RvkRepository* repo    = rend_builder_repository(ctx->builder);
  const RvkGraphic*    graphic = rvk_repository_graphic_get(repo, RvkRepositoryId_FogGraphic);
  if (graphic) {
    struct {
      ALIGNAS(16)
      GeoMatrix fogViewProj;
    } data;

    const GeoMatrix fogViewMat = geo_matrix_inverse(rend_fog_trans(fog));
    data.fogViewProj           = geo_matrix_mul(rend_fog_proj(fog), &fogViewMat);

    rend_builder_draw_push(ctx->builder, graphic);
    mem_cpy(rend_builder_draw_data(ctx->builder, sizeof(data)), mem_var(data));
    rend_builder_draw_image(ctx->builder, fogMap);
    rend_builder_draw_instances(ctx->builder, 0 /* dataStride */, 1 /* count */);
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
  const RvkRepository* repo = rend_builder_repository(ctx->builder);
  const RvkGraphic*    graphic;
  switch (image->type) {
  case RvkImageType_ColorSourceArray:
    graphic = rvk_repository_graphic_get(repo, RvkRepositoryId_DebugImageViewerArrayGraphic);
    break;
  case RvkImageType_ColorSourceCube:
    graphic = rvk_repository_graphic_get(repo, RvkRepositoryId_DebugImageViewerCubeGraphic);
    break;
  default:
    graphic = rvk_repository_graphic_get(repo, RvkRepositoryId_DebugImageViewerGraphic);
    break;
  }
  if (graphic) {
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

    struct {
      ALIGNAS(16)
      u32 flags;
      u32 imageChannels;
      f32 lod;
      f32 layer;
      f32 exposure;
      f32 aspect;
    } data = {
        .flags         = flags,
        .imageChannels = vkFormatComponents(image->vkFormat),
        .lod           = ctx->settings->debugViewerLod,
        .layer         = ctx->settings->debugViewerLayer,
        .exposure      = exposure,
        .aspect        = (f32)image->size.width / (f32)image->size.height,
    };

    rend_builder_draw_push(ctx->builder, graphic);
    mem_cpy(rend_builder_draw_data(ctx->builder, sizeof(data)), mem_var(data));

    RvkSamplerSpec sampler = {.filter = RvkSamplerFilter_Nearest};
    if (ctx->settings->debugViewerFlags & RendDebugViewer_Interpolate) {
      sampler.filter = RvkSamplerFilter_Linear;
    }
    rend_builder_draw_image(ctx->builder, image);
    rend_builder_draw_sampler(ctx->builder, sampler);
    rend_builder_draw_instances(ctx->builder, 0 /* dataStride */, 1 /* count */);
    rend_builder_draw_flush(ctx->builder);
  }
}

static void
painter_push_debug_mesh_viewer(RendPaintContext* ctx, const f32 aspect, const RvkMesh* mesh) {
  const RvkRepository*  repo      = rend_builder_repository(ctx->builder);
  const RvkRepositoryId graphicId = RvkRepositoryId_DebugMeshViewerGraphic;
  const RvkGraphic*     graphic   = rvk_repository_graphic_get(repo, graphicId);
  if (graphic) {
    const GeoVector meshCenter = geo_box_center(&mesh->bounds);
    const f32       meshSize   = math_max(1.0f, geo_box_size(&mesh->bounds).y);

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

    struct {
      ALIGNAS(16)
      GeoMatrix viewProj;
    } data = {.viewProj = geo_matrix_mul(&projMat, &viewMat)};

    rend_builder_draw_push(ctx->builder, graphic);
    mem_cpy(rend_builder_draw_data(ctx->builder, sizeof(data)), mem_var(data));
    rend_builder_draw_mesh(ctx->builder, mesh);
    rend_builder_draw_instances(ctx->builder, 0 /* dataStride */, 1 /* count */);
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
      diag_assert(textureComp->texture->image.frozen);
      // NOTE: The following cast is questionable but safe as frozen images are fully immutable.
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
    const EcsEntityId graphicRes = rend_object_resource(obj, RendObjectRes_GraphicDebugWireframe);
    if (!graphicRes) {
      continue; // Object has no debug wireframe graphic.
    }
    const RvkGraphic* graphic = painter_get_graphic(resourceItr, graphicRes);
    if (!graphic) {
      continue; // Wireframe graphic is not loaded.
    }
    if (UNLIKELY(graphic->passId != AssetGraphicPass_Forward)) {
      log_e("Debug-wireframe can only be drawn from the forward pass");
      continue;
    }

    const EcsEntityId graphicOrgRes = rend_object_resource(obj, RendObjectRes_Graphic);
    const RvkGraphic* graphicOrg    = painter_get_graphic(resourceItr, graphicOrgRes);
    if (!graphicOrg) {
      continue; // Graphic is not loaded.
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
      rend_builder_draw_image_frozen(ctx->builder, &texture->image);
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
    const EcsEntityId graphicRes = rend_object_resource(obj, RendObjectRes_GraphicDebugSkinning);
    if (!graphicRes) {
      continue; // Object has no debug skinning graphic.
    }
    const RvkGraphic* graphic = painter_get_graphic(resourceItr, graphicRes);
    if (!graphic) {
      continue; // Skinning graphic is not loaded.
    }
    if (UNLIKELY(graphic->passId != AssetGraphicPass_Forward)) {
      log_e("Debug-skinning can only be drawn from the forward pass");
      continue;
    }

    const EcsEntityId graphicOrgRes = rend_object_resource(obj, RendObjectRes_Graphic);
    const RvkGraphic* graphicOrg    = painter_get_graphic(resourceItr, graphicOrgRes);
    if (!graphicOrg) {
      continue; // Graphic is not loaded.
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

  RendBuilder* b = rend_builder(platform->builderContainer);
  if (!rend_builder_canvas_push(b, painter->canvas, set, time->frameIdx, painter_win_size(win))) {
    return false; // Canvas not ready for rendering.
  }

  rend_builder_phase_output(b); // Acquire swapchain image.

  RvkImage* swapchainImage = rend_builder_img_swapchain(b);
  if (swapchainImage) {
    rend_builder_img_clear_color(b, swapchainImage, geo_color_black);

    rend_builder_pass_push(b, platform->passes[AssetGraphicPass_Post]);
    {
      const RendView   mainView = painter_view_2d_create(camEntity);
      RendPaintContext ctx      = painter_context(b, set, time, mainView);
      rend_builder_attach_color(b, swapchainImage, 0);
      painter_push_objects_simple(&ctx, objView, resView, AssetGraphicPass_Post);
    }
    rend_builder_pass_flush(b);
  }

  rend_builder_canvas_flush(b);
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
  const f32     winAspect = winSize.height ? ((f32)winSize.width / (f32)winSize.height) : 1.0f;

  RendBuilder* b = rend_builder(platform->builderContainer);
  if (!rend_builder_canvas_push(b, painter->canvas, set, time->frameIdx, winSize)) {
    return false; // Canvas not ready for rendering.
  }
  const GeoMatrix camMat   = camTrans ? scene_transform_matrix(camTrans) : geo_matrix_ident();
  const GeoMatrix projMat  = scene_camera_proj(cam, winAspect);
  const RendView  mainView = painter_view_3d_create(&camMat, &projMat, camEntity, cam->filter);

  // Geometry pass.
  const RvkSize geoSize      = rvk_size_scale(winSize, set->resolutionScale);
  RvkPass*      geoPass      = platform->passes[AssetGraphicPass_Geometry];
  RvkImage*     geoBase      = rend_builder_attach_acquire_color(b, geoPass, 0, geoSize);
  RvkImage*     geoNormal    = rend_builder_attach_acquire_color(b, geoPass, 1, geoSize);
  RvkImage*     geoAttribute = rend_builder_attach_acquire_color(b, geoPass, 2, geoSize);
  RvkImage*     geoEmissive  = rend_builder_attach_acquire_color(b, geoPass, 3, geoSize);
  RvkImage*     geoDepth     = rend_builder_attach_acquire_depth(b, geoPass, geoSize);
  SceneTags     geoTagMask;
  {
    rend_builder_pass_push(b, geoPass);

    RendPaintContext ctx = painter_context(b, set, time, mainView);
    rend_builder_attach_color(b, geoBase, 0);
    rend_builder_attach_color(b, geoNormal, 1);
    rend_builder_attach_color(b, geoAttribute, 2);
    rend_builder_attach_color(b, geoEmissive, 3);
    rend_builder_attach_depth(b, geoDepth);
    painter_set_global_data(&ctx, &camMat, &projMat, geoSize, time, RendViewType_Main);
    geoTagMask = painter_push_objects_simple(&ctx, objView, resView, AssetGraphicPass_Geometry);

    rend_builder_pass_flush(b);
  }

  // Make a copy of the geometry depth to read from while still writing to the original.
  // TODO: Instead of a straight copy considering performing linearization at the same time.
  RvkImage* geoDepthRead = rend_builder_attach_acquire_copy(b, geoDepth);

  // Decal pass.
  if (set->flags & RendFlags_Decals) {
    rend_builder_pass_push(b, platform->passes[AssetGraphicPass_Decal]);

    // Copy the gbufer base and normal images to be able to read during the decal pass.
    RvkImage* geoBaseCpy   = rend_builder_attach_acquire_copy(b, geoBase);
    RvkImage* geoNormalCpy = rend_builder_attach_acquire_copy(b, geoNormal);

    RendPaintContext ctx = painter_context(b, set, time, mainView);
    rend_builder_global_image(b, geoBaseCpy, 0);
    rend_builder_global_image(b, geoNormalCpy, 1);
    rend_builder_global_image(b, geoDepthRead, 2);
    rend_builder_attach_color(b, geoBase, 0);
    rend_builder_attach_color(b, geoNormal, 1);
    rend_builder_attach_color(b, geoAttribute, 2);
    rend_builder_attach_color(b, geoEmissive, 3);
    rend_builder_attach_depth(b, geoDepth);
    painter_set_global_data(&ctx, &camMat, &projMat, geoSize, time, RendViewType_Main);
    painter_push_objects_simple(&ctx, objView, resView, AssetGraphicPass_Decal);

    rend_builder_pass_flush(b);
    rend_builder_attach_release(b, geoBaseCpy);
    rend_builder_attach_release(b, geoNormalCpy);
  }

  // Fog pass.
  const bool    fogActive = rend_fog_active(fog);
  RvkPass*      fogPass   = platform->passes[AssetGraphicPass_Fog];
  const RvkSize fogSize   = fogActive ? rvk_size_square(set->fogResolution) : rvk_size_one;
  RvkImage*     fogBuffer = rend_builder_attach_acquire_color(b, fogPass, 0, fogSize);
  if (fogActive) {
    rend_builder_pass_push(b, fogPass);

    const GeoMatrix*     fogTrans  = rend_fog_trans(fog);
    const GeoMatrix*     fogProj   = rend_fog_proj(fog);
    const SceneTagFilter fogFilter = {0};
    const RendView       fogView = painter_view_3d_create(fogTrans, fogProj, camEntity, fogFilter);

    RendPaintContext ctx = painter_context(b, set, time, fogView);
    rend_builder_attach_color(b, fogBuffer, 0);
    painter_set_global_data(&ctx, fogTrans, fogProj, fogSize, time, RendViewType_Fog);
    painter_push_objects_simple(&ctx, objView, resView, AssetGraphicPass_Fog);

    rend_builder_pass_flush(b);
  } else {
    rend_builder_img_clear_color(b, fogBuffer, geo_color_white);
  }

  // Fog-blur pass.
  if (fogActive && set->fogBlurSteps) {
    RendPaintContext ctx = painter_context(b, set, time, mainView);

    struct {
      ALIGNAS(16)
      f32 sampleScale;
    } blurData = {.sampleScale = set->fogBlurScale};

    RvkImage* tmp = rend_builder_attach_acquire_copy_uninit(b, fogBuffer);
    for (u32 i = 0; i != set->fogBlurSteps; ++i) {
      // Horizontal pass.
      rend_builder_pass_push(b, platform->passes[AssetGraphicPass_FogBlur]);
      rend_builder_global_image(b, fogBuffer, 0);
      rend_builder_attach_color(b, tmp, 0);
      painter_push_simple(&ctx, RvkRepositoryId_FogBlurHorGraphic, mem_var(blurData));
      rend_builder_pass_flush(b);

      // Vertical pass.
      rend_builder_pass_push(b, platform->passes[AssetGraphicPass_FogBlur]);
      rend_builder_global_image(b, tmp, 0);
      rend_builder_attach_color(b, fogBuffer, 0);
      painter_push_simple(&ctx, RvkRepositoryId_FogBlurVerGraphic, mem_var(blurData));
      rend_builder_pass_flush(b);
    }
    rend_builder_attach_release(b, tmp);
  }

  // Shadow pass.
  const bool    shadActive = (set->flags & RendFlags_Shadows) && rend_light_has_shadow(light);
  const RvkSize shadSize   = shadActive ? rvk_size_square(set->shadowResolution) : rvk_size_one;
  RvkPass*      shadPass   = platform->passes[AssetGraphicPass_Shadow];
  RvkImage*     shadDepth  = rend_builder_attach_acquire_depth(b, shadPass, shadSize);
  if (shadActive) {
    rend_builder_pass_push(b, shadPass);

    const GeoMatrix* shadTrans  = rend_light_shadow_trans(light);
    const GeoMatrix* shadProj   = rend_light_shadow_proj(light);
    SceneTagFilter   shadFilter = {
          .required = cam->filter.required | SceneTags_ShadowCaster,
          .illegal  = cam->filter.illegal,
    };
    if (!(set->flags & RendFlags_VfxShadows)) {
      shadFilter.illegal |= SceneTags_Vfx;
    }
    const RendView   shadView = painter_view_3d_create(shadTrans, shadProj, camEntity, shadFilter);
    RendPaintContext ctx      = painter_context(b, set, time, shadView);
    rend_builder_attach_depth(b, shadDepth);
    painter_set_global_data(&ctx, shadTrans, shadProj, shadSize, time, RendViewType_Shadow);
    painter_push_shadow(&ctx, objView, resView);

    rend_builder_pass_flush(b);
  } else {
    rend_builder_img_clear_depth(b, shadDepth, 0);
  }

  // Ambient occlusion.
  const bool    aoActive = (set->flags & RendFlags_AmbientOcclusion) != 0;
  const RvkSize aoSize = aoActive ? rvk_size_scale(geoSize, set->aoResolutionScale) : rvk_size_one;
  RvkPass*      aoPass = platform->passes[AssetGraphicPass_AmbientOcclusion];
  RvkImage*     aoBuffer = rend_builder_attach_acquire_color(b, aoPass, 0, aoSize);
  if (aoActive) {
    rend_builder_pass_push(b, aoPass);

    RendPaintContext ctx = painter_context(b, set, time, mainView);
    rend_builder_global_image(b, geoNormal, 0);
    rend_builder_global_image(b, geoDepthRead, 1);
    rend_builder_attach_color(b, aoBuffer, 0);
    painter_set_global_data(&ctx, &camMat, &projMat, aoSize, time, RendViewType_Main);
    painter_push_ambient_occlusion(&ctx);

    rend_builder_pass_flush(b);
  } else {
    rend_builder_img_clear_color(b, aoBuffer, geo_color_white);
  }

  // Forward pass.
  RvkPass*  fwdPass  = platform->passes[AssetGraphicPass_Forward];
  RvkImage* fwdColor = rend_builder_attach_acquire_color(b, fwdPass, 0, geoSize);
  {
    rend_builder_pass_push(b, fwdPass);

    if (set->flags & RendFlags_DebugCamera && set->skyMode == RendSkyMode_None) {
      // NOTE: The debug camera-mode does not draw to the whole image; thus we need to clear it.
      rend_builder_img_clear_color(b, fwdColor, geo_color_black);
    }
    RendPaintContext ctx = painter_context(b, set, time, mainView);
    if (ctx.settings->ambientMode >= RendAmbientMode_DebugStart) {
      // Disable lighting when using any of the debug ambient modes.
      ctx.view.filter.illegal |= SceneTags_Light;
    }
    rend_builder_global_image(b, geoBase, 0);
    rend_builder_global_image(b, geoNormal, 1);
    rend_builder_global_image(b, geoAttribute, 2);
    rend_builder_global_image(b, geoEmissive, 3);
    rend_builder_global_image(b, geoDepthRead, 4);
    rend_builder_global_image(b, aoBuffer, 5);
    rend_builder_global_shadow(b, shadDepth, 6);
    rend_builder_attach_color(b, fwdColor, 0);
    rend_builder_attach_depth(b, geoDepth);
    painter_set_global_data(&ctx, &camMat, &projMat, geoSize, time, RendViewType_Main);
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

    rend_builder_pass_flush(b);
  }

  rend_builder_attach_release(b, geoBase);
  rend_builder_attach_release(b, geoNormal);
  rend_builder_attach_release(b, geoAttribute);
  rend_builder_attach_release(b, geoEmissive);
  rend_builder_attach_release(b, geoDepthRead);
  rend_builder_attach_release(b, aoBuffer);

  // Distortion.
  const bool    distActive = (set->flags & RendFlags_Distortion) != 0;
  const f32     distScale  = set->distortionResolutionScale;
  const RvkSize distSize   = distActive ? rvk_size_scale(geoSize, distScale) : rvk_size_one;
  RvkPass*      distPass   = platform->passes[AssetGraphicPass_Distortion];
  RvkImage*     distBuffer = rend_builder_attach_acquire_color(b, distPass, 0, distSize);
  if (distActive) {
    rend_builder_pass_push(b, distPass);

    RvkImage* distDepth;
    if (distSize.data == geoSize.data) {
      distDepth = geoDepth;
    } else {
      distDepth = rend_builder_attach_acquire_depth(b, distPass, distSize);
      rend_builder_img_blit(b, geoDepth, distDepth);
    }

    RendPaintContext ctx = painter_context(b, set, time, mainView);
    rend_builder_attach_color(b, distBuffer, 0);
    rend_builder_attach_depth(b, distDepth);
    painter_set_global_data(&ctx, &camMat, &projMat, distSize, time, RendViewType_Main);
    painter_push_objects_simple(&ctx, objView, resView, AssetGraphicPass_Distortion);
    rend_builder_pass_flush(b);

    if (distSize.data != geoSize.data) {
      rend_builder_attach_release(b, distDepth);
    }
  } else {
    rend_builder_img_clear_color(b, distBuffer, geo_color_black);
  }

  rend_builder_attach_release(b, geoDepth);

  // Bloom pass.
  RvkPass*  bloomPass = platform->passes[AssetGraphicPass_Bloom];
  RvkImage* bloomOutput;
  if ((set->flags & RendFlags_Bloom) && set->bloomIntensity > f32_epsilon) {
    RendPaintContext ctx  = painter_context(b, set, time, mainView);
    RvkSize          size = geoSize;
    RvkImage*        images[6];
    diag_assert(set->bloomSteps <= array_elems(images));

    for (u32 i = 0; i != set->bloomSteps; ++i) {
      size      = rvk_size_scale(size, 0.5f);
      images[i] = rend_builder_attach_acquire_color(b, bloomPass, 0, size);
    }

    struct {
      ALIGNAS(16)
      f32 filterRadius;
    } bloomData = {.filterRadius = set->bloomRadius};

    // Render down samples.
    for (u32 i = 0; i != set->bloomSteps; ++i) {
      rend_builder_pass_push(b, bloomPass);
      rend_builder_global_image(b, i == 0 ? fwdColor : images[i - 1], 0);
      rend_builder_attach_color(b, images[i], 0);
      painter_push_simple(&ctx, RvkRepositoryId_BloomDownGraphic, mem_empty);
      rend_builder_pass_flush(b);
    }

    // Render up samples.
    for (u32 i = set->bloomSteps; i-- > 1;) {
      rend_builder_pass_push(b, bloomPass);
      rend_builder_global_image(b, images[i], 0);
      rend_builder_attach_color(b, images[i - 1], 0);
      painter_push_simple(&ctx, RvkRepositoryId_BloomUpGraphic, mem_var(bloomData));
      rend_builder_pass_flush(b);
    }

    // Keep the largest image as the output, release the others.
    bloomOutput = images[0];
    for (u32 i = 1; i != set->bloomSteps; ++i) {
      rend_builder_attach_release(b, images[i]);
    }
  } else {
    bloomOutput = rend_builder_attach_acquire_color(b, bloomPass, 0, rvk_size_one);
    rend_builder_img_clear_color(b, bloomOutput, geo_color_white);
  }

  rend_builder_phase_output(b); // Acquire swapchain image.

  // Post pass.
  RvkImage* swapchainImage = rend_builder_img_swapchain(b);
  if (swapchainImage) {
    rend_builder_pass_push(b, platform->passes[AssetGraphicPass_Post]);

    RendPaintContext ctx = painter_context(b, set, time, mainView);
    rend_builder_global_image(b, fwdColor, 0);
    rend_builder_global_image(b, bloomOutput, 1);
    rend_builder_global_image(b, distBuffer, 2);
    rend_builder_global_image(b, fogBuffer, 3);
    rend_builder_attach_color(b, swapchainImage, 0);
    painter_set_global_data(&ctx, &camMat, &projMat, winSize, time, RendViewType_Main);
    painter_push_tonemapping(&ctx);
    painter_push_objects_simple(&ctx, objView, resView, AssetGraphicPass_Post);
    if (set->flags & RendFlags_DebugFog) {
      const f32 exposure = 1.0f;
      painter_push_debug_image_viewer(&ctx, fogBuffer, exposure);
    } else if (set->flags & RendFlags_DebugShadow) {
      const f32 exposure = 0.5f;
      painter_push_debug_image_viewer(&ctx, shadDepth, exposure);
    } else if (set->flags & RendFlags_DebugDistortion) {
      const f32 exposure = 100.0f;
      painter_push_debug_image_viewer(&ctx, distBuffer, exposure);
    } else if (set->debugViewerResource) {
      painter_push_debug_resource_viewer(world, &ctx, winAspect, resView, set->debugViewerResource);
    }
    rend_builder_pass_flush(b);
  }

  rend_builder_attach_release(b, fogBuffer);
  rend_builder_attach_release(b, fwdColor);
  rend_builder_attach_release(b, shadDepth);
  rend_builder_attach_release(b, bloomOutput);
  rend_builder_attach_release(b, distBuffer);

  rend_builder_canvas_flush(b);
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
    ecs_world_add_t(
        world, entity, RendPainterComp, .canvas = rvk_canvas_create(plat->lib, plat->device, win));

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

    if (cam) {
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
    } else {
      rend_canvas_paint_2d(painter, platform, settings, time, win, entity, objView, resView);
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
