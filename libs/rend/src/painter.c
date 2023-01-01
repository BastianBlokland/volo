#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
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
#include "rvk/mesh_internal.h"
#include "rvk/pass_internal.h"
#include "rvk/repository_internal.h"

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
    RvkPass*                      pass) {
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
              .resolution.x = rvk_pass_size(pass).width,
              .resolution.y = rvk_pass_size(pass).height,
              .resolution.z = (f32)rvk_pass_size(pass).width / (f32)rvk_pass_size(pass).height,
              .time.x       = scene_time_seconds(time),
              .time.y       = scene_real_time_seconds(time),
          },
      .view = rend_view_create(sceneCameraEntity, cameraPosition, &viewProjMatrix, sceneFilter),
  };
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

static void painter_push_simple(RendPaintContext* ctx, const RvkRepositoryId id) {
  RvkRepository* repo    = rvk_canvas_repository(ctx->painter->canvas);
  RvkGraphic*    graphic = rvk_repository_graphic_get_maybe(repo, id);
  if (graphic && rvk_pass_prepare(ctx->pass, graphic)) {
    painter_push(ctx, (RvkPassDraw){.graphic = graphic, .instCount = 1});
  }
}

static void painter_push_compose(RendPaintContext* ctx) {
  RvkRepository*        repo      = rvk_canvas_repository(ctx->painter->canvas);
  const RvkRepositoryId graphicId = ctx->settings->composeMode == RendComposeMode_Normal
                                        ? RvkRepositoryId_ComposeGraphic
                                        : RvkRepositoryId_ComposeDebugGraphic;
  RvkGraphic*           graphic   = rvk_repository_graphic_get_maybe(repo, graphicId);
  if (graphic && rvk_pass_prepare(ctx->pass, graphic)) {
    const u32 mode  = ctx->settings->composeMode;
    u32       flags = 0;
    if (ctx->settings->flags & RendFlags_AmbientOcclusion) {
      flags |= 1 << 0;
    }

    typedef struct {
      ALIGNAS(16)
      GeoVector packed; // x: ambient, y: mode, z: flags, w: unused.
    } ComposeData;

    ComposeData* data = alloc_alloc_t(g_alloc_scratch, ComposeData);
    data->packed.x    = ctx->settingsGlobal->lightAmbient;
    data->packed.y    = bits_u32_as_f32(mode);
    data->packed.z    = bits_u32_as_f32(flags);

    painter_push(
        ctx,
        (RvkPassDraw){
            .graphic   = graphic,
            .instCount = 1,
            .drawData  = mem_create(data, sizeof(ComposeData)),
        });
  }
}

static void painter_push_ambient_occlusion(RendPaintContext* ctx) {
  RvkRepository*        repo      = rvk_canvas_repository(ctx->painter->canvas);
  const RvkRepositoryId graphicId = RvkRepositoryId_AmbientOcclusionGraphic;
  RvkGraphic*           graphic   = rvk_repository_graphic_get_maybe(repo, graphicId);
  if (graphic && rvk_pass_prepare(ctx->pass, graphic)) {
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

    painter_push(
        ctx,
        (RvkPassDraw){
            .graphic   = graphic,
            .instCount = 1,
            .drawData  = mem_create(data, sizeof(AoData)),
        });
  }
}

