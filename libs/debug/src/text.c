#include "core_alloc.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "debug_register.h"
#include "debug_text.h"
#include "ecs_utils.h"
#include "log_logger.h"
#include "scene_camera.h"
#include "scene_transform.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_style.h"

#define debug_text_transient_chunk_size (64 * usize_kibibyte)
#define debug_text_transient_max 512

typedef struct {
  GeoVector pos;
  GeoColor  color;
  String    text;
  u16       fontSize;
} DebugText3D;

ecs_comp_define(DebugTextComp) {
  DynArray   entries; // DebugText3D[]
  Allocator* allocTransient;
};

ecs_comp_define(DebugTextRendererComp) { EcsEntityId canvas; };

static void ecs_destruct_text(void* data) {
  DebugTextComp* comp = data;
  dynarray_destroy(&comp->entries);
  alloc_chunked_destroy(comp->allocTransient);
}

ecs_view_define(RendererCreateView) {
  ecs_access_with(SceneCameraComp);
  ecs_access_without(DebugTextRendererComp);
}

ecs_view_define(TextView) { ecs_access_write(DebugTextComp); }

ecs_view_define(RendererView) {
  ecs_access_read(DebugTextRendererComp);
  ecs_access_read(SceneCameraComp);
  ecs_access_maybe_read(SceneTransformComp);
}

ecs_view_define(CanvasView) { ecs_access_write(UiCanvasComp); }

static GeoMatrix debug_text_view_proj(
    const SceneCameraComp* cam, const SceneTransformComp* trans, const UiCanvasComp* canvas) {
  const UiVector res    = ui_canvas_resolution(canvas);
  const f32      aspect = (f32)res.width / (f32)res.height;
  return scene_camera_view_proj(cam, trans, aspect);
}

static GeoVector debug_text_canvas_pos(const GeoMatrix* viewProj, const GeoVector pos) {
  const GeoVector ndcPos = geo_matrix_transform(viewProj, geo_vector(pos.x, pos.y, pos.z, 1));
  if (UNLIKELY(ndcPos.w == 0)) {
    return geo_vector(-1, -1, -1, -1); // Not a valid position on screen.
  }
  const GeoVector persDivPos = geo_vector_perspective_div(ndcPos);
  const GeoVector normPos    = geo_vector_mul(geo_vector_add(persDivPos, geo_vector(1, 1)), 0.5f);
  return geo_vector(normPos.x, 1.0f - normPos.y, persDivPos.z);
}

static UiColor debug_text_to_ui_color(const GeoColor c) {
  return ui_color(
      (u8)(math_min(c.r, 1.0f) * 255),
      (u8)(math_min(c.g, 1.0f) * 255),
      (u8)(math_min(c.b, 1.0f) * 255),
      (u8)(math_min(c.a, 1.0f) * 255));
}

ecs_system_define(DebugTextInitSys) {
  // Create a global text component for convenience.
  if (!ecs_world_has_t(world, ecs_world_global(world), DebugTextComp)) {
    debug_text_create(world, ecs_world_global(world));
  }

  // Create a renderer for each camera.
  EcsView* createRendererView = ecs_world_view_t(world, RendererCreateView);
  for (EcsIterator* itr = ecs_view_itr(createRendererView); ecs_view_walk(itr);) {
    const EcsEntityId cameraEntity = ecs_view_entity(itr);
    ecs_world_add_t(
        world,
        cameraEntity,
        DebugTextRendererComp,
        .canvas = ui_canvas_create(world, cameraEntity, UiCanvasCreateFlags_None));
  }
}

