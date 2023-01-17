#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_thread.h"
#include "ecs_utils.h"
#include "gap_window.h"
#include "rend_register.h"
#include "rend_settings.h"
#include "scene_camera.h"
#include "scene_time.h"
#include "scene_transform.h"

#include "draw_internal.h"
#include "light_internal.h"
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

static const RvkPassConfig g_passConfig[RendPass_Count] = {
    [RendPass_Geometry] =
        {
            // Attachment depth.
            .attachDepth     = RvkPassDepth_Stored,
            .attachDepthLoad = RvkPassLoad_Clear,

            // Attachment color 0:  color (rgb) and roughness (a).
            .attachColorFormat[0] = RvkPassFormat_Color4Srgb,
            .attachColorLoad[0]   = RvkPassLoad_Clear,

            // Attachment color 1: normal (rgb) and tags (a).
            .attachColorFormat[1] = RvkPassFormat_Color4Linear,
            .attachColorLoad[1]   = RvkPassLoad_Clear,
        },

    [RendPass_Shadow] =
        {
            // Attachment depth.
            .attachDepth     = RvkPassDepth_Stored,
            .attachDepthLoad = RvkPassLoad_Clear,
        },

    [RendPass_AmbientOcclusion] =
        {
            // Attachment color 0: occlusion (r).
            .attachColorFormat[0] = RvkPassFormat_Color1Linear,
            .attachColorLoad[0]   = RvkPassLoad_DontCare,
        },

    [RendPass_Forward] =
        {
            // Attachment depth.
            .attachDepth     = RvkPassDepth_Transient,
            .attachDepthLoad = RvkPassLoad_Preserve,

            // Attachment color 0: color (rgb).
            .attachColorFormat[0] = RvkPassFormat_Color3Float,
            .attachColorLoad[0]   = RvkPassLoad_Clear,
        },

    [RendPass_Bloom] =
        {
            // Attachment color 0: bloom (rgb).
            .attachColorFormat[0] = RvkPassFormat_Color3Float,
            .attachColorLoad[0]   = RvkPassLoad_PreserveDontCheck,
        },

    [RendPass_Post] =
        {
            // Attachment color 0: color (rgba).
            .attachColorFormat[0] = RvkPassFormat_Swapchain,
            .attachColorLoad[0]   = RvkPassLoad_DontCare,
        },
};

ecs_comp_define_public(RendPainterComp);

static void ecs_destruct_painter(void* data) {
  RendPainterComp* comp = data;
  dynarray_destroy(&comp->drawBuffer);
  rvk_canvas_destroy(comp->canvas);
}

