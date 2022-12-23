#include "core_alloc.h"
#include "core_diag.h"
#include "core_thread.h"
#include "ecs_utils.h"
#include "gap_window.h"
#include "rend_register.h"
#include "scene_camera.h"
#include "scene_transform.h"

#include "draw_internal.h"
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

typedef struct {
  ALIGNAS(16)
  GeoMatrix proj, projInv, viewProj, viewProjInv;
  GeoVector camPosition;
  GeoQuat   camRotation;
  f32       aspectRatio;
} RendPainterGlobalData;

ASSERT(sizeof(RendPainterGlobalData) == 304, "Size needs to match the size defined in glsl");

ecs_view_define(GlobalView) {
  ecs_access_write(RendPlatformComp);
  ecs_access_without(RendResetComp);
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

static GeoMatrix painter_proj_matrix(const GapWindowComp* win, const SceneCameraComp* cam) {
  const GapVector winSize = gap_window_param(win, GapParam_WindowSize);
  const f32       aspect  = (f32)winSize.width / (f32)winSize.height;
  if (LIKELY(cam)) {
    return scene_camera_proj(cam, aspect);
  }
  // Fall back to a basic orthographic projection.
  return geo_matrix_proj_ortho(2.0f, 2.0f / aspect, -100.0f, 100.0f);
}

static GeoMatrix painter_view_proj_matrix(const GeoMatrix* proj, const SceneTransformComp* trans) {
  const GeoMatrix view = LIKELY(trans) ? scene_transform_matrix_inv(trans) : geo_matrix_ident();
  return geo_matrix_mul(proj, &view);
}

static void painter_push(RendPainterComp* painter, const RvkPassDraw draw) {
  *dynarray_push_t(&painter->drawBuffer, RvkPassDraw) = draw;
}

static void painter_push_geometry(
    RendPainterComp*        painter,
    const RendSettingsComp* settings,
    const RendView*         view,
    RvkPass*                pass,
    EcsView*                drawView,
    EcsView*                graphicView) {

  EcsIterator* graphicItr = ecs_view_itr(graphicView);
  for (EcsIterator* drawItr = ecs_view_itr(drawView); ecs_view_walk(drawItr);) {
    RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);
    if (!(rend_draw_flags(draw) & RendDrawFlags_Geometry)) {
      continue; // Not a geometry draw.
    }
    if (!rend_draw_gather(draw, view, settings)) {
      continue; // Draw culled.
    }
    if (!ecs_view_maybe_jump(graphicItr, rend_draw_graphic(draw))) {
      continue; // Graphic not loaded.
    }
    RvkGraphic* graphic = ecs_view_write_t(graphicItr, RendResGraphicComp)->graphic;
    if (rvk_pass_prepare(pass, graphic)) {
      painter_push(painter, rend_draw_output(draw, graphic, null));
    }
  }
}

static void painter_push_simple(RendPainterComp* painter, RvkPass* pass, const RvkRepositoryId id) {
  RvkRepository* repo    = rvk_canvas_repository(painter->canvas);
  RvkGraphic*    graphic = rvk_repository_graphic_get_maybe(repo, id);
  if (graphic && rvk_pass_prepare(pass, graphic)) {
    painter_push(painter, (RvkPassDraw){.graphic = graphic, .instCount = 1});
  }
}

static void painter_push_shade_debug(
    RendPainterComp* painter, const RendSettingsComp* settings, RvkPass* pass) {

  RvkRepository* repo = rvk_canvas_repository(painter->canvas);
  RvkGraphic* graphic = rvk_repository_graphic_get_maybe(repo, RvkRepositoryId_ShadeDebugGraphic);
  if (!graphic || !rvk_pass_prepare(pass, graphic)) {
    return; // Graphic not ready to be drawn.
  }

  typedef struct {
    ALIGNAS(16)
    u32 mode;
  } ShadeDebugData;

  ShadeDebugData* data = alloc_alloc_t(g_alloc_scratch, ShadeDebugData);
  *data                = (ShadeDebugData){.mode = (u32)settings->shadeDebug};

  painter_push(
      painter,
      (RvkPassDraw){
          .graphic        = graphic,
          .instCount      = 1,
          .instData       = mem_create(data, sizeof(ShadeDebugData)),
          .instDataStride = sizeof(ShadeDebugData),
      });
}