ecs_system_define(DebugTextRenderSys) {
  EcsIterator* textItr     = ecs_view_itr(ecs_world_view_t(world, TextView));
  EcsIterator* rendererItr = ecs_view_itr(ecs_world_view_t(world, RendererView));

  // Draw all requests for all renderers.
  for (ecs_view_itr_reset(rendererItr); ecs_view_walk(rendererItr);) {
    const DebugTextRendererComp* renderer  = ecs_view_read_t(rendererItr, DebugTextRendererComp);
    const SceneCameraComp*       camera    = ecs_view_read_t(rendererItr, SceneCameraComp);
    const SceneTransformComp*    transform = ecs_view_read_t(rendererItr, SceneTransformComp);

    UiCanvasComp*   canvas   = ecs_utils_write_t(world, CanvasView, renderer->canvas, UiCanvasComp);
    const GeoMatrix viewProj = debug_text_view_proj(camera, transform, canvas);

    ui_canvas_reset(canvas);
    ui_canvas_to_back(canvas);

    for (ecs_view_itr_reset(textItr); ecs_view_walk(textItr);) {
      DebugTextComp* textComp = ecs_view_write_t(textItr, DebugTextComp);
      dynarray_for_t(&textComp->entries, DebugText3D, entry) {
        const GeoVector canvasPos = debug_text_canvas_pos(&viewProj, entry->pos);
        if (canvasPos.z <= 0) {
          continue; // Text is behind the camera.
        }
        const UiVector canvasSize = ui_vector(0.2f, 0.1f);
        const UiRect   canvasRect = {
            ui_vector(canvasPos.x - canvasSize.x * 0.5f, canvasPos.y - canvasSize.y * 0.5f),
            canvasSize,
        };
        ui_style_color(canvas, debug_text_to_ui_color(entry->color));
        ui_layout_set(canvas, canvasRect, UiBase_Canvas);
        ui_canvas_draw_text(
            canvas, entry->text, entry->fontSize, UiAlign_MiddleCenter, UiFlags_None);
      }
    }
  }

  // Clear the draw requests.
  for (ecs_view_itr_reset(textItr); ecs_view_walk(textItr);) {
    DebugTextComp* textComp = ecs_view_write_t(textItr, DebugTextComp);
    dynarray_clear(&textComp->entries);
    alloc_reset(textComp->allocTransient);
  }
}

ecs_module_init(debug_text_module) {
  ecs_register_comp(DebugTextComp, .destructor = ecs_destruct_text);
  ecs_register_comp(DebugTextRendererComp);

  ecs_register_view(RendererCreateView);
  ecs_register_view(TextView);
  ecs_register_view(RendererView);
  ecs_register_view(CanvasView);

  ecs_register_system(DebugTextInitSys, ecs_view_id(RendererCreateView));

  ecs_register_system(
      DebugTextRenderSys,
      ecs_view_id(TextView),
      ecs_view_id(RendererView),
      ecs_view_id(CanvasView));

  ecs_order(DebugTextRenderSys, DebugOrder_TextRender);
}

DebugTextComp* debug_text_create(EcsWorld* world, const EcsEntityId entity) {
  return ecs_world_add_t(
      world,
      entity,
      DebugTextComp,
      .entries = dynarray_create_t(g_alloc_heap, DebugText3D, 64),
      .allocTransient =
          alloc_chunked_create(g_alloc_page, alloc_bump_create, debug_text_transient_chunk_size));
}

void debug_text_with_opts(
    DebugTextComp* comp, const GeoVector pos, const String text, const DebugTextOpts* opts) {
  if (UNLIKELY(text.size > debug_text_transient_max)) {
    log_e(
        "Debug text size exceeds maximum",
        log_param("size", fmt_size(text.size)),
        log_param("limit", fmt_size(debug_text_transient_max)));
    return;
  }
  if (UNLIKELY(!text.size)) {
    return;
  }
  // TODO: Report error when the transient allocator runs out of space.
  *((DebugText3D*)dynarray_push(&comp->entries, 1).ptr) = (DebugText3D){
      .pos      = pos,
      .text     = string_dup(comp->allocTransient, text),
      .fontSize = opts->fontSize,
      .color    = opts->color,
  };
}