ecs_view_define(GlobalView) {
  ecs_access_read(RendLightRendererComp);
  ecs_access_read(RendSettingsGlobalComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_without(RendResetComp);
  ecs_access_write(RendPlatformComp);
}
ecs_view_define(DrawView) { ecs_access_write(RendDrawComp); }
ecs_view_define(GraphicView) {
  ecs_access_write(RendResGraphicComp);
  ecs_access_with(RendResFinishedComp);
  ecs_access_without(RendResUnloadComp);
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

static i8 painter_compare_pass_draw(const void* a, const void* b) {
  const RvkPassDraw* drawA = a;
  const RvkPassDraw* drawB = b;
  return compare_i32(&drawA->graphic->renderOrder, &drawB->graphic->renderOrder);
}

static RvkSize painter_win_size(const GapWindowComp* win) {
  const GapVector winSize = gap_window_param(win, GapParam_WindowSize);
  return rvk_size((u16)winSize.width, (u16)winSize.height);
}

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

typedef struct {
  RendPainterComp*              painter;
  RvkPass*                      pass;
  const RendSettingsComp*       settings;
  const RendSettingsGlobalComp* settingsGlobal;
  RendPainterGlobalData         data;
  RendView                      view;
} RendPaintContext;

static RendPaintContext painter_context(
    const GeoMatrix*              cameraMatrix,
    const GeoMatrix*              projMatrix,
    const EcsEntityId             sceneCameraEntity,
    const SceneTagFilter          sceneFilter,
    RendPainterComp*              painter,
    const RendSettingsComp*       settings,
    const RendSettingsGlobalComp* settingsGlobal,
    const SceneTimeComp*          time,
    RvkPass*                      pass,
    const RvkSize                 size) {
  const GeoMatrix viewMatrix     = geo_matrix_inverse(cameraMatrix);
  const GeoMatrix viewProjMatrix = geo_matrix_mul(projMatrix, &viewMatrix);
  const GeoVector cameraPosition = geo_matrix_to_translation(cameraMatrix);
  const GeoQuat   cameraRotation = geo_matrix_to_quat(cameraMatrix);

  return (RendPaintContext){
      .painter        = painter,
      .settings       = settings,
      .settingsGlobal = settingsGlobal,
      .pass           = pass,
      .data =
          {
              .view         = viewMatrix,
              .viewInv      = *cameraMatrix,
              .proj         = *projMatrix,
              .projInv      = geo_matrix_inverse(projMatrix),
              .viewProj     = viewProjMatrix,
              .viewProjInv  = geo_matrix_inverse(&viewProjMatrix),
              .camPosition  = cameraPosition,
              .camRotation  = cameraRotation,
              .resolution.x = size.width,
              .resolution.y = size.height,
              .resolution.z = (f32)size.width / (f32)size.height,
              .time.x       = scene_time_seconds(time),
              .time.y       = scene_real_time_seconds(time),
          },
      .view = rend_view_create(sceneCameraEntity, cameraPosition, &viewProjMatrix, sceneFilter),
  };
}

static void painter_set_debug_camera(RendPaintContext* ctx) {
  static const f32 g_size     = 300;
  static const f32 g_depthMin = -200;
  static const f32 g_depthMax = 200;

  const f32       aspect  = ctx->data.resolution.z;
  const GeoMatrix camMat  = geo_matrix_rotate_x(math_pi_f32 * 0.5f);
  const GeoMatrix viewMat = geo_matrix_inverse(&camMat);
  const GeoMatrix projMat = geo_matrix_proj_ortho(g_size, g_size / aspect, g_depthMin, g_depthMax);
  const GeoMatrix viewProjMat = geo_matrix_mul(&projMat, &viewMat);

  ctx->data.view        = viewMat;
  ctx->data.viewInv     = camMat;
  ctx->data.proj        = projMat;
  ctx->data.projInv     = geo_matrix_inverse(&projMat);
  ctx->data.viewProj    = viewProjMat;
  ctx->data.viewProjInv = geo_matrix_inverse(&viewProjMat);
  ctx->data.camPosition = geo_vector(0, 0, 0);
  ctx->data.camRotation = geo_quat_forward_to_down;
}

static void painter_push(RendPaintContext* ctx, const RvkPassDraw draw) {
  *dynarray_push_t(&ctx->painter->drawBuffer, RvkPassDraw) = draw;
}

static SceneTags painter_push_geometry(RendPaintContext* ctx, EcsView* drawView, EcsView* graView) {
  SceneTags tagMask = 0;

  EcsIterator* graphicItr = ecs_view_itr(graView);
  for (EcsIterator* drawItr = ecs_view_itr(drawView); ecs_view_walk(drawItr);) {
    RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);
    if (!(rend_draw_flags(draw) & RendDrawFlags_Geometry)) {
      continue; // Shouldn't be included in the geometry pass.
    }
    if (!rend_draw_gather(draw, &ctx->view, ctx->settings)) {
      continue; // Draw culled.
    }
    if (!ecs_view_maybe_jump(graphicItr, rend_draw_graphic(draw))) {
      continue; // Graphic not loaded.
    }
    RvkGraphic* graphic = ecs_view_write_t(graphicItr, RendResGraphicComp)->graphic;
    if (rvk_pass_prepare(ctx->pass, graphic)) {
      painter_push(ctx, rend_draw_output(draw, graphic, null));
      tagMask |= rend_draw_tag_mask(draw);
    }
  }

  return tagMask;
}

static void painter_push_shadow(RendPaintContext* ctx, EcsView* drawView, EcsView* graView) {
  RvkRepository* repo       = rvk_canvas_repository(ctx->painter->canvas);
  EcsIterator*   graphicItr = ecs_view_itr(graView);

  for (EcsIterator* drawItr = ecs_view_itr(drawView); ecs_view_walk(drawItr);) {
    RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);
    if (!(rend_draw_flags(draw) & RendDrawFlags_StandardGeometry)) {
      continue; // Shouldn't be included in the shadow pass.
    }
    if (!rend_draw_gather(draw, &ctx->view, ctx->settings)) {
      continue; // Draw culled.
    }
    if (!ecs_view_maybe_jump(graphicItr, rend_draw_graphic(draw))) {
      continue; // Graphic not loaded.
    }
    RvkGraphic* graphicOriginal = ecs_view_write_t(graphicItr, RendResGraphicComp)->graphic;
    RvkMesh*    mesh            = graphicOriginal->mesh;
    if (!mesh) {
      continue; // Graphic does not have a mesh to draw a shadow for.
    }
    RvkRepositoryId graphicId;
    if (rend_draw_flags(draw) & RendDrawFlags_Skinned) {
      graphicId = RvkRepositoryId_ShadowSkinnedGraphic;
    } else {
      graphicId = RvkRepositoryId_ShadowGraphic;
    }
    RvkGraphic* shadowGraphic = rvk_repository_graphic_get_maybe(repo, graphicId);
    if (!shadowGraphic) {
      continue; // Shadow graphic not loaded.
    }
    if (rvk_pass_prepare(ctx->pass, shadowGraphic) && rvk_pass_prepare_mesh(ctx->pass, mesh)) {
      painter_push(ctx, rend_draw_output(draw, shadowGraphic, mesh));
    }
  }
}