static void painter_push_forward(
    RendPainterComp*        painter,
    const RendSettingsComp* settings,
    const RendView*         view,
    RvkPass*                pass,
    EcsView*                drawView,
    EcsView*                graphicView) {

  EcsIterator* graphicItr = ecs_view_itr(graphicView);
  for (EcsIterator* drawItr = ecs_view_itr(drawView); ecs_view_walk(drawItr);) {
    RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);
    if (rend_draw_flags(draw) & RendDrawFlags_Geometry) {
      continue; // Ignore geometry (should be drawn in the geometry pass).
    }
    if (!rend_draw_gather(draw, view, settings)) {
      continue; // Draw culled.
    }
    if (!ecs_view_maybe_jump(graphicItr, rend_draw_graphic(draw))) {
      continue; // Graphic not loaded.
    }
    RvkGraphic* graphic = ecs_view_write_t(graphicItr, RendResGraphicComp)->graphic;
    if (rvk_pass_prepare(pass, graphic)) {
      painter_push(painter, rend_draw_output(draw, graphic, null));
    }
  }
}

static void painter_push_wireframe(
    RendPainterComp*        painter,
    const RendSettingsComp* settings,
    const RendView*         view,
    RvkPass*                pass,
    EcsView*                drawView,
    EcsView*                graphicView) {

  RvkRepository* repo       = rvk_canvas_repository(painter->canvas);
  EcsIterator*   graphicItr = ecs_view_itr(graphicView);

  for (EcsIterator* drawItr = ecs_view_itr(drawView); ecs_view_walk(drawItr);) {
    RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);
    if ((rend_draw_flags(draw) & (RendDrawFlags_StandardGeometry | RendDrawFlags_Terrain)) == 0) {
      continue; // Not a draw we can render a wireframe for.
    }
    if (!rend_draw_gather(draw, view, settings)) {
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
    if (rvk_pass_prepare(pass, graphicWireframe) && rvk_pass_prepare_mesh(pass, mesh)) {
      painter_push(painter, rend_draw_output(draw, graphicWireframe, mesh));
    }
  }
}

static void painter_push_debugskinning(
    RendPainterComp*        painter,
    const RendSettingsComp* settings,
    const RendView*         view,
    RvkPass*                pass,
    EcsView*                drawView,
    EcsView*                graphicView) {

  RvkRepository*        repository     = rvk_canvas_repository(painter->canvas);
  const RvkRepositoryId debugGraphicId = RvkRepositoryId_DebugSkinningGraphic;
  RvkGraphic*           debugGraphic   = rvk_repository_graphic_get(repository, debugGraphicId);
  if (!debugGraphic || !rvk_pass_prepare(pass, debugGraphic)) {
    return; // Debug graphic not ready to be drawn.
  }

  EcsIterator* graphicItr = ecs_view_itr(graphicView);
  for (EcsIterator* drawItr = ecs_view_itr(drawView); ecs_view_walk(drawItr);) {
    RendDrawComp*       draw      = ecs_view_write_t(drawItr, RendDrawComp);
    const RendDrawFlags drawFlags = rend_draw_flags(draw);
    if ((drawFlags & RendDrawFlags_Skinned) == 0) {
      continue; // Not a skinned draw.
    }
    if (!rend_draw_gather(draw, view, settings)) {
      continue; // Draw culled.
    }
    if (!ecs_view_maybe_jump(graphicItr, rend_draw_graphic(draw))) {
      continue; // Graphic not loaded.
    }
    RvkGraphic* graphicOriginal = ecs_view_write_t(graphicItr, RendResGraphicComp)->graphic;
    RvkMesh*    mesh            = graphicOriginal->mesh;
    diag_assert(mesh);

    if (rvk_pass_prepare_mesh(pass, mesh)) {
      painter_push(painter, rend_draw_output(draw, debugGraphic, mesh));
    }
  }
}

static void painter_flush(RendPainterComp* painter, RvkPass* pass) {
  dynarray_sort(&painter->drawBuffer, painter_compare_pass_draw);
  dynarray_for_t(&painter->drawBuffer, RvkPassDraw, draw) { rvk_pass_draw(pass, draw); }
  dynarray_clear(&painter->drawBuffer);
}

