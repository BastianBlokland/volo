#include "asset_ftx.h"
#include "core_diag.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "gap_register.h"
#include "gap_window.h"
#include "rend_draw.h"
#include "scene_lifetime.h"
#include "ui_register.h"

#include "builder_internal.h"
#include "cmd_internal.h"
#include "resource_internal.h"

#define ui_canvas_clip_rects_max 10
#define ui_canvas_canvasses_max 100

typedef struct {
  UiRect rect;
} UiElement;

ecs_comp_define(UiRendererComp) {
  EcsEntityId draw;
  DynArray    overlayGlyphs; // UiGlyphData[]
};

ecs_comp_define(UiCanvasComp) {
  EcsEntityId  window;
  UiCmdBuffer* cmdBuffer;
  UiId         nextId;
  DynArray     elements; // UiElement[]
  UiVector     windowSize;
  UiVector     inputDelta, inputPos;
  UiId         activeId;
  UiStatus     activeStatus;
  TimeSteady   activeStatusStart;
};

static void ecs_destruct_renderer(void* data) {
  UiRendererComp* comp = data;
  dynarray_destroy(&comp->overlayGlyphs);
}

static void ecs_destruct_canvas(void* data) {
  UiCanvasComp* comp = data;
  ui_cmdbuffer_destroy(comp->cmdBuffer);
  dynarray_destroy(&comp->elements);
}

typedef struct {
  ALIGNAS(16)
  f32    glyphsPerDim;
  f32    invGlyphsPerDim;
  f32    padding[2];
  UiRect clipRects[10];
} UiDrawMetaData;

ASSERT(sizeof(UiDrawMetaData) == 176, "Size needs to match the size defined in glsl");

typedef struct {
  const AssetFtxComp*  font;
  const GapWindowComp* window;
  UiRendererComp*      renderer;
  RendDrawComp*        draw;
  UiCanvasComp*        canvas;
  UiRect               clipRects[ui_canvas_clip_rects_max];
  u32                  clipRectCount;
} UiRenderState;

static UiDrawMetaData ui_draw_metadata(const UiRenderState* state, const AssetFtxComp* font) {
  UiDrawMetaData meta = {
      .glyphsPerDim    = font->glyphsPerDim,
      .invGlyphsPerDim = 1.0f / (f32)font->glyphsPerDim,
  };
  mem_cpy(mem_var(meta.clipRects), mem_var(state->clipRects));
  return meta;
}

static const UiElement* ui_canvas_elem(const UiCanvasComp* canvas, const UiId id) {
  if (id >= canvas->elements.size) {
    return null;
  }
  return dynarray_at_t(&canvas->elements, id, UiElement);
}

static u8 ui_canvas_output_clip_rect(void* userCtx, const UiRect rect) {
  UiRenderState* state = userCtx;
  diag_assert(state->clipRectCount < ui_canvas_clip_rects_max);
  const u8 id          = state->clipRectCount++;
  state->clipRects[id] = rect;
  return id;
}

static void ui_canvas_output_glyph(void* userCtx, const UiGlyphData data, const UiLayer layer) {
  UiRenderState* state = userCtx;
  switch (layer) {
  case UiLayer_Normal:
    rend_draw_add_instance(state->draw, mem_var(data), SceneTags_None, (GeoBox){0});
    break;
  case UiLayer_Overlay:
    *dynarray_push_t(&state->renderer->overlayGlyphs, UiGlyphData) = data;
    break;
  }
}

static void ui_canvas_output_rect(void* userCtx, const UiId id, const UiRect rect) {
  UiRenderState* state                                         = userCtx;
  dynarray_at_t(&state->canvas->elements, id, UiElement)->rect = rect;
}

static void ui_canvas_set_active(UiCanvasComp* canvas, const UiId id, const UiStatus status) {
  if (canvas->activeId == id && canvas->activeStatus == status) {
    return;
  }
  canvas->activeId          = id;
  canvas->activeStatus      = status;
  canvas->activeStatusStart = time_steady_clock();
}

static void ui_canvas_update_active(
    UiCanvasComp* canvas, const GapWindowComp* window, const UiBuildResult result) {

  const bool inputDown     = gap_window_key_down(window, GapKey_MouseLeft);
  const bool inputReleased = gap_window_key_released(window, GapKey_MouseLeft);

  const bool hasActiveElem       = !sentinel_check(canvas->activeId);
  const bool activeElemIsHovered = canvas->activeId == result.hoveredId;

  if (hasActiveElem && activeElemIsHovered && inputReleased) {
    ui_canvas_set_active(canvas, canvas->activeId, UiStatus_Activated);
    return;
  }
  if (hasActiveElem && activeElemIsHovered && inputDown) {
    ui_canvas_set_active(canvas, canvas->activeId, UiStatus_Pressed);
    return;
  }
  if (inputDown) {
    return; // Keep the same element active while holding down the input.
  }

  // Select a new active element.
  ui_canvas_set_active(
      canvas,
      result.hoveredId,
      sentinel_check(result.hoveredId) ? UiStatus_Idle : UiStatus_Hovered);
}

