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

typedef enum {
  RendPainter2DPass_Post,

  RendPainter2DPass_Count,
} RendPainter2DPass;

typedef enum {
  RendPainter3DPass_Geometry,
  RendPainter3DPass_Decal,
  RendPainter3DPass_Fog,
  RendPainter3DPass_FogBlur,
  RendPainter3DPass_Shadow,
  RendPainter3DPass_AmbientOcclusion,
  RendPainter3DPass_Forward,
  RendPainter3DPass_Distortion,
  RendPainter3DPass_Bloom,
  RendPainter3DPass_Post,

  RendPainter3DPass_Count,
} RendPainter3DPass;

// clang-format off

static const RvkPassConfig g_passConfig2D[RendPainter2DPass_Count] = {
    [RendPainter2DPass_Post] = { .name = string_static("Post"),
        // Attachment color 0: color (rgba).
        .attachColorFormat[0] = RvkPassFormat_Swapchain,
        .attachColorLoad[0]   = RvkPassLoad_DontCare,
    },
};

static const RvkPassConfig g_passConfig3D[RendPainter3DPass_Count] = {
    [RendPainter3DPass_Geometry] = { .name = string_static("Geometry"),
        // Attachment depth.
        .attachDepth     = RvkPassDepth_Stored,
        .attachDepthLoad = RvkPassLoad_Clear,

        // Attachment color 0: color (rgb) and emissive (a).
        .attachColorFormat[0] = RvkPassFormat_Color4Srgb,
        .attachColorLoad[0]   = RvkPassLoad_DontCare,

        // Attachment color 1: normal (rg), roughness (b) and tags (a).
        .attachColorFormat[1] = RvkPassFormat_Color4Linear,
        .attachColorLoad[1]   = RvkPassLoad_DontCare,
    },

    [RendPainter3DPass_Decal] = { .name = string_static("Decal"),
        // Attachment depth.
        .attachDepth     = RvkPassDepth_Stored,
        .attachDepthLoad = RvkPassLoad_Preserve,

        // Attachment color 0: color (rgb) and emissive (a).
        .attachColorFormat[0] = RvkPassFormat_Color4Srgb,
        .attachColorLoad[0]   = RvkPassLoad_Preserve,

        // Attachment color 1: normal (rg), roughness (b) and tags (a).
        .attachColorFormat[1] = RvkPassFormat_Color4Linear,
        .attachColorLoad[1]   = RvkPassLoad_Preserve,
    },

    [RendPainter3DPass_Fog] = { .name = string_static("Fog"),
        // Attachment color 0: vision (r).
        .attachColorFormat[0] = RvkPassFormat_Color1Linear,
        .attachColorLoad[0]   = RvkPassLoad_Clear,
    },

    [RendPainter3DPass_FogBlur] = { .name = string_static("FogBlur"),
        // Attachment color 0: vision (r).
        .attachColorFormat[0] = RvkPassFormat_Color1Linear,
        .attachColorLoad[0]   = RvkPassLoad_PreserveDontCheck,
    },

    [RendPainter3DPass_Shadow] = { .name = string_static("Shadow"),
        // Attachment depth.
        .attachDepth     = RvkPassDepth_Stored,
        .attachDepthLoad = RvkPassLoad_Clear,
    },

    [RendPainter3DPass_AmbientOcclusion] = { .name = string_static("AmbientOcclusion"),
        // Attachment color 0: occlusion (r).
        .attachColorFormat[0] = RvkPassFormat_Color1Linear,
        .attachColorLoad[0]   = RvkPassLoad_DontCare,
    },

    [RendPainter3DPass_Forward] = { .name = string_static("Forward"),
        // Attachment depth.
        .attachDepth     = RvkPassDepth_Stored, // Stored as Distortion still needs the depth.
        .attachDepthLoad = RvkPassLoad_Preserve,

        // Attachment color 0: color (rgb).
        .attachColorFormat[0] = RvkPassFormat_Color3Float,
        .attachColorLoad[0]   = RvkPassLoad_DontCare,
    },

    [RendPainter3DPass_Distortion] = { .name = string_static("Distortion"),
        // Attachment depth.
        .attachDepth     = RvkPassDepth_Transient,
        .attachDepthLoad = RvkPassLoad_Preserve,

        // Attachment color 0: distortion-offset(rg).
        .attachColorFormat[0] = RvkPassFormat_Color2SignedFloat,
        .attachColorLoad[0]   = RvkPassLoad_Clear,
    },

    [RendPainter3DPass_Bloom] = { .name = string_static("Bloom"),
        // Attachment color 0: bloom (rgb).
        .attachColorFormat[0] = RvkPassFormat_Color3Float,
        .attachColorLoad[0]   = RvkPassLoad_PreserveDontCheck,
    },

    [RendPainter3DPass_Post] = { .name = string_static("Post"),
        // Attachment color 0: color (rgba).
        .attachColorFormat[0] = RvkPassFormat_Swapchain,
        .attachColorLoad[0]   = RvkPassLoad_DontCare,
    },
};