static bool painter_draw(
    RendPainterComp*          painter,
    const RendSettingsComp*   settings,
    const GapWindowComp*      win,
    const EcsEntityId         camEntity,
    const SceneCameraComp*    cam,
    const SceneTransformComp* trans,
    EcsView*                  drawView,
    EcsView*                  graphicView) {

  const GapVector winSize    = gap_window_param(win, GapParam_WindowSize);
  const RvkSize   resolution = rvk_size((u16)winSize.width, (u16)winSize.height);
  const bool      draw       = rvk_canvas_begin(painter->canvas, settings, resolution);
  if (draw) {
    const GeoVector      origin   = trans ? trans->position : geo_vector(0);
    const GeoMatrix      proj     = painter_proj_matrix(win, cam);
    const GeoMatrix      viewProj = painter_view_proj_matrix(&proj, trans);
    const SceneTagFilter filter   = cam ? cam->filter : (SceneTagFilter){0};
    const RendView       view     = rend_view_create(camEntity, origin, &viewProj, filter);

    const RendPainterGlobalData globalData = {
        .proj        = proj,
        .projInv     = geo_matrix_inverse(&proj),
        .viewProj    = viewProj,
        .viewProjInv = geo_matrix_inverse(&viewProj),
        .camPosition = trans ? trans->position : geo_vector(0),
        .camRotation = trans ? trans->rotation : geo_quat_ident,
        .aspectRatio = (f32)resolution.width / (f32)resolution.height,
    };

    // Geometry pass.
    RvkPass* geometryPass = rvk_canvas_pass(painter->canvas, RvkRenderPass_Geometry);
    rvk_pass_bind_global_data(geometryPass, mem_var(globalData));
    painter_push_geometry(painter, settings, &view, geometryPass, drawView, graphicView);
    rvk_pass_begin(geometryPass, geo_color_clear);
    painter_flush(painter, geometryPass);
    rvk_pass_end(geometryPass);

    // Forward pass.
    RvkPass* forwardPass = rvk_canvas_pass(painter->canvas, RvkRenderPass_Forward);
    rvk_pass_use_depth(forwardPass, rvk_pass_output(geometryPass, RvkPassOutput_Depth));
    rvk_pass_bind_global_data(forwardPass, mem_var(globalData));
    rvk_pass_bind_global_image(forwardPass, rvk_pass_output(geometryPass, RvkPassOutput_Color1), 0);
    rvk_pass_bind_global_image(forwardPass, rvk_pass_output(geometryPass, RvkPassOutput_Color2), 1);
    rvk_pass_bind_global_image(forwardPass, rvk_pass_output(geometryPass, RvkPassOutput_Depth), 2);
    if (settings->shadeDebug == RendShadeDebug_None) {
      painter_push_simple(painter, forwardPass, RvkRepositoryId_ShadeBaseGraphic);
    } else {
      painter_push_shade_debug(painter, settings, forwardPass);
    }
    painter_push_simple(painter, forwardPass, RvkRepositoryId_SkyGraphic);
    painter_push_forward(painter, settings, &view, forwardPass, drawView, graphicView);
    if (settings->flags & RendFlags_Wireframe) {
      painter_push_wireframe(painter, settings, &view, forwardPass, drawView, graphicView);
    }
    if (settings->flags & RendFlags_DebugSkinning) {
      painter_push_debugskinning(painter, settings, &view, forwardPass, drawView, graphicView);
    }
    rvk_pass_begin(forwardPass, geo_color_clear);
    painter_flush(painter, forwardPass);
    rvk_pass_end(forwardPass);

    // Finish the frame.
    rvk_canvas_end(painter->canvas);
  }
  return draw;
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
  /**
   * Dependency on the global 'RendPlatformComp' to ensure ordering between this and the
   * platform update system.
   */
  if (!globalItr) {
    return;
  }

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
    anyPainterDrawn |=
        painter_draw(painter, settings, win, entity, camera, transform, drawView, graphicView);
  }

  if (!anyPainterDrawn) {
    /**
     * If no painter was drawn this frame (for example because they are all minimized) we sleep
     * the thread to avoid wasting cpu cycles.
     */
    thread_sleep(time_second / 30);
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
