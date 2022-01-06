#include "core_alloc.h"
#include "core_thread.h"
#include "core_time.h"
#include "ecs_utils.h"
#include "gap_input.h"
#include "gap_window.h"
#include "geo_matrix.h"
#include "rend_instance.h"
#include "scene_camera.h"
#include "scene_transform.h"

#include "platform_internal.h"
#include "resource_internal.h"
#include "rvk/canvas_internal.h"
#include "rvk/graphic_internal.h"
#include "rvk/pass_internal.h"

ecs_comp_define(RendPainterComp) { RvkCanvas* canvas; };

static void ecs_destruct_painter_comp(void* data) {
  RendPainterComp* comp = data;
  if (comp->canvas) {
    rvk_canvas_destroy(comp->canvas);
  }
}

typedef struct {
  ALIGNAS(16)
  GeoMatrix viewProj;
  GeoVector camPosition;
  GeoQuat   camRotation;
} RendPainterGlobalData;

typedef struct {
  ALIGNAS(16)
  GeoVector position;
  GeoQuat   rotation;
} RendPainterInstanceData;

ecs_comp_define(RendPainterBatchComp) {
  DynArray instances; // RendPainterInstanceData[]
};

static void ecs_destruct_batch_comp(void* data) {
  RendPainterBatchComp* comp = data;
  dynarray_destroy(&comp->instances);
}

static void ecs_combine_batch(void* dataA, void* dataB) {
  RendPainterBatchComp* compA = dataA;
  RendPainterBatchComp* compB = dataB;
  dynarray_for_t(&compB->instances, RendPainterInstanceData, instance) {
    mem_cpy(dynarray_push(&compA->instances, 1), mem_var(*instance));
  }
  dynarray_destroy(&compB->instances);
}

ecs_view_define(GlobalView) { ecs_access_write(RendPlatformComp); }

ecs_view_define(RenderableView) {
  ecs_access_read(RendInstanceComp);
  ecs_access_maybe_read(SceneTransformComp);
}

ecs_view_define(DrawBatchView) {
  ecs_access_write(RendResGraphicComp);
  ecs_access_read(RendPainterBatchComp);
}

ecs_view_define(CreateBatchView) {
  ecs_access_with(RendResGraphicComp);
  ecs_access_without(RendResUnloadComp);
  ecs_access_maybe_write(RendPainterBatchComp);
}

ecs_view_define(ClearBatchView) { ecs_access_write(RendPainterBatchComp); }

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

static RendPainterBatchComp* painter_batch_get(EcsWorld* world, EcsIterator* graphic) {
  RendPainterBatchComp* comp = ecs_view_write_t(graphic, RendPainterBatchComp);
  if (LIKELY(comp)) {
    return comp;
  }
  return ecs_world_add_t(
      world,
      ecs_view_entity(graphic),
      RendPainterBatchComp,
      .instances = dynarray_create_t(g_alloc_heap, RendPainterInstanceData, 32));
}

static void painter_draw_forward(
    const RendPainterGlobalData* globalData, RvkPass* forwardPass, EcsView* batchView) {
  DynArray drawBuffer = dynarray_create_t(g_alloc_scratch, RvkPassDraw, 1024);

  // Prepare draws.
  for (EcsIterator* itr = ecs_view_itr(batchView); ecs_view_walk(itr);) {
    RendResGraphicComp*         graphicResComp = ecs_view_write_t(itr, RendResGraphicComp);
    const RendPainterBatchComp* batchComp      = ecs_view_read_t(itr, RendPainterBatchComp);
    if (!batchComp->instances.size) {
      continue;
    }
    if (rvk_pass_prepare(forwardPass, graphicResComp->graphic)) {
      *dynarray_push_t(&drawBuffer, RvkPassDraw) = (RvkPassDraw){
          .graphic       = graphicResComp->graphic,
          .instanceCount = (u32)batchComp->instances.size,
          .data          = dynarray_at(&batchComp->instances, 0, batchComp->instances.size),
          .dataStride    = sizeof(RendPainterInstanceData),
      };
    }
  }

  // Sort draws.
  dynarray_sort(&drawBuffer, painter_compare_draw);

  // Execute draws.
  rvk_pass_begin(forwardPass, rend_soothing_purple);
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
    EcsView*                  batchView) {

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
    painter_draw_forward(&globalData, forwardPass, batchView);
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
    RvkCanvas*           canvas     = rvk_canvas_create(plat->device, windowComp);
    ecs_world_add_t(world, ecs_view_entity(itr), RendPainterComp, .canvas = canvas);
  }
}

