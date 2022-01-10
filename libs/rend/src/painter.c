#include "core_alloc.h"
#include "core_diag.h"
#include "core_thread.h"
#include "core_time.h"
#include "ecs_utils.h"
#include "gap_input.h"
#include "gap_window.h"
#include "geo_matrix.h"
#include "rend_register.h"
#include "scene_camera.h"
#include "scene_transform.h"

#include "painter_internal.h"
#include "platform_internal.h"
#include "resource_internal.h"
#include "rvk/canvas_internal.h"
#include "rvk/graphic_internal.h"
#include "rvk/pass_internal.h"

ecs_comp_define_public(RendPainterComp);
ecs_comp_define_public(RendPainterDrawComp);

static void ecs_destruct_painter(void* data) {
  RendPainterComp* comp = data;
  rvk_canvas_destroy(comp->canvas);
}

static void ecs_destruct_draw(void* data) {
  RendPainterDrawComp* comp = data;
  dynarray_destroy(&comp->instances);
}

static void ecs_combine_draw(void* dataA, void* dataB) {
  RendPainterDrawComp* compA = dataA;
  RendPainterDrawComp* compB = dataB;
  diag_assert_msg(
      compA->instances.stride == compB->instances.stride,
      "Only draws with the same data-stride can be combined");

  mem_cpy(
      dynarray_push(&compA->instances, compB->instances.size),
      dynarray_at(&compB->instances, 0, compB->instances.size));
  dynarray_destroy(&compB->instances);
}

typedef struct {
  ALIGNAS(16)
  GeoMatrix viewProj;
  GeoVector camPosition;
  GeoQuat   camRotation;
} RendPainterGlobalData;

ecs_view_define(GlobalView) { ecs_access_write(RendPlatformComp); }
ecs_view_define(DrawView) { ecs_access_read(RendPainterDrawComp); }
ecs_view_define(GraphicView) {
  ecs_access_write(RendResComp);
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

  ecs_access_maybe_read(SceneCameraComp);
  ecs_access_maybe_read(SceneTransformComp);
}

static i8 painter_compare_draw(const void* a, const void* b) {
  const RvkPassDraw* drawA = a;
  const RvkPassDraw* drawB = b;
  return compare_i32(&drawA->graphic->renderOrder, &drawB->graphic->renderOrder);
}

static GeoMatrix painter_view_proj_matrix(
    const GapWindowComp* win, const SceneCameraComp* cam, const SceneTransformComp* trans) {

  const GapVector winSize = gap_window_param(win, GapParam_WindowSize);
  const f32       aspect  = (f32)winSize.width / (f32)winSize.height;

  const GeoMatrix proj = cam ? scene_camera_proj(cam, aspect)
                             : geo_matrix_proj_ortho(2.0f, 2.0f / aspect, -100.0f, 100.0f);
  const GeoMatrix view = trans ? scene_transform_matrix_inv(trans) : geo_matrix_ident();
  return geo_matrix_mul(&proj, &view);
}

static void painter_draw_forward(
    const RendPainterGlobalData* globalData,
    RvkPass*                     forwardPass,
    EcsView*                     drawView,
    EcsView*                     graphicView) {
  DynArray drawBuffer = dynarray_create_t(g_alloc_scratch, RvkPassDraw, 1024);

  // Prepare draws.
  EcsIterator* graphicItr = ecs_view_itr(graphicView);
  for (EcsIterator* drawItr = ecs_view_itr(drawView); ecs_view_walk(drawItr);) {
    const RendPainterDrawComp* drawComp = ecs_view_read_t(drawItr, RendPainterDrawComp);
    if (!drawComp->instances.size) {
      continue;
    }
    if (!ecs_view_contains(graphicView, drawComp->graphic)) {
      continue;
    }
    ecs_view_jump(graphicItr, drawComp->graphic);
    rend_resource_mark_used(ecs_view_write_t(graphicItr, RendResComp));

    RendResGraphicComp* graphicResComp = ecs_view_write_t(graphicItr, RendResGraphicComp);
    if (rvk_pass_prepare(forwardPass, graphicResComp->graphic)) {
      *dynarray_push_t(&drawBuffer, RvkPassDraw) = (RvkPassDraw){
          .graphic       = graphicResComp->graphic,
          .instanceCount = (u32)drawComp->instances.size,
          .data          = dynarray_at(&drawComp->instances, 0, drawComp->instances.size),
          .dataStride    = drawComp->instances.stride,
      };
    }
  }

  // Sort draws.
  dynarray_sort(&drawBuffer, painter_compare_draw);

  // Execute draws.
  rvk_pass_begin(forwardPass, geo_color_white);
  rvk_pass_draw(
      forwardPass,
      mem_var(*globalData),
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
    EcsView*                  drawView,
    EcsView*                  graphicView) {

  const GapVector winSize  = gap_window_param(win, GapParam_WindowSize);
  const RendSize  rendSize = rend_size((u32)winSize.width, (u32)winSize.height);
  const bool      draw     = rvk_canvas_begin(painter->canvas, rendSize);
  if (draw) {
    const RendPainterGlobalData globalData = {
        .viewProj    = painter_view_proj_matrix(win, cam, trans),
        .camPosition = trans ? trans->position : geo_vector(0),
        .camRotation = trans ? trans->rotation : geo_quat_ident,
    };
    RvkPass* forwardPass = rvk_canvas_pass_forward(painter->canvas);
    painter_draw_forward(&globalData, forwardPass, drawView, graphicView);
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
    RvkCanvas* canvas = rvk_canvas_create(plat->device, windowComp);
    ecs_world_add_t(world, ecs_view_entity(itr), RendPainterComp, .canvas = canvas);
  }
}

ecs_system_define(RendPainterDrawBatchesSys) {
  EcsView* painterView = ecs_world_view_t(world, PainterUpdateView);
  EcsView* drawView    = ecs_world_view_t(world, DrawView);
  EcsView* graphicView = ecs_world_view_t(world, GraphicView);

  bool anyPainterDrawn = false;
  for (EcsIterator* itr = ecs_view_itr(painterView); ecs_view_walk(itr);) {
    const GapWindowComp*  win       = ecs_view_read_t(itr, GapWindowComp);
    const GapWindowEvents winEvents = gap_window_events(win);
    if (winEvents & GapWindowEvents_CloseRequested || winEvents & GapWindowEvents_Closed) {
      ecs_world_remove_t(world, ecs_view_entity(itr), RendPainterComp);
      continue;
    }
    RendPainterComp*          painter   = ecs_view_write_t(itr, RendPainterComp);
    const SceneCameraComp*    camera    = ecs_view_read_t(itr, SceneCameraComp);
    const SceneTransformComp* transform = ecs_view_read_t(itr, SceneTransformComp);
    anyPainterDrawn |= painter_draw(painter, win, camera, transform, drawView, graphicView);
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
  ecs_register_comp(
      RendPainterDrawComp, .destructor = ecs_destruct_draw, .combinator = ecs_combine_draw);

  ecs_register_view(GlobalView);
  ecs_register_view(DrawView);
  ecs_register_view(GraphicView);
  ecs_register_view(PainterCreateView);
  ecs_register_view(PainterUpdateView);

  ecs_register_system(
      RendPainterCreateSys, ecs_view_id(GlobalView), ecs_view_id(PainterCreateView));

  ecs_register_system(
      RendPainterDrawBatchesSys,
      ecs_view_id(PainterUpdateView),
      ecs_view_id(DrawView),
      ecs_view_id(GraphicView));

  ecs_order(RendPainterDrawBatchesSys, RendOrder_DrawExecute);
}