static UiBuildResult ui_canvas_build(UiRenderState* state) {

  // Ensure we have UiElement structures for all elements that are requested to be drawn.
  dynarray_resize(&state->canvas->elements, state->canvas->nextId);
  mem_set(dynarray_at(&state->canvas->elements, 0, state->canvas->nextId), 0);

  const UiBuildCtx buildCtx = {
      .window         = state->window,
      .font           = state->font,
      .userCtx        = state,
      .outputClipRect = &ui_canvas_output_clip_rect,
      .outputGlyph    = &ui_canvas_output_glyph,
      .outputRect     = &ui_canvas_output_rect,
  };
  return ui_build(state->canvas->cmdBuffer, &buildCtx);
}

ecs_view_define(GlobalResourcesView) { ecs_access_read(UiGlobalResourcesComp); }
ecs_view_define(FtxView) { ecs_access_read(AssetFtxComp); }
ecs_view_define(WindowView) {
  ecs_access_read(GapWindowComp);
  ecs_access_maybe_write(UiRendererComp);
}
ecs_view_define(CanvasView) { ecs_access_write(UiCanvasComp); }
ecs_view_define(DrawView) { ecs_access_write(RendDrawComp); }

static const UiGlobalResourcesComp* ui_global_resources(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourcesView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_read_t(globalItr, UiGlobalResourcesComp) : null;
}

static const AssetFtxComp* ui_global_font(EcsWorld* world, const EcsEntityId entity) {
  EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(world, FtxView), entity);
  return itr ? ecs_view_read_t(itr, AssetFtxComp) : null;
}

ecs_system_define(UiCanvasInputSys) {
  EcsView*     windowView = ecs_world_view_t(world, WindowView);
  EcsIterator* windowItr  = ecs_view_itr(windowView);

  for (EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, CanvasView)); ecs_view_walk(itr);) {
    UiCanvasComp* canvas = ecs_view_write_t(itr, UiCanvasComp);
    if (!ecs_view_maybe_jump(windowItr, canvas->window)) {
      continue;
    }
    const GapWindowComp* window      = ecs_view_read_t(windowItr, GapWindowComp);
    const GapVector      windowSize  = gap_window_param(window, GapParam_WindowSize);
    const GapVector      cursorDelta = gap_window_param(window, GapParam_CursorDelta);
    const GapVector      cursorPos   = gap_window_param(window, GapParam_CursorPos);

    if (gap_window_events(window) & GapWindowEvents_FocusLost) {
      ui_canvas_set_active(canvas, sentinel_u64, UiStatus_Idle);
    }

    canvas->windowSize = ui_vector(windowSize.x, windowSize.y);
    canvas->inputDelta = ui_vector(cursorDelta.x, cursorDelta.y);
    canvas->inputPos   = ui_vector(cursorPos.x, cursorPos.y);
  }
}

static void ui_renderer_create(
    EcsWorld* world, const UiGlobalResourcesComp* globalRes, const EcsEntityId window) {

  const EcsEntityId drawEntity = ecs_world_entity_create(world);
  ecs_world_add_t(world, drawEntity, SceneLifetimeOwnerComp, .owner = window);

  RendDrawComp* draw = rend_draw_create(world, drawEntity, RendDrawFlags_NoInstanceFiltering);
  rend_draw_set_camera_filter(draw, window);
  rend_draw_set_graphic(draw, ui_resource_graphic(globalRes));

  ecs_world_add_t(
      world,
      window,
      UiRendererComp,
      .draw          = drawEntity,
      .overlayGlyphs = dynarray_create_t(g_alloc_heap, UiGlyphData, 32));
}

static u32 ui_canvass_query(
    EcsWorld* world, const EcsEntityId window, UiCanvasComp* out[ui_canvas_canvasses_max]) {
  u32 count = 0;
  for (EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, CanvasView)); ecs_view_walk(itr);) {
    UiCanvasComp* canvas = ecs_view_write_t(itr, UiCanvasComp);
    if (canvas->window == window) {
      diag_assert(count < ui_canvas_canvasses_max);
      out[count++] = canvas;
    }
  }
  return count;
}