static void painter_push_simple(RendPaintContext* ctx, const RvkRepositoryId id, const Mem data) {
  RvkRepository* repo    = rvk_canvas_repository(ctx->painter->canvas);
  RvkGraphic*    graphic = rvk_repository_graphic_get_maybe(repo, id);
  if (graphic && rvk_pass_prepare(ctx->pass, graphic)) {
    painter_push(ctx, (RvkPassDraw){.graphic = graphic, .instCount = 1, .drawData = data});
  }
}

static void painter_push_ambient(RendPaintContext* ctx) {
  typedef enum {
    AmbientFlags_AmbientOcclusion     = 1 << 0,
    AmbientFlags_AmbientOcclusionBlur = 1 << 1,
  } AmbientFlags;

  typedef struct {
    ALIGNAS(16)
    GeoVector packed; // x: ambientLight, y: mode, z: flags, w: unused.
  } AmbientData;

  const u32    mode  = ctx->settings->ambientMode;
  AmbientFlags flags = 0;
  if (ctx->settings->flags & RendFlags_AmbientOcclusion) {
    flags |= AmbientFlags_AmbientOcclusion;
  }
  if (ctx->settings->flags & RendFlags_AmbientOcclusionBlur) {
    flags |= AmbientFlags_AmbientOcclusionBlur;
  }

  AmbientData* data = alloc_alloc_t(g_alloc_scratch, AmbientData);
  data->packed.x    = ctx->settingsGlobal->lightAmbient;
  data->packed.y    = bits_u32_as_f32(mode);
  data->packed.z    = bits_u32_as_f32(flags);

  const RvkRepositoryId graphicId = ctx->settings->ambientMode == RendAmbientMode_Normal
                                        ? RvkRepositoryId_AmbientGraphic
                                        : RvkRepositoryId_AmbientDebugGraphic;

  painter_push_simple(ctx, graphicId, mem_create(data, sizeof(AmbientData)));
}

static void painter_push_ambient_occlusion(RendPaintContext* ctx) {
  typedef struct {
    ALIGNAS(16)
    f32       radius;
    f32       power;
    GeoVector kernel[rend_ao_kernel_size];
  } AoData;

  AoData* data     = alloc_alloc_t(g_alloc_scratch, AoData);
  data->radius     = ctx->settings->aoRadius;
  data->power      = ctx->settings->aoPower;
  const Mem kernel = mem_create(ctx->settings->aoKernel, sizeof(GeoVector) * rend_ao_kernel_size);
  mem_cpy(array_mem(data->kernel), kernel);

  painter_push_simple(
      ctx, RvkRepositoryId_AmbientOcclusionGraphic, mem_create(data, sizeof(AoData)));
}