// clang-format on

ecs_comp_define_public(RendPainterComp);

static void ecs_destruct_painter(void* data) {
  RendPainterComp* comp = data;
  rvk_canvas_destroy(comp->canvas);
}

ecs_view_define(GlobalView) {
  ecs_access_read(RendFogComp);
  ecs_access_read(RendLightRendererComp);
  ecs_access_read(RendSettingsGlobalComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_without(RendResetComp);
  ecs_access_write(RendPlatformComp);
}

ecs_view_define(ObjView) { ecs_access_read(RendObjectComp); }

ecs_view_define(ResourceView) {
  ecs_access_maybe_write(RendResGraphicComp);
  ecs_access_maybe_write(RendResMeshComp);
  ecs_access_maybe_write(RendResTextureComp);
  ecs_access_with(RendResFinishedComp);
  ecs_access_without(RendResUnloadComp);
  ecs_access_write(RendResComp);
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
  RvkCanvas*                    canvas;
  RendBuilderBuffer*            builder;
  const RendSettingsComp*       settings;
  const RendSettingsGlobalComp* settingsGlobal;
  const SceneTimeComp*          time;
  RvkPass*                      pass;
  RendView                      view;
} RendPaintContext;

static RendPaintContext painter_context(
    RvkCanvas*                    canvas,
    RendBuilderBuffer*            builder,
    const RendSettingsComp*       settings,
    const RendSettingsGlobalComp* settingsGlobal,
    const SceneTimeComp*          time,
    RvkPass*                      pass,
    RendView                      view) {

  return (RendPaintContext){
      .canvas         = canvas,
      .builder        = builder,
      .settings       = settings,
      .settingsGlobal = settingsGlobal,
      .time           = time,
      .pass           = pass,
      .view           = view,
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

static RvkGraphic* painter_get_graphic(EcsIterator* resourceItr, const EcsEntityId resource) {
  if (!ecs_view_maybe_jump(resourceItr, resource)) {
    return null; // Resource not loaded.
  }
  RendResGraphicComp* graphicResource = ecs_view_write_t(resourceItr, RendResGraphicComp);
  if (!graphicResource) {
    log_e("Invalid graphic asset", log_param("entity", ecs_entity_fmt(resource)));
    return null;
  }
  return graphicResource->graphic;
}

static RvkTexture* painter_get_texture(EcsIterator* resourceItr, const EcsEntityId resource) {
  if (!ecs_view_maybe_jump(resourceItr, resource)) {
    return null; // Resource not loaded.
  }
  RendResTextureComp* textureResource = ecs_view_write_t(resourceItr, RendResTextureComp);
  if (!textureResource) {
    log_e("Invalid texture asset", log_param("entity", ecs_entity_fmt(resource)));
    return null;
  }
  return textureResource->texture;
}

static void painter_push_simple(RendPaintContext* ctx, const RvkRepositoryId id, const Mem data) {
  RvkRepository* repo    = rvk_canvas_repository(ctx->canvas);
  RvkGraphic*    graphic = rvk_repository_graphic_get_maybe(repo, id);
  if (graphic && rvk_pass_prepare(ctx->pass, graphic)) {
    rend_builder_draw_push(ctx->builder, graphic);
    if (data.size) {
      mem_cpy(rend_builder_draw_data(ctx->builder, data.size), data);
    }
    rend_builder_draw_instances(ctx->builder, 1, 0);
    rend_builder_draw_flush(ctx->builder);
  }
}

static SceneTags painter_push_draws_simple(
    RendPaintContext*     ctx,
    EcsView*              objView,
    EcsView*              resourceView,
    const RendObjectFlags includeFlags /* included if the draw has any of these flags */,
    const RendObjectFlags ignoreFlags) {
  SceneTags tagMask = 0;

  EcsIterator* resourceItr = ecs_view_itr(resourceView);
  for (EcsIterator* objItr = ecs_view_itr(objView); ecs_view_walk(objItr);) {
    const RendObjectComp* obj = ecs_view_read_t(objItr, RendObjectComp);
    if (includeFlags && !(rend_draw_flags(obj) & includeFlags)) {
      continue; // Object misses a include flag.
    }
    if (rend_draw_flags(obj) & ignoreFlags) {
      continue; // Object has an ignore flag.
    }

    // Retrieve and prepare the object's graphic.
    const EcsEntityId graphicResource = rend_draw_resource(obj, RendDrawResource_Graphic);
    RvkGraphic*       graphic         = painter_get_graphic(resourceItr, graphicResource);
    if (!graphic || !rvk_pass_prepare(ctx->pass, graphic)) {
      continue; // Graphic not ready to be drawn.
    }

    // If the object uses a 'per draw' texture then retrieve and prepare it.
    const EcsEntityId textureResource = rend_draw_resource(obj, RendDrawResource_Texture);
    RvkTexture*       texture         = null;
    if (textureResource) {
      texture = painter_get_texture(resourceItr, textureResource);
      if (!texture || !rvk_pass_prepare_texture(ctx->pass, texture)) {
        continue; // Object uses a 'per draw' texture which is not ready.
      }
    }

    rend_builder_draw_push(ctx->builder, graphic);
    if (texture) {
      rend_builder_draw_image(ctx->builder, &texture->image);
    }
    rend_draw_push(obj, &ctx->view, ctx->settings, ctx->builder);
    rend_builder_draw_flush(ctx->builder);

    tagMask |= rend_draw_tag_mask(obj);
  }

  return tagMask;
}

static void painter_push_shadow(RendPaintContext* ctx, EcsView* objView, EcsView* resourceView) {
  RendObjectFlags requiredAny = 0;
  requiredAny |= RendObjectFlags_StandardGeometry; // Include geometry.
  if (ctx->settings->flags & RendFlags_VfxSpriteShadows) {
    requiredAny |= RendObjectFlags_VfxSprite; // Include vfx sprites.
  }

  RvkRepository* repo        = rvk_canvas_repository(ctx->canvas);
  EcsIterator*   resourceItr = ecs_view_itr(resourceView);

  for (EcsIterator* objItr = ecs_view_itr(objView); ecs_view_walk(objItr);) {
    const RendObjectComp* obj = ecs_view_read_t(objItr, RendObjectComp);
    if (!(rend_draw_flags(obj) & requiredAny)) {
      continue; // Object shouldn't be included in the shadow pass.
    }
    const EcsEntityId graphicOriginalResource = rend_draw_resource(obj, RendDrawResource_Graphic);
    RvkGraphic*       graphicOriginal = painter_get_graphic(resourceItr, graphicOriginalResource);
    if (!graphicOriginal) {
      continue; // Graphic not loaded.
    }
    const bool isVfxSprite = (rend_draw_flags(obj) & RendObjectFlags_VfxSprite) != 0;
    RvkMesh*   objMesh     = graphicOriginal->mesh;
    if (!isVfxSprite && (!objMesh || !rvk_pass_prepare_mesh(ctx->pass, objMesh))) {
      continue; // Graphic is not a vfx sprite and does not have a mesh to draw a shadow for.
    }
    RvkImage* objAlphaImg = null;
    enum { AlphaTextureIndex = 2 }; // TODO: Make this configurable from content.
    const bool hasAlphaTexture = (graphicOriginal->samplerMask & (1 << AlphaTextureIndex)) != 0;
    if (graphicOriginal->flags & RvkGraphicFlags_MayDiscard && hasAlphaTexture) {
      RvkTexture* alphaTexture = graphicOriginal->samplerTextures[AlphaTextureIndex];
      if (!alphaTexture || !rvk_pass_prepare_texture(ctx->pass, alphaTexture)) {
        continue; // Graphic uses discard but has no alpha texture.
      }
      objAlphaImg = &alphaTexture->image;
    }
    RvkRepositoryId graphicId;
    if (isVfxSprite) {
      graphicId = RvkRepositoryId_ShadowVfxSpriteGraphic;
    } else if (rend_draw_flags(obj) & RendObjectFlags_Skinned) {
      graphicId = RvkRepositoryId_ShadowSkinnedGraphic;
    } else {
      graphicId = objAlphaImg ? RvkRepositoryId_ShadowClipGraphic : RvkRepositoryId_ShadowGraphic;
    }
    RvkGraphic* shadowGraphic = rvk_repository_graphic_get_maybe(repo, graphicId);
    if (!shadowGraphic) {
      continue; // Shadow graphic not loaded.
    }

    if (rvk_pass_prepare(ctx->pass, shadowGraphic)) {
      rend_builder_draw_push(ctx->builder, shadowGraphic);
      rend_builder_draw_mesh(ctx->builder, objMesh);
      if (objAlphaImg) {
        rend_builder_draw_image(ctx->builder, objAlphaImg);
        rend_builder_draw_sampler(ctx->builder, (RvkSamplerSpec){.aniso = RvkSamplerAniso_x8});
      }
      rend_draw_push(obj, &ctx->view, ctx->settings, ctx->builder);
      rend_builder_draw_flush(ctx->builder);
    }
  }
}

static void painter_push_fog(RendPaintContext* ctx, const RendFogComp* fog, RvkImage* fogMap) {
  RvkRepository*        repo      = rvk_canvas_repository(ctx->canvas);
  const RvkRepositoryId graphicId = RvkRepositoryId_FogGraphic;
  RvkGraphic*           graphic   = rvk_repository_graphic_get_maybe(repo, graphicId);
  if (graphic && rvk_pass_prepare(ctx->pass, graphic)) {
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

  const u32    mode  = ctx->settings->ambientMode;
  AmbientFlags flags = 0;
  if (ctx->settings->flags & RendFlags_AmbientOcclusion) {
    flags |= AmbientFlags_AmbientOcclusion;
  }
  if (ctx->settings->flags & RendFlags_AmbientOcclusionBlur) {
    flags |= AmbientFlags_AmbientOcclusionBlur;
  }

  data.packed.x = intensity;
  data.packed.y = bits_u32_as_f32(mode);
  data.packed.z = bits_u32_as_f32(flags);

  const RvkRepositoryId graphicId = ctx->settings->ambientMode >= RendAmbientMode_DebugStart
                                        ? RvkRepositoryId_AmbientDebugGraphic
                                        : RvkRepositoryId_AmbientGraphic;

  painter_push_simple(ctx, graphicId, mem_var(data));
}

static void painter_push_ambient_occlusion(RendPaintContext* ctx) {
  struct {
    ALIGNAS(16)
    f32       radius;
    f32       power;
    GeoVector kernel[rend_ao_kernel_size];
  } data;

  data.radius      = ctx->settings->aoRadius;
  data.power       = ctx->settings->aoPower;
  const Mem kernel = mem_create(ctx->settings->aoKernel, sizeof(GeoVector) * rend_ao_kernel_size);
  mem_cpy(array_mem(data.kernel), kernel);

  painter_push_simple(ctx, RvkRepositoryId_AmbientOcclusionGraphic, mem_var(data));
}

static void painter_push_forward(RendPaintContext* ctx, EcsView* objView, EcsView* resourceView) {
  RendObjectFlags ignoreFlags = 0;
  ignoreFlags |= RendObjectFlags_Geometry;   // Ignore geometry (drawn in a separate pass).
  ignoreFlags |= RendObjectFlags_Decal;      // Ignore decals (drawn in a separate pass).
  ignoreFlags |= RendObjectFlags_FogVision;  // Ignore fog-vision (drawn in a separate pass).
  ignoreFlags |= RendObjectFlags_Distortion; // Ignore distortion (drawn in a separate pass)
  ignoreFlags |= RendObjectFlags_Post;       // Ignore post (drawn in a separate pass).

  if (ctx->settings->ambientMode >= RendAmbientMode_DebugStart) {
    // Disable lighting when using any of the debug ambient modes.
    ignoreFlags |= RendObjectFlags_Light;
  }

  painter_push_draws_simple(ctx, objView, resourceView, RendObjectFlags_None, ignoreFlags);
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
  RvkRepository* repo = rvk_canvas_repository(ctx->canvas);
  RvkGraphic*    graphic;
  if (image->type == RvkImageType_ColorSourceCube) {
    graphic = rvk_repository_graphic_get_maybe(repo, RvkRepositoryId_DebugImageViewerCubeGraphic);
  } else {
    graphic = rvk_repository_graphic_get_maybe(repo, RvkRepositoryId_DebugImageViewerGraphic);
  }
  if (graphic && rvk_pass_prepare(ctx->pass, graphic)) {
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

static void painter_push_debug_mesh_viewer(RendPaintContext* ctx, const f32 aspect, RvkMesh* mesh) {
  RvkRepository*        repo      = rvk_canvas_repository(ctx->canvas);
  const RvkRepositoryId graphicId = RvkRepositoryId_DebugMeshViewerGraphic;
  RvkGraphic*           graphic   = rvk_repository_graphic_get_maybe(repo, graphicId);
  if (graphic && rvk_pass_prepare(ctx->pass, graphic)) {
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
    RendPaintContext* ctx,
    const f32         aspect,
    EcsView*          resourceView,
    const EcsEntityId resourceEntity) {

  EcsIterator* itr = ecs_view_maybe_at(resourceView, resourceEntity);
  if (itr) {
    rend_res_mark_used(ecs_view_write_t(itr, RendResComp));

    RendResTextureComp* textureComp = ecs_view_write_t(itr, RendResTextureComp);
    if (textureComp) {
      if (rvk_pass_prepare_texture(ctx->pass, textureComp->texture)) {
        const f32 exposure = 1.0f;
        painter_push_debug_image_viewer(ctx, &textureComp->texture->image, exposure);
      }
    }
    RendResMeshComp* meshComp = ecs_view_write_t(itr, RendResMeshComp);
    if (meshComp && rvk_pass_prepare_mesh(ctx->pass, meshComp->mesh)) {
      painter_push_debug_mesh_viewer(ctx, aspect, meshComp->mesh);
    }
  }
}

static void
painter_push_debug_wireframe(RendPaintContext* ctx, EcsView* objView, EcsView* resourceView) {
  RvkRepository* repo        = rvk_canvas_repository(ctx->canvas);
  EcsIterator*   resourceItr = ecs_view_itr(resourceView);

  for (EcsIterator* objItr = ecs_view_itr(objView); ecs_view_walk(objItr);) {
    const RendObjectComp* obj = ecs_view_read_t(objItr, RendObjectComp);
    if (!(rend_draw_flags(obj) & RendObjectFlags_Geometry)) {
      continue; // Not a object we can render a wireframe for.
    }
    const EcsEntityId graphicOriginalResource = rend_draw_resource(obj, RendDrawResource_Graphic);
    RvkGraphic*       graphicOriginal = painter_get_graphic(resourceItr, graphicOriginalResource);
    if (!graphicOriginal) {
      continue; // Graphic not loaded.
    }
    RvkMesh* mesh = graphicOriginal->mesh;
    if (!mesh || !rvk_pass_prepare_mesh(ctx->pass, mesh)) {
      continue; // Graphic does not have a mesh to draw a wireframe for (or its not ready).
    }

    RvkRepositoryId graphicId;
    if (rend_draw_flags(obj) & RendObjectFlags_Terrain) {
      graphicId = RvkRepositoryId_DebugWireframeTerrainGraphic;
    } else if (rend_draw_flags(obj) & RendObjectFlags_Skinned) {
      graphicId = RvkRepositoryId_DebugWireframeSkinnedGraphic;
    } else {
      graphicId = RvkRepositoryId_DebugWireframeGraphic;
    }
    RvkGraphic* graphicWireframe = rvk_repository_graphic_get_maybe(repo, graphicId);
    if (!graphicWireframe || !rvk_pass_prepare(ctx->pass, graphicWireframe)) {
      continue; // Wireframe graphic not loaded.
    }

    // If the object uses a 'per draw' texture then retrieve and prepare it.
    // NOTE: This is needed for the terrain wireframe as it contains the heightmap.
    const EcsEntityId textureResource = rend_draw_resource(obj, RendDrawResource_Texture);
    RvkTexture*       texture         = null;
    if (textureResource) {
      texture = painter_get_texture(resourceItr, textureResource);
      if (!texture || !rvk_pass_prepare_texture(ctx->pass, texture)) {
        continue; // Object uses a 'per draw' texture which is not ready.
      }
    }

    rend_builder_draw_push(ctx->builder, graphicWireframe);
    rend_builder_draw_mesh(ctx->builder, mesh);
    if (texture) {
      rend_builder_draw_image(ctx->builder, &texture->image);
    }
    rend_draw_push(obj, &ctx->view, ctx->settings, ctx->builder);
    rend_builder_draw_flush(ctx->builder);
  }
}

static void
painter_push_debug_skinning(RendPaintContext* ctx, EcsView* objView, EcsView* resourceView) {
  RvkRepository*        repository     = rvk_canvas_repository(ctx->canvas);
  const RvkRepositoryId debugGraphicId = RvkRepositoryId_DebugSkinningGraphic;
  RvkGraphic*           debugGraphic = rvk_repository_graphic_get_maybe(repository, debugGraphicId);
  if (!debugGraphic || !rvk_pass_prepare(ctx->pass, debugGraphic)) {
    return; // Debug graphic not ready to be drawn.
  }

  EcsIterator* resourceItr = ecs_view_itr(resourceView);
  for (EcsIterator* objItr = ecs_view_itr(objView); ecs_view_walk(objItr);) {
    const RendObjectComp* obj = ecs_view_read_t(objItr, RendObjectComp);
    if (!(rend_draw_flags(obj) & RendObjectFlags_Skinned)) {
      continue; // Not a skinned object.
    }
    const EcsEntityId graphicOriginalResource = rend_draw_resource(obj, RendDrawResource_Graphic);
    RvkGraphic*       graphicOriginal = painter_get_graphic(resourceItr, graphicOriginalResource);
    if (!graphicOriginal) {
      continue; // Graphic not loaded.
    }
    RvkMesh* mesh = graphicOriginal->mesh;
    diag_assert(mesh);

    if (rvk_pass_prepare_mesh(ctx->pass, mesh)) {
      rend_builder_draw_push(ctx->builder, debugGraphic);
      rend_builder_draw_mesh(ctx->builder, mesh);
      rend_draw_push(obj, &ctx->view, ctx->settings, ctx->builder);
      rend_builder_draw_flush(ctx->builder);
    }
  }
}

static bool rend_canvas_paint_2d(
    RendPainterComp*              painter,
    RendBuilderBuffer*            builder,
    const RendSettingsComp*       set,
    const RendSettingsGlobalComp* setGlobal,
    const SceneTimeComp*          time,
    const GapWindowComp*          win,
    const EcsEntityId             camEntity,
    EcsView*                      objView,
    EcsView*                      resourceView) {
  diag_assert(rvk_canvas_pass_count(painter->canvas) == RendPainter2DPass_Count);

  if (!rvk_canvas_begin(painter->canvas, set, painter_win_size(win))) {
    return false; // Canvas not ready for rendering.
  }
  trace_begin("rend_paint_2d", TraceColor_Red);

  const RendView mainView = painter_view_2d_create(camEntity);

  RvkImage* swapchainImage = rvk_canvas_swapchain_image(painter->canvas);
  rvk_canvas_img_clear_color(painter->canvas, swapchainImage, geo_color_black);

  RvkPass* postPass = rvk_canvas_pass(painter->canvas, RendPainter2DPass_Post);
  {
    rend_builder_pass_push(builder, postPass);

    RendPaintContext ctx =
        painter_context(painter->canvas, builder, set, setGlobal, time, postPass, mainView);
    rvk_pass_stage_attach_color(postPass, swapchainImage, 0);
    painter_push_draws_simple(
        &ctx, objView, resourceView, RendObjectFlags_Post, RendObjectFlags_None);

    rend_builder_pass_flush(builder);
  }

  trace_end();
  rvk_canvas_end(painter->canvas);
  return true;
}

static bool rend_canvas_paint_3d(
    RendPainterComp*              painter,
    RendBuilderBuffer*            builder,
    const RendSettingsComp*       set,
    const RendSettingsGlobalComp* setGlobal,
    const SceneTimeComp*          time,
    const RendLightRendererComp*  light,
    const RendFogComp*            fog,
    const GapWindowComp*          win,
    const EcsEntityId             camEntity,
    const SceneCameraComp*        cam,
    const SceneTransformComp*     camTrans,
    EcsView*                      objView,
    EcsView*                      resourceView) {
  diag_assert(rvk_canvas_pass_count(painter->canvas) == RendPainter3DPass_Count);

  const RvkSize winSize   = painter_win_size(win);
  const f32     winAspect = (f32)winSize.width / (f32)winSize.height;

  if (!rvk_canvas_begin(painter->canvas, set, winSize)) {
    return false; // Canvas not ready for rendering.
  }
  trace_begin("rend_paint_3d", TraceColor_Red);

  const GeoMatrix      camMat   = camTrans ? scene_transform_matrix(camTrans) : geo_matrix_ident();
  const GeoMatrix      projMat  = cam ? scene_camera_proj(cam, winAspect)
                                      : geo_matrix_proj_ortho_hor(2.0, winAspect, -100, 100);
  const SceneTagFilter filter   = cam ? cam->filter : (SceneTagFilter){0};
  const RendView       mainView = painter_view_3d_create(&camMat, &projMat, camEntity, filter);

  RvkImage*     swapchainImage = rvk_canvas_swapchain_image(painter->canvas);
  const RvkSize swapchainSize  = swapchainImage->size;

  // Geometry pass.
  const RvkSize geoSize  = rvk_size_scale(swapchainSize, set->resolutionScale);
  RvkPass*      geoPass  = rvk_canvas_pass(painter->canvas, RendPainter3DPass_Geometry);
  RvkImage*     geoData0 = rvk_canvas_attach_acquire_color(painter->canvas, geoPass, 0, geoSize);
  RvkImage*     geoData1 = rvk_canvas_attach_acquire_color(painter->canvas, geoPass, 1, geoSize);
  RvkImage*     geoDepth = rvk_canvas_attach_acquire_depth(painter->canvas, geoPass, geoSize);
  SceneTags     geoTagMask;
  {
    trace_begin("rend_paint_geo", TraceColor_White);
    rend_builder_pass_push(builder, geoPass);

    RendPaintContext ctx =
        painter_context(painter->canvas, builder, set, setGlobal, time, geoPass, mainView);
    rvk_pass_stage_attach_color(geoPass, geoData0, 0);
    rvk_pass_stage_attach_color(geoPass, geoData1, 1);
    rvk_pass_stage_attach_depth(geoPass, geoDepth);
    painter_stage_global_data(&ctx, &camMat, &projMat, geoSize, time, RendViewType_Main);
    geoTagMask = painter_push_draws_simple(
        &ctx, objView, resourceView, RendObjectFlags_Geometry, RendObjectFlags_None);

    rend_builder_pass_flush(builder);
    trace_end();
  }

  // Make a copy of the geometry depth to read from while still writing to the original.
  // TODO: Instead of a straight copy considering performing linearization at the same time.
  RvkImage* geoDepthRead = rvk_canvas_attach_acquire_copy(painter->canvas, geoDepth);

  // Decal pass.
  RvkPass* decalPass = rvk_canvas_pass(painter->canvas, RendPainter3DPass_Decal);
  if (set->flags & RendFlags_Decals) {

    trace_begin("rend_paint_decals", TraceColor_White);
    rend_builder_pass_push(builder, decalPass);

    // Copy the gbufer data1 image to be able to read the gbuffer normal and tags.
    RvkImage* geoData1Cpy = rvk_canvas_attach_acquire_copy(painter->canvas, geoData1);

    RendPaintContext ctx =
        painter_context(painter->canvas, builder, set, setGlobal, time, decalPass, mainView);
    rvk_pass_stage_global_image(decalPass, geoData1Cpy, 0);
    rvk_pass_stage_global_image(decalPass, geoDepthRead, 1);
    rvk_pass_stage_attach_color(decalPass, geoData0, 0);
    rvk_pass_stage_attach_color(decalPass, geoData1, 1);
    rvk_pass_stage_attach_depth(decalPass, geoDepth);
    painter_stage_global_data(&ctx, &camMat, &projMat, geoSize, time, RendViewType_Main);
    painter_push_draws_simple(
        &ctx, objView, resourceView, RendObjectFlags_Decal, RendObjectFlags_None);

    rend_builder_pass_flush(builder);
    trace_end();

    rvk_canvas_attach_release(painter->canvas, geoData1Cpy);
  }

  // Fog pass.
  const bool    fogActive = rend_fog_active(fog);
  RvkPass*      fogPass   = rvk_canvas_pass(painter->canvas, RendPainter3DPass_Fog);
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

    RendPaintContext ctx =
        painter_context(painter->canvas, builder, set, setGlobal, time, fogPass, fogView);
    rvk_pass_stage_attach_color(fogPass, fogBuffer, 0);
    painter_stage_global_data(&ctx, fogTrans, fogProj, fogSize, time, RendViewType_Fog);
    painter_push_draws_simple(
        &ctx, objView, resourceView, RendObjectFlags_FogVision, RendObjectFlags_None);

    rend_builder_pass_flush(builder);
    trace_end();
  } else {
    rvk_canvas_img_clear_color(painter->canvas, fogBuffer, geo_color_white);
  }

  // Fog-blur pass.
  RvkPass* fogBlurPass = rvk_canvas_pass(painter->canvas, RendPainter3DPass_FogBlur);
  if (fogActive && set->fogBlurSteps) {
    trace_begin("rend_paint_fog_blur", TraceColor_White);

    RendPaintContext ctx =
        painter_context(painter->canvas, builder, set, setGlobal, time, fogBlurPass, mainView);

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
      painter_push_simple(&ctx, RvkRepositoryId_BlurHorGraphic, mem_var(blurData));
      rend_builder_pass_flush(builder);

      // Vertical pass.
      rend_builder_pass_push(builder, fogBlurPass);
      rvk_pass_stage_global_image(fogBlurPass, tmp, 0);
      rvk_pass_stage_attach_color(fogBlurPass, fogBuffer, 0);
      painter_push_simple(&ctx, RvkRepositoryId_BlurVerGraphic, mem_var(blurData));
      rend_builder_pass_flush(builder);
    }
    rvk_canvas_attach_release(painter->canvas, tmp);
    trace_end();
  }

  // Shadow pass.
  const bool    shadowsActive = set->flags & RendFlags_Shadows && rend_light_has_shadow(light);
  const RvkSize shadowSize =
      shadowsActive ? (RvkSize){set->shadowResolution, set->shadowResolution} : (RvkSize){1, 1};
  RvkPass*  shadowPass  = rvk_canvas_pass(painter->canvas, RendPainter3DPass_Shadow);
  RvkImage* shadowDepth = rvk_canvas_attach_acquire_depth(painter->canvas, shadowPass, shadowSize);
  if (shadowsActive) {
    trace_begin("rend_paint_shadows", TraceColor_White);
    rend_builder_pass_push(builder, shadowPass);

    const GeoMatrix*     shadTrans  = rend_light_shadow_trans(light);
    const GeoMatrix*     shadProj   = rend_light_shadow_proj(light);
    const SceneTagFilter shadFilter = {
        .required = filter.required | SceneTags_ShadowCaster,
        .illegal  = filter.illegal,
    };
    const RendView   shadView = painter_view_3d_create(shadTrans, shadProj, camEntity, shadFilter);
    RendPaintContext ctx =
        painter_context(painter->canvas, builder, set, setGlobal, time, shadowPass, shadView);
    rvk_pass_stage_attach_depth(shadowPass, shadowDepth);
    painter_stage_global_data(&ctx, shadTrans, shadProj, shadowSize, time, RendViewType_Shadow);
    painter_push_shadow(&ctx, objView, resourceView);

    rend_builder_pass_flush(builder);
    trace_end();
  } else {
    rvk_canvas_img_clear_depth(painter->canvas, shadowDepth, 0);
  }

  // Ambient occlusion.
  const RvkSize aoSize   = set->flags & RendFlags_AmbientOcclusion
                               ? rvk_size_scale(geoSize, set->aoResolutionScale)
                               : (RvkSize){1, 1};
  RvkPass*      aoPass   = rvk_canvas_pass(painter->canvas, RendPainter3DPass_AmbientOcclusion);
  RvkImage*     aoBuffer = rvk_canvas_attach_acquire_color(painter->canvas, aoPass, 0, aoSize);
  if (set->flags & RendFlags_AmbientOcclusion) {
    trace_begin("rend_paint_ao", TraceColor_White);
    rend_builder_pass_push(builder, aoPass);

    RendPaintContext ctx =
        painter_context(painter->canvas, builder, set, setGlobal, time, aoPass, mainView);
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
  RvkPass*  fwdPass  = rvk_canvas_pass(painter->canvas, RendPainter3DPass_Forward);
  RvkImage* fwdColor = rvk_canvas_attach_acquire_color(painter->canvas, fwdPass, 0, geoSize);
  {
    trace_begin("rend_paint_forward", TraceColor_White);
    rend_builder_pass_push(builder, fwdPass);

    if (set->flags & RendFlags_DebugCamera && set->skyMode == RendSkyMode_None) {
      // NOTE: The debug camera-mode does not draw to the whole image; thus we need to clear it.
      rvk_canvas_img_clear_color(painter->canvas, fwdColor, geo_color_black);
    }
    RendPaintContext ctx =
        painter_context(painter->canvas, builder, set, setGlobal, time, fwdPass, mainView);
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
    painter_push_forward(&ctx, objView, resourceView);
    if (fogActive) {
      painter_push_fog(&ctx, fog, fogBuffer);
    }
    if (set->flags & RendFlags_DebugWireframe) {
      painter_push_debug_wireframe(&ctx, objView, resourceView);
    }
    if (set->flags & RendFlags_DebugSkinning) {
      painter_push_debug_skinning(&ctx, objView, resourceView);
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
  RvkPass*      distPass = rvk_canvas_pass(painter->canvas, RendPainter3DPass_Distortion);
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

    RendPaintContext ctx =
        painter_context(painter->canvas, builder, set, setGlobal, time, distPass, mainView);
    rvk_pass_stage_attach_color(distPass, distBuffer, 0);
    rvk_pass_stage_attach_depth(distPass, distDepth);

    painter_stage_global_data(&ctx, &camMat, &projMat, distSize, time, RendViewType_Main);
    painter_push_draws_simple(
        &ctx, objView, resourceView, RendObjectFlags_Distortion, RendObjectFlags_None);

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
  RvkPass*  bloomPass = rvk_canvas_pass(painter->canvas, RendPainter3DPass_Bloom);
  RvkImage* bloomOutput;
  if (set->flags & RendFlags_Bloom && set->bloomIntensity > f32_epsilon) {
    trace_begin("rend_paint_bloom", TraceColor_White);

    RendPaintContext ctx =
        painter_context(painter->canvas, builder, set, setGlobal, time, bloomPass, mainView);
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
  RvkPass* postPass = rvk_canvas_pass(painter->canvas, RendPainter3DPass_Post);
  {
    trace_begin("rend_paint_post", TraceColor_White);
    rend_builder_pass_push(builder, postPass);

    RendPaintContext ctx =
        painter_context(painter->canvas, builder, set, setGlobal, time, postPass, mainView);
    rvk_pass_stage_global_image(postPass, fwdColor, 0);
    rvk_pass_stage_global_image(postPass, bloomOutput, 1);
    rvk_pass_stage_global_image(postPass, distBuffer, 2);
    rvk_pass_stage_global_image(postPass, fogBuffer, 3);
    rvk_pass_stage_attach_color(postPass, swapchainImage, 0);
    painter_stage_global_data(&ctx, &camMat, &projMat, swapchainSize, time, RendViewType_Main);
    painter_push_tonemapping(&ctx);
    painter_push_draws_simple(
        &ctx, objView, resourceView, RendObjectFlags_Post, RendObjectFlags_None);

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
      painter_push_debug_resource_viewer(&ctx, winAspect, resourceView, set->debugViewerResource);
    }

    rend_builder_pass_flush(builder);
    trace_end();
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
      p->canvas = rvk_canvas_create(plat->device, win, g_passConfig2D, RendPainter2DPass_Count);
      break;
    case RendPainterType_3D:
      p->canvas = rvk_canvas_create(plat->device, win, g_passConfig3D, RendPainter3DPass_Count);
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
  RendPlatformComp*             platform       = ecs_view_write_t(globalItr, RendPlatformComp);
  const RendSettingsGlobalComp* settingsGlobal = ecs_view_read_t(globalItr, RendSettingsGlobalComp);
  const SceneTimeComp*          time           = ecs_view_read_t(globalItr, SceneTimeComp);
  const RendLightRendererComp*  light          = ecs_view_read_t(globalItr, RendLightRendererComp);
  const RendFogComp*            fog            = ecs_view_read_t(globalItr, RendFogComp);

  EcsView* painterView  = ecs_world_view_t(world, PainterUpdateView);
  EcsView* objView      = ecs_world_view_t(world, ObjView);
  EcsView* resourceView = ecs_world_view_t(world, ResourceView);

  for (EcsIterator* itr = ecs_view_itr(painterView); ecs_view_walk(itr);) {
    const EcsEntityId         entity   = ecs_view_entity(itr);
    const GapWindowComp*      win      = ecs_view_read_t(itr, GapWindowComp);
    RendPainterComp*          painter  = ecs_view_write_t(itr, RendPainterComp);
    const RendSettingsComp*   settings = ecs_view_read_t(itr, RendSettingsComp);
    const SceneCameraComp*    cam      = ecs_view_read_t(itr, SceneCameraComp);
    const SceneTransformComp* camTrans = ecs_view_read_t(itr, SceneTransformComp);

    RendBuilderBuffer* builder = rend_builder_buffer(platform->builder);

    switch (painter->type) {
    case RendPainterType_2D:
      rend_canvas_paint_2d(
          painter, builder, settings, settingsGlobal, time, win, entity, objView, resourceView);
      break;
    case RendPainterType_3D:
      rend_canvas_paint_3d(
          painter,
          builder,
          settings,
          settingsGlobal,
          time,
          light,
          fog,
          win,
          entity,
          cam,
          camTrans,
          objView,
          resourceView);
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
