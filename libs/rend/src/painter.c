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
  GeoMatrix viewProj;
  GeoVector camPosition;
  GeoQuat   camRotation;
  f32       aspectRatio;
} RendPainterGlobalData;

ASSERT(sizeof(RendPainterGlobalData) == 112, "Size needs to match the size defined in glsl");

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

static GeoMatrix painter_view_proj_matrix(
    const GapWindowComp* win, const SceneCameraComp* cam, const SceneTransformComp* trans) {

  const GapVector winSize = gap_window_param(win, GapParam_WindowSize);
  const f32       aspect  = (f32)winSize.width / (f32)winSize.height;

  if (LIKELY(cam)) {
    return scene_camera_view_proj(cam, trans, aspect);
  }
  // Fall back to a basic orthographic projection.
  return geo_matrix_proj_ortho(2.0f, 2.0f / aspect, -100.0f, 100.0f);
}

static void painter_draw_override_dynmesh(
    RendPainterComp*      painter,
    RvkPass*              pass,
    const RvkRepositoryId graphicId,
    RendDrawComp*         orgDraw,
    RvkMesh*              dynMesh) {
  RvkRepository* repository = rvk_canvas_repository(painter->canvas);
  RvkGraphic*    gra        = rvk_repository_graphic_get(repository, graphicId);

  if (gra && rvk_pass_prepare(pass, gra) && rvk_pass_prepare_mesh(pass, dynMesh)) {
    *dynarray_push_t(&painter->drawBuffer, RvkPassDraw) = rend_draw_output(orgDraw, gra, dynMesh);
  }
}

static void painter_draw_forward(
    RendPainterComp*             painter,
    const RendSettingsComp*      settings,
    const RendPainterGlobalData* globalData,
    const RendView*              view,
    RvkPass*                     forwardPass,
    EcsView*                     drawView,
    EcsView*                     graphicView) {

  dynarray_clear(&painter->drawBuffer);

  // Prepare draws.
  EcsIterator* graphicItr = ecs_view_itr(graphicView);
  for (EcsIterator* drawItr = ecs_view_itr(drawView); ecs_view_walk(drawItr);) {
    RendDrawComp* draw = ecs_view_write_t(drawItr, RendDrawComp);
    if (!rend_draw_gather(draw, view, settings)) {
      continue;
    }
    if (!ecs_view_contains(graphicView, rend_draw_graphic(draw))) {
      continue;
    }
    ecs_view_jump(graphicItr, rend_draw_graphic(draw));
    RvkGraphic* graphic = ecs_view_write_t(graphicItr, RendResGraphicComp)->graphic;

    if (rvk_pass_prepare(forwardPass, graphic)) {
      *dynarray_push_t(&painter->drawBuffer, RvkPassDraw) = rend_draw_output(draw, graphic, null);
    }

    if (graphic->mesh) {
      const bool isStandardGeometry = (rend_draw_flags(draw) & RendDrawFlags_StandardGeometry) != 0;
      const bool isSkinned          = (rend_draw_flags(draw) & RendDrawFlags_Skinned) != 0;
      const bool isTerrain          = (rend_draw_flags(draw) & RendDrawFlags_Terrain) != 0;

      diag_assert_msg(
          isSkinned == ((graphic->mesh->flags & RvkMeshFlags_Skinned) != 0),
          "Skinned meshes can only be drawn using skinned draws");

      if (settings->flags & RendFlags_DebugSkinning && isStandardGeometry && isSkinned) {
        const RvkRepositoryId graphicId = RvkRepositoryId_DebugSkinningGraphic;
        painter_draw_override_dynmesh(painter, forwardPass, graphicId, draw, graphic->mesh);
      }
      if (settings->flags & RendFlags_Wireframe && (isStandardGeometry || isTerrain)) {
        RvkRepositoryId graphicId;
        if (isStandardGeometry && isSkinned) {
          graphicId = RvkRepositoryId_WireframeSkinnedGraphic;
        } else if (isTerrain) {
          graphicId = RvkRepositoryId_WireframeTerrainGraphic;
        } else {
          diag_assert(isStandardGeometry && !isSkinned);
          graphicId = RvkRepositoryId_WireframeGraphic;
        }
        painter_draw_override_dynmesh(painter, forwardPass, graphicId, draw, graphic->mesh);
      }
    }
  }

  // Sort draws.
  dynarray_sort(&painter->drawBuffer, painter_compare_pass_draw);

  // Execute draws.
  rvk_pass_begin(forwardPass, geo_color_black);
  rvk_pass_draw(
      forwardPass,
      mem_var(*globalData),
      (RvkPassDrawList){
          .values = dynarray_begin_t(&painter->drawBuffer, RvkPassDraw),
          .count  = painter->drawBuffer.size,
      });
  rvk_pass_end(forwardPass);
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
  const RvkSize   resolution = rvk_size((u32)winSize.width, (u32)winSize.height);
  const bool      draw       = rvk_canvas_begin(painter->canvas, settings, resolution);
  if (draw) {
    const GeoVector      origin   = trans ? trans->position : geo_vector(0);
    const GeoMatrix      viewProj = painter_view_proj_matrix(win, cam, trans);
    const SceneTagFilter filter   = cam ? cam->filter : (SceneTagFilter){0};
    const RendView       view     = rend_view_create(camEntity, origin, &viewProj, filter);

    const RendPainterGlobalData globalData = {
        .viewProj    = viewProj,
        .camPosition = trans ? trans->position : geo_vector(0),
        .camRotation = trans ? trans->rotation : geo_quat_ident,
        .aspectRatio = (f32)resolution.width / (f32)resolution.height,
    };

    RvkPass* forwardPass = rvk_canvas_pass_forward(painter->canvas);
    painter_draw_forward(painter, settings, &globalData, &view, forwardPass, drawView, graphicView);
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
      ecs_view_id(PainterUpdateView),
      ecs_view_id(DrawView),
      ecs_view_id(GraphicView));

  ecs_order(RendPainterDrawBatchesSys, RendOrder_DrawExecute);
}

void rend_painter_teardown(EcsWorld* world, const EcsEntityId entity) {
  ecs_world_remove_t(world, entity, RendPainterComp);
}