static void painter_push_tonemapping(RendPaintContext* ctx) {
  typedef struct {
    ALIGNAS(16)
    f32 exposure;
    u32 mode;
    f32 bloomIntensity;
  } TonemapperData;

  TonemapperData* data = alloc_alloc_t(g_alloc_scratch, TonemapperData);
  data->exposure       = ctx->settings->exposure;
  data->mode           = ctx->settings->tonemapper;
  data->bloomIntensity = ctx->settings->flags & RendFlags_Bloom ? ctx->settings->bloomIntensity : 0;

  painter_push_simple(
      ctx, RvkRepositoryId_TonemapperGraphic, mem_create(data, sizeof(TonemapperData)));
}

static void painter_push_forward(RendPaintContext* ctx, EcsView* drawView, EcsView* graphicView) {
  RendDrawFlags ignoreFlags = 0;
  ignoreFlags |= RendDrawFlags_Geometry; // Ignore geometry (should be drawn in the geometry pass).
  ignoreFlags |= RendDrawFlags_Post;     // Ignore post (should be drawn in the post pass).

  if (ctx->settings->ambientMode != RendAmbientMode_Normal) {
    // Disable lighting when using any of the debug ambient modes.
    ignoreFlags |= RendDrawFlags_Light;
  }

  EcsIterator* graphicItr = ecs_view_itr(graphicView);
  for (EcsIterator* drawItr = ecs_view_itr(drawView); ecs_view_walk(drawItr);) {
    RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);
    if (rend_draw_flags(draw) & ignoreFlags) {
      continue;
    }
    if (!rend_draw_gather(draw, &ctx->view, ctx->settings)) {
      continue; // Draw culled.
    }
    if (!ecs_view_maybe_jump(graphicItr, rend_draw_graphic(draw))) {
      continue; // Graphic not loaded.
    }
    RvkGraphic* graphic = ecs_view_write_t(graphicItr, RendResGraphicComp)->graphic;
    if (rvk_pass_prepare(ctx->pass, graphic)) {
      painter_push(ctx, rend_draw_output(draw, graphic, null));
    }
  }
}

static void painter_push_wireframe(RendPaintContext* ctx, EcsView* drawView, EcsView* graphicView) {
  RvkRepository* repo       = rvk_canvas_repository(ctx->painter->canvas);
  EcsIterator*   graphicItr = ecs_view_itr(graphicView);

  for (EcsIterator* drawItr = ecs_view_itr(drawView); ecs_view_walk(drawItr);) {
    RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);
    if (!(rend_draw_flags(draw) & RendDrawFlags_Geometry)) {
      continue; // Not a draw we can render a wireframe for.
    }
    if (!rend_draw_gather(draw, &ctx->view, ctx->settings)) {
      continue; // Draw culled.
    }
    if (!ecs_view_maybe_jump(graphicItr, rend_draw_graphic(draw))) {
      continue; // Graphic not loaded.
    }
    RvkGraphic* graphicOriginal = ecs_view_write_t(graphicItr, RendResGraphicComp)->graphic;
    RvkMesh*    mesh            = graphicOriginal->mesh;
    if (!mesh) {
      continue; // Graphic does not have a mesh to draw a wireframe for.
    }
    RvkRepositoryId graphicId;
    if (rend_draw_flags(draw) & RendDrawFlags_Terrain) {
      graphicId = RvkRepositoryId_WireframeTerrainGraphic;
    } else if (rend_draw_flags(draw) & RendDrawFlags_Skinned) {
      graphicId = RvkRepositoryId_WireframeSkinnedGraphic;
    } else {
      graphicId = RvkRepositoryId_WireframeGraphic;
    }
    RvkGraphic* graphicWireframe = rvk_repository_graphic_get_maybe(repo, graphicId);
    if (!graphicWireframe) {
      continue; // Wireframe graphic not loaded.
    }
    if (rvk_pass_prepare(ctx->pass, graphicWireframe) && rvk_pass_prepare_mesh(ctx->pass, mesh)) {
      painter_push(ctx, rend_draw_output(draw, graphicWireframe, mesh));
    }
  }
}