ecs_system_define(RendPainterUpdateBatchesSys) {
  // Clear the current batches.
  EcsView* clearBatchView = ecs_world_view_t(world, ClearBatchView);
  for (EcsIterator* clearItr = ecs_view_itr(clearBatchView); ecs_view_walk(clearItr);) {
    dynarray_clear(&ecs_view_write_t(clearItr, RendPainterBatchComp)->instances);
  }

  // Create new batches.
  EcsView*     createBatchView = ecs_world_view_t(world, CreateBatchView);
  EcsIterator* batchItr        = ecs_view_itr(createBatchView);
  EcsView*     renderableView  = ecs_world_view_t(world, RenderableView);

  for (EcsIterator* renderableItr = ecs_view_itr(renderableView); ecs_view_walk(renderableItr);) {
    const RendInstanceComp*   instanceComp  = ecs_view_read_t(renderableItr, RendInstanceComp);
    const SceneTransformComp* transformComp = ecs_view_read_t(renderableItr, SceneTransformComp);

    if (!ecs_view_contains(createBatchView, instanceComp->graphic)) {
      continue;
    }
    ecs_view_jump(batchItr, instanceComp->graphic);
    RendPainterBatchComp* batchComp = painter_batch_get(world, batchItr);

    *dynarray_push_t(&batchComp->instances, RendPainterInstanceData) = (RendPainterInstanceData){
        .position = transformComp ? transformComp->position : geo_vector(0),
        .rotation = transformComp ? transformComp->rotation : geo_quat_ident,
    };
  }
}

ecs_system_define(RendPainterDrawBatchesSys) {
  EcsView* painterView = ecs_world_view_t(world, PainterUpdateView);
  EcsView* batchView   = ecs_world_view_t(world, DrawBatchView);

  bool anyPainterDrawn = false;
  for (EcsIterator* itr = ecs_view_itr(painterView); ecs_view_walk(itr);) {
    const GapWindowComp*      win       = ecs_view_read_t(itr, GapWindowComp);
    RendPainterComp*          painter   = ecs_view_write_t(itr, RendPainterComp);
    const SceneCameraComp*    camera    = ecs_view_read_t(itr, SceneCameraComp);
    const SceneTransformComp* transform = ecs_view_read_t(itr, SceneTransformComp);
    anyPainterDrawn |= painter_draw(painter, win, camera, transform, batchView);
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
  ecs_register_comp(RendPainterComp, .destructor = ecs_destruct_painter_comp);
  ecs_register_comp(
      RendPainterBatchComp, .destructor = ecs_destruct_batch_comp, .combinator = ecs_combine_batch);

  ecs_register_view(GlobalView);
  ecs_register_view(RenderableView);
  ecs_register_view(DrawBatchView);
  ecs_register_view(CreateBatchView);
  ecs_register_view(ClearBatchView);
  ecs_register_view(PainterCreateView);
  ecs_register_view(PainterUpdateView);

  ecs_register_system(
      RendPainterCreateSys, ecs_view_id(GlobalView), ecs_view_id(PainterCreateView));

  ecs_register_system(
      RendPainterUpdateBatchesSys,
      ecs_view_id(ClearBatchView),
      ecs_view_id(CreateBatchView),
      ecs_view_id(RenderableView));

  ecs_register_system(
      RendPainterDrawBatchesSys, ecs_view_id(PainterUpdateView), ecs_view_id(DrawBatchView));
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
