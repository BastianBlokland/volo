#include "core_alloc.h"
#include "core_thread.h"
#include "core_time.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "gap_input.h"
#include "gap_window.h"
#include "rend_instance.h"
#include "scene_camera.h"
#include "scene_transform.h"

#include "platform_internal.h"
#include "resource_internal.h"
#include "rvk/canvas_internal.h"
#include "rvk/pass_internal.h"

ecs_comp_define(RendPainterComp) { RvkCanvas* canvas; };

static void ecs_destruct_painter_comp(void* data) {
  RendPainterComp* comp = data;
  if (comp->canvas) {
    rvk_canvas_destroy(comp->canvas);
  }
}

ecs_view_define(PlatformView) { ecs_access_write(RendPlatformComp); };
ecs_view_define(RenderableView) { ecs_access_read(RendInstanceComp); };
ecs_view_define(GraphicView) { ecs_access_write(RendGraphicComp); };

ecs_view_define(PainterCreateView) {
  ecs_access_read(GapWindowComp);
  ecs_access_without(RendPainterComp);
};

ecs_view_define(PainterUpdateView) {
  ecs_access_read(GapWindowComp);
  ecs_access_write(RendPainterComp);

  ecs_access_maybe_read(SceneCameraComp);
  ecs_access_maybe_read(SceneTransformComp);
};

static GeoMatrix painter_view_proj_matrix(
    const GapWindowComp* win, const SceneCameraComp* cam, const SceneTransformComp* trans) {

  const GapVector winSize = gap_window_param(win, GapParam_WindowSize);
  const f32       aspect  = (f32)winSize.width / (f32)winSize.height;

  const GeoMatrix proj = cam ? scene_camera_proj(cam, aspect)
                             : geo_matrix_proj_ortho(2.0f, 2.0f / aspect, -1.0f, 1.0f);
  const GeoMatrix view = trans ? scene_transform_matrix_inv(trans) : geo_matrix_ident();
  return geo_matrix_mul(&proj, &view);
}

static void painter_draw_forward(
    const GeoMatrix viewProjMatrix,
    RvkPass*        forwardPass,
    EcsView*        renderableView,
    EcsView*        graphicView) {
  DynArray drawBuffer = dynarray_create_t(g_alloc_scratch, RvkPassDraw, 1024);

  // Prepare draws.
  EcsIterator* renderableItr = ecs_view_itr(renderableView);
  EcsIterator* graphicItr    = ecs_view_itr(graphicView);
  while (ecs_view_walk(renderableItr)) {
    const RendInstanceComp* instanceComp = ecs_view_read_t(renderableItr, RendInstanceComp);
    if (!ecs_view_contains(graphicView, instanceComp->asset)) {
      continue;
    }
    ecs_view_jump(graphicItr, instanceComp->asset);
    RendGraphicComp* graphicComp = ecs_view_write_t(graphicItr, RendGraphicComp);
    if (rvk_pass_prepare(forwardPass, graphicComp->graphic)) {
      *dynarray_push_t(&drawBuffer, RvkPassDraw) = (RvkPassDraw){
          .graphic = graphicComp->graphic,
      };
    }
  }

  // Execute draws.
  rvk_pass_begin(forwardPass, rend_soothing_purple);
  rvk_pass_draw(
      forwardPass,
      mem_var(viewProjMatrix),
      (RvkPassDrawList){
          .values = dynarray_begin_t(&drawBuffer, RvkPassDraw),
          .count  = drawBuffer.size,
      });
  rvk_pass_end(forwardPass);
}

static bool painter_draw(
    RendPainterComp*          painter,
    const GapWindowComp*      win,
    const SceneCameraComp*    cam,
    const SceneTransformComp* trans,
    EcsView*                  renderableView,
    EcsView*                  graphicView) {
  const GapVector winSize  = gap_window_param(win, GapParam_WindowSize);
  const RendSize  rendSize = rend_size((u32)winSize.width, (u32)winSize.height);
  const bool      draw     = rvk_canvas_begin(painter->canvas, rendSize);
  if (draw) {
    const GeoMatrix viewProjMatrix = painter_view_proj_matrix(win, cam, trans);
    RvkPass*        forwardPass    = rvk_canvas_pass_forward(painter->canvas);
    painter_draw_forward(viewProjMatrix, forwardPass, renderableView, graphicView);
    rvk_canvas_end(painter->canvas);
  }
  return draw;
}

ecs_system_define(RendPainterCreateSys) {
  RendPlatformComp* plat = ecs_utils_write_first_t(world, PlatformView, RendPlatformComp);
  if (!plat) {
    return;
  }

  EcsView* painterView = ecs_world_view_t(world, PainterCreateView);
  for (EcsIterator* itr = ecs_view_itr(painterView); ecs_view_walk(itr);) {
    const GapWindowComp* windowComp = ecs_view_read_t(itr, GapWindowComp);
    RvkCanvas*           canvas     = rvk_canvas_create(plat->device, windowComp);
    ecs_world_add_t(world, ecs_view_entity(itr), RendPainterComp, .canvas = canvas);
  }
}

ecs_system_define(RendPainterUpdateSys) {
  EcsView* painterView     = ecs_world_view_t(world, PainterUpdateView);
  EcsView* renderablesView = ecs_world_view_t(world, RenderableView);
  EcsView* graphicView     = ecs_world_view_t(world, GraphicView);

  bool anyPainterDrawn = false;
  for (EcsIterator* itr = ecs_view_itr(painterView); ecs_view_walk(itr);) {
    const GapWindowComp*      win       = ecs_view_read_t(itr, GapWindowComp);
    RendPainterComp*          painter   = ecs_view_write_t(itr, RendPainterComp);
    const SceneCameraComp*    camera    = ecs_view_read_t(itr, SceneCameraComp);
    const SceneTransformComp* transform = ecs_view_read_t(itr, SceneTransformComp);
    anyPainterDrawn |= painter_draw(painter, win, camera, transform, renderablesView, graphicView);
  }

  if (!anyPainterDrawn) {
    /**
     * If no painter was drawn this frame (for example because they are all minimized) we sleep
     * the thread to avoid wasting cpu cycles.
     */
    thread_sleep(time_second / 30);
  }
}

ecs_module_init(rend_canvas_module) {
  ecs_register_comp(RendPainterComp, .destructor = ecs_destruct_painter_comp);

  ecs_register_view(PlatformView);
  ecs_register_view(RenderableView);
  ecs_register_view(GraphicView);
  ecs_register_view(PainterCreateView);
  ecs_register_view(PainterUpdateView);

  ecs_register_system(
      RendPainterCreateSys, ecs_view_id(PlatformView), ecs_view_id(PainterCreateView));

  ecs_register_system(
      RendPainterUpdateSys,
      ecs_view_id(PainterUpdateView),
      ecs_view_id(RenderableView),
      ecs_view_id(GraphicView));
}

void rend_painter_teardown(EcsWorld* world) {
  // Teardown painters.
  EcsView* painterView = ecs_world_view_t(world, PainterUpdateView);
  for (EcsIterator* itr = ecs_view_itr(painterView); ecs_view_walk(itr);) {
    RendPainterComp* comp = ecs_view_write_t(itr, RendPainterComp);
    if (comp->canvas) {
      rvk_canvas_destroy(comp->canvas);
      comp->canvas = null;
    }
  }
}