static void painter_push_debugskinning(RendPaintContext* ctx, EcsView* drawView, EcsView* graView) {
  RvkRepository*        repository     = rvk_canvas_repository(ctx->painter->canvas);
  const RvkRepositoryId debugGraphicId = RvkRepositoryId_DebugSkinningGraphic;
  RvkGraphic*           debugGraphic   = rvk_repository_graphic_get(repository, debugGraphicId);
  if (!debugGraphic || !rvk_pass_prepare(ctx->pass, debugGraphic)) {
    return; // Debug graphic not ready to be drawn.
  }

  EcsIterator* graphicItr = ecs_view_itr(graView);
  for (EcsIterator* drawItr = ecs_view_itr(drawView); ecs_view_walk(drawItr);) {
    RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);
    if (!(rend_draw_flags(draw) & RendDrawFlags_Skinned)) {
      continue; // Not a skinned draw.
    }
    if (!rend_draw_gather(draw, &ctx->view, ctx->settings)) {
      continue; // Draw culled.
    }
    if (!ecs_view_maybe_jump(graphicItr, rend_draw_graphic(draw))) {
      continue; // Graphic not loaded.
    }
    RvkGraphic* graphicOriginal = ecs_view_write_t(graphicItr, RendResGraphicComp)->graphic;
    RvkMesh*    mesh            = graphicOriginal->mesh;
    diag_assert(mesh);

    if (rvk_pass_prepare_mesh(ctx->pass, mesh)) {
      painter_push(ctx, rend_draw_output(draw, debugGraphic, mesh));
    }
  }
}

static void painter_push_post(RendPaintContext* ctx, EcsView* drawView, EcsView* graView) {
  EcsIterator* graphicItr = ecs_view_itr(graView);
  for (EcsIterator* drawItr = ecs_view_itr(drawView); ecs_view_walk(drawItr);) {
    RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);
    if (!(rend_draw_flags(draw) & RendDrawFlags_Post)) {
      continue; // Shouldn't be included in the post pass.
    }
    if (!rend_draw_gather(draw, &ctx->view, ctx->settings)) {
      continue; // Draw culled.
    }
    if (!ecs_view_maybe_jump(graphicItr, rend_draw_graphic(draw))) {
      continue; // Graphic not loaded.
    }
    RvkGraphic* graphic = ecs_view_write_t(graphicItr, RendResGraphicComp)->graphic;
    if (rvk_pass_prepare(ctx->pass, graphic)) {
      painter_push(ctx, rend_draw_output(draw, graphic, null));
    }
  }
}

static void painter_flush(RendPaintContext* ctx) {
  rvk_pass_begin(ctx->pass);
  {
    dynarray_sort(&ctx->painter->drawBuffer, painter_compare_pass_draw);
    dynarray_for_t(&ctx->painter->drawBuffer, RvkPassDraw, draw) { rvk_pass_draw(ctx->pass, draw); }
    dynarray_clear(&ctx->painter->drawBuffer);
  }
  rvk_pass_end(ctx->pass);
}