ecs_system_define(UiRenderSys) {
  const UiGlobalResourcesComp* globalRes = ui_global_resources(world);
  if (!globalRes) {
    return; // Global resources not initialized yet.
  }
  const AssetFtxComp* font = ui_global_font(world, ui_resource_font(globalRes));
  if (!font) {
    return; // Global font not loaded yet.
  }

  for (EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, WindowView)); ecs_view_walk(itr);) {
    const EcsEntityId    entity   = ecs_view_entity(itr);
    const GapWindowComp* window   = ecs_view_read_t(itr, GapWindowComp);
    UiRendererComp*      renderer = ecs_view_write_t(itr, UiRendererComp);
    if (!renderer) {
      ui_renderer_create(world, globalRes, entity);
      continue;
    }
    RendDrawComp* draw = ecs_utils_write_t(world, DrawView, renderer->draw, RendDrawComp);

    const GapVector winSize     = gap_window_param(window, GapParam_WindowSize);
    UiRenderState   renderState = {
        .font          = font,
        .window        = window,
        .renderer      = renderer,
        .draw          = draw,
        .clipRects[0]  = {.size = {winSize.width, winSize.height}},
        .clipRectCount = 1,
    };

    // Build all canvasses.
    UiCanvasComp* canvasses[ui_canvas_canvasses_max];
    const u32     canvasCount = ui_canvass_query(world, entity, canvasses);
    for (u32 i = 0; i != canvasCount; ++i) {
      renderState.canvas         = canvasses[i];
      const UiBuildResult result = ui_canvas_build(&renderState);
      ui_canvas_update_active(canvasses[i], window, result);
    }

    // Add the overlay glyphs, at this stage all the normal glyphs have already been added.
    dynarray_for_t(&renderer->overlayGlyphs, UiGlyphData, glyph) {
      rend_draw_add_instance(draw, mem_var(*glyph), SceneTags_None, (GeoBox){0});
    }
    dynarray_clear(&renderer->overlayGlyphs);

    // Set the metadata.
    const UiDrawMetaData meta = ui_draw_metadata(&renderState, font);
    rend_draw_set_data(draw, mem_var(meta));
  }
}

ecs_module_init(ui_canvas_module) {
  ecs_register_comp(UiCanvasComp, .destructor = ecs_destruct_canvas);
  ecs_register_comp(UiRendererComp, .destructor = ecs_destruct_renderer);

  ecs_register_view(CanvasView);
  ecs_register_view(DrawView);
  ecs_register_view(GlobalResourcesView);
  ecs_register_view(FtxView);
  ecs_register_view(WindowView);

  ecs_register_system(UiCanvasInputSys, ecs_view_id(CanvasView), ecs_view_id(WindowView));

  ecs_register_system(
      UiRenderSys,
      ecs_view_id(GlobalResourcesView),
      ecs_view_id(FtxView),
      ecs_view_id(WindowView),
      ecs_view_id(CanvasView),
      ecs_view_id(DrawView));

  ecs_order(UiCanvasInputSys, GapOrder_WindowUpdate + 1);
  ecs_order(UiRenderSys, UiOrder_Render);
}

EcsEntityId ui_canvas_create(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId canvasEntity = ecs_world_entity_create(world);
  ecs_world_add_t(
      world,
      canvasEntity,
      UiCanvasComp,
      .window    = window,
      .cmdBuffer = ui_cmdbuffer_create(g_alloc_heap),
      .elements  = dynarray_create_t(g_alloc_heap, UiElement, 128));

  ecs_world_add_t(world, canvasEntity, SceneLifetimeOwnerComp, .owner = window);
  return canvasEntity;
}

void ui_canvas_reset(UiCanvasComp* comp) {
  ui_cmdbuffer_clear(comp->cmdBuffer);
  comp->nextId = 0;
}

UiId ui_canvas_id_peek(const UiCanvasComp* comp) { return comp->nextId; }
void ui_canvas_id_skip(UiCanvasComp* comp) { ++comp->nextId; }

UiStatus ui_canvas_elem_status(const UiCanvasComp* comp, const UiId id) {
  return id == comp->activeId ? comp->activeStatus : UiStatus_Idle;
}

TimeDuration ui_canvas_elem_status_duration(const UiCanvasComp* comp, const UiId id) {
  return id == comp->activeId ? time_steady_duration(comp->activeStatusStart, time_steady_clock())
                              : 0;
}

UiRect ui_canvas_elem_rect(const UiCanvasComp* comp, const UiId id) {
  const UiElement* elem = ui_canvas_elem(comp, id);
  return elem ? elem->rect : ui_rect(ui_vector(0, 0), ui_vector(0, 0));
}

UiVector ui_canvas_window_size(const UiCanvasComp* comp) { return comp->windowSize; }

UiVector ui_canvas_input_delta(const UiCanvasComp* comp) { return comp->inputDelta; }
UiVector ui_canvas_input_pos(const UiCanvasComp* comp) { return comp->inputPos; }

UiId ui_canvas_draw_text(
    UiCanvasComp* comp,
    const String  text,
    const u16     fontSize,
    const UiAlign align,
    const UiFlags flags) {

  const UiId id = comp->nextId++;
  if (!string_is_empty(text)) {
    ui_cmd_push_draw_text(comp->cmdBuffer, id, text, fontSize, align, flags);
  }
  return id;
}

UiId ui_canvas_draw_glyph(
    UiCanvasComp* comp, const Unicode cp, const u16 maxCorner, const UiFlags flags) {
  const UiId id = comp->nextId++;
  ui_cmd_push_draw_glyph(comp->cmdBuffer, id, cp, maxCorner, flags);
  return id;
}

UiCmdBuffer* ui_canvas_cmd_buffer(UiCanvasComp* canvas) { return canvas->cmdBuffer; }