static void painter_push_forward(RendPaintContext* ctx, EcsView* drawView, EcsView* graphicView) {
  RendDrawFlags ignoreFlags = 0;
  ignoreFlags |= RendDrawFlags_Geometry; // Ignore geometry (should be drawn in the geometry pass).

  if (ctx->settings->composeMode != RendComposeMode_Normal) {
    // Disable lighting when using any of the debug compose modes.
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

static void painter_flush(RendPaintContext* ctx) {
  dynarray_sort(&ctx->painter->drawBuffer, painter_compare_pass_draw);
  dynarray_for_t(&ctx->painter->drawBuffer, RvkPassDraw, draw) { rvk_pass_draw(ctx->pass, draw); }
  dynarray_clear(&ctx->painter->drawBuffer);
}

static bool rend_canvas_paint(
    RendPainterComp*              painter,
    const RendSettingsComp*       settings,
    const RendSettingsGlobalComp* settingsGlobal,
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

  if (!rvk_canvas_begin(painter->canvas, settings, winSize)) {
    return false; // Canvas not ready for rendering.
  }

  // Geometry pass.
  RvkPass*  geoPass = rvk_canvas_pass(painter->canvas, RvkRenderPass_Geometry);
  SceneTags geoTagMask;
  {
    RendPaintContext ctx = painter_context(
        &camMat, &projMat, camEntity, filter, painter, settings, settingsGlobal, time, geoPass);
    rvk_pass_bind_global_data(geoPass, mem_var(ctx.data));
    geoTagMask = painter_push_geometry(&ctx, drawView, graphicView);
    rvk_pass_begin(geoPass, geo_color_clear);
    painter_flush(&ctx);
    rvk_pass_end(geoPass);
  }

  // Shadow pass.
  RvkPass* shadowPass = rvk_canvas_pass(painter->canvas, RvkRenderPass_Shadow);
  if (rend_light_has_shadow(light)) {
    const GeoMatrix* sTrans = rend_light_shadow_trans(light);
    const GeoMatrix* sProj  = rend_light_shadow_proj(light);
    RendPaintContext ctx    = painter_context(
        sTrans, sProj, camEntity, filter, painter, settings, settingsGlobal, time, shadowPass);
    rvk_pass_bind_global_data(shadowPass, mem_var(ctx.data));
    painter_push_shadow(&ctx, drawView, graphicView);
    rvk_pass_begin(shadowPass, geo_color_clear);
    painter_flush(&ctx);
    rvk_pass_end(shadowPass);
  }

  // Ambient occlusion.
  RvkPass* aoPass = rvk_canvas_pass(painter->canvas, RvkRenderPass_AmbientOcclusion);
  if (settings->flags & RendFlags_AmbientOcclusion) {
    RendPaintContext ctx = painter_context(
        &camMat, &projMat, camEntity, filter, painter, settings, settingsGlobal, time, aoPass);
    rvk_pass_bind_global_data(aoPass, mem_var(ctx.data));
    rvk_pass_bind_global_image(aoPass, rvk_pass_output(geoPass, RvkPassOutput_Color2), 0);
    rvk_pass_bind_global_image(aoPass, rvk_pass_output(geoPass, RvkPassOutput_Depth), 1);
    painter_push_ambient_occlusion(&ctx);
    rvk_pass_begin(aoPass, geo_color_clear);
    painter_flush(&ctx);
    rvk_pass_end(aoPass);
  }

  // Forward pass.
  RvkPass* fwdPass = rvk_canvas_pass(painter->canvas, RvkRenderPass_Forward);
  {
    RendPaintContext ctx = painter_context(
        &camMat, &projMat, camEntity, filter, painter, settings, settingsGlobal, time, fwdPass);
    rvk_pass_use_depth(fwdPass, rvk_pass_output(geoPass, RvkPassOutput_Depth));
    rvk_pass_bind_global_data(fwdPass, mem_var(ctx.data));
    rvk_pass_bind_global_image(fwdPass, rvk_pass_output(geoPass, RvkPassOutput_Color1), 0);
    rvk_pass_bind_global_image(fwdPass, rvk_pass_output(geoPass, RvkPassOutput_Color2), 1);
    rvk_pass_bind_global_image(fwdPass, rvk_pass_output(geoPass, RvkPassOutput_Depth), 2);
    rvk_pass_bind_global_image(fwdPass, rvk_pass_output(aoPass, RvkPassOutput_Color1), 3);
    rvk_pass_bind_global_image(fwdPass, rvk_pass_output(shadowPass, RvkPassOutput_Depth), 4);
    painter_push_compose(&ctx);
    painter_push_simple(&ctx, RvkRepositoryId_SkyGraphic);
    if (geoTagMask & SceneTags_Outline) {
      painter_push_simple(&ctx, RvkRepositoryId_OutlineGraphic);
    }
    painter_push_forward(&ctx, drawView, graphicView);
    if (settings->flags & RendFlags_Wireframe) {
      painter_push_wireframe(&ctx, drawView, graphicView);
    }
    if (settings->flags & RendFlags_DebugSkinning) {
      painter_push_debugskinning(&ctx, drawView, graphicView);
    }
    if (settings->flags & RendFlags_DebugShadow) {
      painter_push_simple(&ctx, RvkRepositoryId_DebugShadowGraphic);
    }
    rvk_pass_begin(fwdPass, geo_color_clear);
    painter_flush(&ctx);
    rvk_pass_end(fwdPass);
  }

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
        .canvas     = rvk_canvas_create(plat->device, windowComp));

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