static bool rend_canvas_paint(
    RendPainterComp*              painter,
    const RendSettingsComp*       set,
    const RendSettingsGlobalComp* setGlobal,
    const SceneTimeComp*          time,
    const RendLightRendererComp*  light,
    const GapWindowComp*          win,
    const EcsEntityId             camEntity,
    const SceneCameraComp*        cam,
    const SceneTransformComp*     trans,
    EcsView*                      drawView,
    EcsView*                      graphicView) {
  const RvkSize winSize   = painter_win_size(win);
  const f32     winAspect = (f32)winSize.width / (f32)winSize.height;

  const GeoMatrix      camMat  = trans ? scene_transform_matrix(trans) : geo_matrix_ident();
  const GeoMatrix      projMat = cam ? scene_camera_proj(cam, winAspect)
                                     : geo_matrix_proj_ortho(2, 2.0f / winAspect, -100, 100);
  const SceneTagFilter filter  = cam ? cam->filter : (SceneTagFilter){0};

  if (!rvk_canvas_begin(painter->canvas, set, winSize)) {
    return false; // Canvas not ready for rendering.
  }

  RvkImage*     swapchainImage = rvk_canvas_swapchain_image(painter->canvas);
  const RvkSize swapchainSize  = swapchainImage->size;

  // Geometry pass.
  const RvkSize geoSize   = rvk_size_scale(swapchainSize, set->resolutionScale);
  RvkPass*      geoPass   = rvk_canvas_pass(painter->canvas, RendPass_Geometry);
  RvkImage* geoColorRough = rvk_canvas_attach_acquire_color(painter->canvas, geoPass, 0, geoSize);
  RvkImage* geoNormTags   = rvk_canvas_attach_acquire_color(painter->canvas, geoPass, 1, geoSize);
  RvkImage* geoDepth      = rvk_canvas_attach_acquire_depth(painter->canvas, geoPass, geoSize);
  SceneTags geoTagMask;
  {
    RendPaintContext ctx = painter_context(
        &camMat, &projMat, camEntity, filter, painter, set, setGlobal, time, geoPass, geoSize);
    if (set->flags & RendFlags_DebugCamera) {
      painter_set_debug_camera(&ctx);
    }
    rvk_pass_stage_global_data(geoPass, mem_var(ctx.data));
    rvk_pass_stage_attach_color(geoPass, geoColorRough, 0);
    rvk_pass_stage_attach_color(geoPass, geoNormTags, 1);
    rvk_pass_stage_attach_depth(geoPass, geoDepth);
    geoTagMask = painter_push_geometry(&ctx, drawView, graphicView);
    painter_flush(&ctx);
  }

  // Shadow pass.
  const RvkSize shadowSize = rend_light_has_shadow(light)
                                 ? (RvkSize){set->shadowResolution, set->shadowResolution}
                                 : (RvkSize){1, 1};
  RvkPass*      shadowPass = rvk_canvas_pass(painter->canvas, RendPass_Shadow);
  RvkImage* shadowDepth = rvk_canvas_attach_acquire_depth(painter->canvas, shadowPass, shadowSize);
  if (rend_light_has_shadow(light)) {
    const GeoMatrix* sTrans = rend_light_shadow_trans(light);
    const GeoMatrix* sProj  = rend_light_shadow_proj(light);
    RendPaintContext ctx    = painter_context(
        sTrans, sProj, camEntity, filter, painter, set, setGlobal, time, shadowPass, shadowSize);
    rvk_pass_stage_global_data(shadowPass, mem_var(ctx.data));
    rvk_pass_stage_attach_depth(shadowPass, shadowDepth);
    painter_push_shadow(&ctx, drawView, graphicView);
    painter_flush(&ctx);
  } else {
    rvk_canvas_img_clear_depth(painter->canvas, shadowDepth, 0);
  }

  // Ambient occlusion.
  const RvkSize aoSize   = set->flags & RendFlags_AmbientOcclusion
                               ? rvk_size_scale(geoSize, set->aoResolutionScale)
                               : (RvkSize){1, 1};
  RvkPass*      aoPass   = rvk_canvas_pass(painter->canvas, RendPass_AmbientOcclusion);
  RvkImage*     aoBuffer = rvk_canvas_attach_acquire_color(painter->canvas, aoPass, 0, aoSize);
  if (set->flags & RendFlags_AmbientOcclusion) {
    RendPaintContext ctx = painter_context(
        &camMat, &projMat, camEntity, filter, painter, set, setGlobal, time, aoPass, aoSize);
    if (set->flags & RendFlags_DebugCamera) {
      painter_set_debug_camera(&ctx);
    }
    rvk_pass_stage_global_data(aoPass, mem_var(ctx.data));
    rvk_pass_stage_global_image(aoPass, geoNormTags, 0);
    rvk_pass_stage_global_image(aoPass, geoDepth, 1);
    rvk_pass_stage_attach_color(aoPass, aoBuffer, 0);
    painter_push_ambient_occlusion(&ctx);
    painter_flush(&ctx);
  } else {
    rvk_canvas_img_clear_color(painter->canvas, aoBuffer, geo_color_white);
  }

  // Forward pass.
  const RvkSize fwdSize  = rvk_size_scale(swapchainSize, set->resolutionScale);
  RvkPass*      fwdPass  = rvk_canvas_pass(painter->canvas, RendPass_Forward);
  RvkImage*     fwdColor = rvk_canvas_attach_acquire_color(painter->canvas, fwdPass, 0, fwdSize);
  RvkImage*     fwdDepth = rvk_canvas_attach_acquire_depth(painter->canvas, fwdPass, fwdSize);
  {
    rvk_canvas_img_copy(painter->canvas, geoDepth, fwdDepth); // Initialize to the geometry depth.

    RendPaintContext ctx = painter_context(
        &camMat, &projMat, camEntity, filter, painter, set, setGlobal, time, fwdPass, fwdSize);
    if (set->flags & RendFlags_DebugCamera) {
      painter_set_debug_camera(&ctx);
    }
    rvk_pass_stage_global_data(fwdPass, mem_var(ctx.data));
    rvk_pass_stage_global_image(fwdPass, geoColorRough, 0);
    rvk_pass_stage_global_image(fwdPass, geoNormTags, 1);
    rvk_pass_stage_global_image(fwdPass, geoDepth, 2);
    rvk_pass_stage_global_image(fwdPass, aoBuffer, 3);
    rvk_pass_stage_global_shadow(fwdPass, shadowDepth, 4);
    rvk_pass_stage_attach_color(fwdPass, fwdColor, 0);
    rvk_pass_stage_attach_depth(fwdPass, fwdDepth);
    painter_push_ambient(&ctx);
    painter_push_simple(&ctx, RvkRepositoryId_SkyGraphic, mem_empty);
    if (geoTagMask & SceneTags_Outline) {
      painter_push_simple(&ctx, RvkRepositoryId_OutlineGraphic, mem_empty);
    }
    painter_push_forward(&ctx, drawView, graphicView);
    if (set->flags & RendFlags_Wireframe) {
      painter_push_wireframe(&ctx, drawView, graphicView);
    }
    if (set->flags & RendFlags_DebugSkinning) {
      painter_push_debugskinning(&ctx, drawView, graphicView);
    }
    painter_flush(&ctx);
  }

  rvk_canvas_attach_release(painter->canvas, geoColorRough);
  rvk_canvas_attach_release(painter->canvas, geoNormTags);
  rvk_canvas_attach_release(painter->canvas, geoDepth);
  rvk_canvas_attach_release(painter->canvas, aoBuffer);
  rvk_canvas_attach_release(painter->canvas, fwdDepth);

  // Bloom pass.
  RvkPass*  bloomPass = rvk_canvas_pass(painter->canvas, RendPass_Bloom);
  RvkImage* bloomOutput;
  if (set->flags & RendFlags_Bloom && set->bloomIntensity > f32_epsilon) {
    // TODO: 'RendPaintContext' is too heavy weight, we don't need any global data here.
    RendPaintContext ctx = painter_context(
        &camMat, &projMat, camEntity, filter, painter, set, setGlobal, time, bloomPass, fwdSize);

    RvkSize   size = fwdSize;
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
      rvk_pass_stage_global_image(bloomPass, i == 0 ? fwdColor : images[i - 1], 0);
      rvk_pass_stage_attach_color(bloomPass, images[i], 0);
      painter_push_simple(&ctx, RvkRepositoryId_BloomDownGraphic, mem_empty);
      painter_flush(&ctx);
    }

    // Render up samples.
    for (u32 i = set->bloomSteps; i-- > 1;) {
      rvk_pass_stage_global_image(bloomPass, images[i], 0);
      rvk_pass_stage_attach_color(bloomPass, images[i - 1], 0);
      painter_push_simple(&ctx, RvkRepositoryId_BloomUpGraphic, mem_var(bloomData));
      painter_flush(&ctx);
    }

    // Keep the largest image as the output, release the others.
    bloomOutput = images[0];
    for (u32 i = 1; i != set->bloomSteps; ++i) {
      rvk_canvas_attach_release(painter->canvas, images[i]);
    }
  } else {
    bloomOutput = rvk_canvas_attach_acquire_color(painter->canvas, bloomPass, 0, (RvkSize){1, 1});
    rvk_canvas_img_clear_color(painter->canvas, bloomOutput, geo_color_white);
  }

  // Post pass.
  const RvkSize postSize = swapchainSize;
  RvkPass*      postPass = rvk_canvas_pass(painter->canvas, RendPass_Post);
  {
    RendPaintContext ctx = painter_context(
        &camMat, &projMat, camEntity, filter, painter, set, setGlobal, time, postPass, postSize);
    if (set->flags & RendFlags_DebugCamera) {
      painter_set_debug_camera(&ctx);
    }
    rvk_pass_stage_global_data(postPass, mem_var(ctx.data));
    rvk_pass_stage_global_image(postPass, fwdColor, 0);
    rvk_pass_stage_global_image(postPass, bloomOutput, 1);
    rvk_pass_stage_global_shadow(postPass, shadowDepth, 4);
    rvk_pass_stage_attach_color(postPass, swapchainImage, 0);
    painter_push_tonemapping(&ctx);
    painter_push_post(&ctx, drawView, graphicView);
    if (set->flags & RendFlags_DebugShadow) {
      painter_push_simple(&ctx, RvkRepositoryId_DebugShadowGraphic, mem_empty);
    }
    painter_flush(&ctx);
  }

  rvk_canvas_attach_release(painter->canvas, fwdColor);
  rvk_canvas_attach_release(painter->canvas, shadowDepth);
  rvk_canvas_attach_release(painter->canvas, bloomOutput);

  // Finish the frame.
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
    const GapWindowComp* windowComp = ecs_view_read_t(itr, GapWindowComp);
    if (gap_window_events(windowComp) & GapWindowEvents_Initializing) {
      continue;
    }
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_add_t(
        world,
        entity,
        RendPainterComp,
        .drawBuffer = dynarray_create_t(g_alloc_heap, RvkPassDraw, 1024),
        .canvas     = rvk_canvas_create(plat->device, windowComp, g_passConfig));

    if (!ecs_world_has_t(world, entity, RendSettingsComp)) {
      RendSettingsComp* settings = ecs_world_add_t(world, entity, RendSettingsComp);
      rend_settings_to_default(settings);
    }
  }
}

ecs_system_define(RendPainterDrawBatchesSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const RendSettingsGlobalComp* settingsGlobal = ecs_view_read_t(globalItr, RendSettingsGlobalComp);
  const SceneTimeComp*          time           = ecs_view_read_t(globalItr, SceneTimeComp);
  const RendLightRendererComp*  light          = ecs_view_read_t(globalItr, RendLightRendererComp);

  EcsView* painterView = ecs_world_view_t(world, PainterUpdateView);
  EcsView* drawView    = ecs_world_view_t(world, DrawView);
  EcsView* graphicView = ecs_world_view_t(world, GraphicView);

  bool anyPainterDrawn = false;
  for (EcsIterator* itr = ecs_view_itr(painterView); ecs_view_walk(itr);) {
    const EcsEntityId         entity    = ecs_view_entity(itr);
    const GapWindowComp*      win       = ecs_view_read_t(itr, GapWindowComp);
    RendPainterComp*          painter   = ecs_view_write_t(itr, RendPainterComp);
    const RendSettingsComp*   settings  = ecs_view_read_t(itr, RendSettingsComp);
    const SceneCameraComp*    camera    = ecs_view_read_t(itr, SceneCameraComp);
    const SceneTransformComp* transform = ecs_view_read_t(itr, SceneTransformComp);

    anyPainterDrawn |= rend_canvas_paint(
        painter,
        settings,
        settingsGlobal,
        time,
        light,
        win,
        entity,
        camera,
        transform,
        drawView,
        graphicView);
  }

  if (!anyPainterDrawn) {
    /**
     * If no painter was drawn this frame (for example because they are all minimized) we sleep
     * the thread to avoid wasting cpu cycles.
     */
    thread_sleep(time_second / 60);
  }
}

ecs_module_init(rend_painter_module) {
  ecs_register_comp(RendPainterComp, .destructor = ecs_destruct_painter);

  ecs_register_view(GlobalView);
  ecs_register_view(DrawView);
  ecs_register_view(GraphicView);
  ecs_register_view(PainterCreateView);
  ecs_register_view(PainterUpdateView);

  ecs_register_system(
      RendPainterCreateSys, ecs_view_id(GlobalView), ecs_view_id(PainterCreateView));

  ecs_register_system(
      RendPainterDrawBatchesSys,
      ecs_view_id(GlobalView),
      ecs_view_id(PainterUpdateView),
      ecs_view_id(DrawView),
      ecs_view_id(GraphicView));

  ecs_order(RendPainterDrawBatchesSys, RendOrder_DrawExecute);
}

void rend_painter_teardown(EcsWorld* world, const EcsEntityId entity) {
  ecs_world_remove_t(world, entity, RendPainterComp);
}
