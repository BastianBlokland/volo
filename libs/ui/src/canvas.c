#include "asset_ftx.h"
#include "core_diag.h"
#include "core_sort.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "gap_register.h"
#include "gap_window.h"
#include "rend_draw.h"
#include "scene_lifetime.h"
#include "ui_register.h"
#include "ui_settings.h"

#include "builder_internal.h"
#include "cmd_internal.h"
#include "resource_internal.h"

#define ui_canvas_clip_rects_max 10
#define ui_canvas_canvasses_max 100

typedef UiCanvasComp*       UiCanvasPtr;
typedef const UiCanvasComp* UiCanvasConstPtr;

typedef struct {
  UiId   id;
  UiRect rect;
} UiTrackedElem;

typedef enum {
  UiCanvasFlags_InputAny = 1 << 0,
} UiCanvasFlags;

typedef enum {
  UiRendererFlags_Disabled = 1 << 0,
} UiRendererFlags;

ecs_comp_define(UiRendererComp) {
  UiRendererFlags flags;
  EcsEntityId     draw;
  DynArray        overlayGlyphs; // UiGlyphData[]
};

ecs_comp_define(UiCanvasComp) {
  UiCanvasFlags flags;
  i32           order;
  EcsEntityId   window;
  UiCmdBuffer*  cmdBuffer;
  UiId          nextId;
  DynArray      trackedElems; // UiTrackedElem[]
  UiVector      resolution;   // Resolution of the canvas in ui-pixels.
  UiVector      inputDelta, inputPos;
  UiId          activeId;
  UiStatus      activeStatus;
  TimeSteady    activeStatusStart;
};

static void ecs_destruct_renderer(void* data) {
  UiRendererComp* comp = data;
  dynarray_destroy(&comp->overlayGlyphs);
}

static void ecs_destruct_canvas(void* data) {
  UiCanvasComp* comp = data;
  ui_cmdbuffer_destroy(comp->cmdBuffer);
  dynarray_destroy(&comp->trackedElems);
}

static i8 ui_canvas_ptr_compare(const void* a, const void* b) {
  const UiCanvasConstPtr* canvasPtrA = a;
  const UiCanvasConstPtr* canvasPtrB = b;
  return compare_i32(&(*(canvasPtrA))->order, &(*canvasPtrB)->order);
}

static i8 ui_tracked_elem_compare(const void* a, const void* b) {
  return compare_u64(field_ptr(a, UiTrackedElem, id), field_ptr(b, UiTrackedElem, id));
}

typedef struct {
  ALIGNAS(16)
  GeoVector canvasRes;      // x + y = canvas size in ui-pixels, z + w = inverse of x + y.
  f32       invCanvasScale; // Inverse of the canvas scale.
  f32       glyphsPerDim;
  f32       invGlyphsPerDim;
  f32       padding[1];
  UiRect    clipRects[10];
} UiDrawMetaData;

ASSERT(sizeof(UiDrawMetaData) == 192, "Size needs to match the size defined in glsl");

typedef struct {
  const UiSettingsComp* settings;
  const AssetFtxComp*   font;
  UiRendererComp*       renderer;
  RendDrawComp*         draw;
  UiCanvasComp*         canvas;
  UiRect                clipRects[ui_canvas_clip_rects_max];
  u32                   clipRectCount;
} UiRenderState;

static UiDrawMetaData ui_draw_metadata(const UiRenderState* state, const AssetFtxComp* font) {
  const UiVector canvasRes = state->canvas->resolution;
  UiDrawMetaData meta      = {
      .canvasRes = geo_vector(
          canvasRes.width, canvasRes.height, 1.0f / canvasRes.width, 1.0f / canvasRes.height),
      .invCanvasScale  = 1.0f / state->settings->scale,
      .glyphsPerDim    = font->glyphsPerDim,
      .invGlyphsPerDim = 1.0f / (f32)font->glyphsPerDim,
  };
  mem_cpy(mem_var(meta.clipRects), mem_var(state->clipRects));
  return meta;
}

static const UiTrackedElem* ui_canvas_tracked_get(const UiCanvasComp* canvas, const UiId id) {
  return dynarray_search_binary(
      (DynArray*)&canvas->trackedElems,
      ui_tracked_elem_compare,
      mem_struct(UiTrackedElem, .id = id).ptr);
}

static UiTrackedElem* ui_canvas_tracked_add(UiCanvasComp* canvas, const UiId id) {
  // TODO: This could be optimized to a single binary search if needed.
  UiTrackedElem* elem = (UiTrackedElem*)ui_canvas_tracked_get(canvas, id);
  if (!elem) {
    elem = dynarray_insert_sorted_t(
        (DynArray*)&canvas->trackedElems,
        UiTrackedElem,
        ui_tracked_elem_compare,
        mem_struct(UiTrackedElem, .id = id).ptr);
    elem->id = id;
  }
  return elem;
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
  case UiLayer_Invisible:
    break;
  case UiLayer_Overlay:
    *dynarray_push_t(&state->renderer->overlayGlyphs, UiGlyphData) = data;
    break;
  }
}

static void ui_canvas_output_rect(void* userCtx, const UiId id, const UiRect rect) {
  UiRenderState* state                           = userCtx;
  ui_canvas_tracked_add(state->canvas, id)->rect = rect;
}

static void ui_canvas_set_active(UiCanvasComp* canvas, const UiId id, const UiStatus status) {
  if (canvas->activeId == id && canvas->activeStatus == status) {
    return;
  }
  canvas->activeId          = id;
  canvas->activeStatus      = status;
  canvas->activeStatusStart = time_steady_clock();
}

static void ui_canvas_update_interaction(
    UiCanvasComp*        canvas,
    UiSettingsComp*      settings,
    const GapWindowComp* window,
    const UiId           hoveredId) {

  const bool inputDown     = gap_window_key_down(window, GapKey_MouseLeft);
  const bool inputReleased = gap_window_key_released(window, GapKey_MouseLeft);

  if (UNLIKELY(settings->flags & UiSettingFlags_DebugInspector)) {
    if (inputReleased) {
      settings->flags ^= UiSettingFlags_DebugInspector;
    }
    ui_canvas_set_active(canvas, hoveredId, UiStatus_Idle);
    return; // Normal input is disabled while using the debug inspector.
  }

  const bool hasActiveElem       = !sentinel_check(canvas->activeId);
  const bool activeElemIsHovered = canvas->activeId == hoveredId;

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
      canvas, hoveredId, sentinel_check(hoveredId) ? UiStatus_Idle : UiStatus_Hovered);
}

static UiBuildResult ui_canvas_build(UiRenderState* state, const UiId debugElem) {
  dynarray_clear(&state->canvas->trackedElems);

  const UiBuildCtx buildCtx = {
      .settings       = state->settings,
      .font           = state->font,
      .debugElem      = debugElem,
      .canvasRes      = state->canvas->resolution,
      .inputPos       = state->canvas->inputPos,
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
  ecs_access_maybe_write(UiSettingsComp);
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
    const GapWindowComp*  window      = ecs_view_read_t(windowItr, GapWindowComp);
    const UiSettingsComp* settings    = ecs_view_read_t(windowItr, UiSettingsComp);
    const GapVector       windowSize  = gap_window_param(window, GapParam_WindowSize);
    const GapVector       cursorDelta = gap_window_param(window, GapParam_CursorDelta);
    const GapVector       cursorPos   = gap_window_param(window, GapParam_CursorPos);

    if (gap_window_events(window) & GapWindowEvents_FocusLost) {
      ui_canvas_set_active(canvas, sentinel_u64, UiStatus_Idle);
    }

    if (gap_window_events(window) & GapWindowEvents_KeyPressed) {
      canvas->flags |= UiCanvasFlags_InputAny;
    } else {
      canvas->flags &= ~UiCanvasFlags_InputAny;
    }

    const f32 scale    = settings ? settings->scale : 1.0f;
    canvas->resolution = ui_vector(windowSize.x / scale, windowSize.y / scale);
    canvas->inputDelta = ui_vector(cursorDelta.x / scale, cursorDelta.y / scale);
    canvas->inputPos   = ui_vector(cursorPos.x / scale, cursorPos.y / scale);
  }
}

static void ui_renderer_create(EcsWorld* world, const EcsEntityId window) {

  const EcsEntityId drawEntity = ecs_world_entity_create(world);
  ecs_world_add_t(world, drawEntity, SceneLifetimeOwnerComp, .owner = window);

  RendDrawComp* draw = rend_draw_create(world, drawEntity, RendDrawFlags_NoInstanceFiltering);
  rend_draw_set_camera_filter(draw, window);

  ecs_world_add_t(
      world,
      window,
      UiRendererComp,
      .draw          = drawEntity,
      .overlayGlyphs = dynarray_create_t(g_alloc_heap, UiGlyphData, 32));

  UiSettingsComp* settings = ecs_world_add_t(world, window, UiSettingsComp);
  ui_settings_to_default(settings);
}

static UiId ui_canvas_debug_elem(UiCanvasComp* canvas, const UiSettingsComp* settings) {
  if (UNLIKELY(settings->flags & UiSettingFlags_DebugInspector)) {
    return canvas->activeId;
  }
  return sentinel_u64;
}

static u32 ui_canvass_query(
    EcsWorld* world, const EcsEntityId window, UiCanvasPtr out[ui_canvas_canvasses_max]) {
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
    UiSettingsComp*      settings = ecs_view_write_t(itr, UiSettingsComp);
    if (!renderer) {
      ui_renderer_create(world, entity);
      continue;
    }
    if (gap_window_key_released(window, GapKey_F12)) {
      renderer->flags ^= UiRendererFlags_Disabled;
    }
    if (UNLIKELY(renderer->flags & UiRendererFlags_Disabled)) {
      continue;
    }

    RendDrawComp* draw = ecs_utils_write_t(world, DrawView, renderer->draw, RendDrawComp);
    if (settings->flags & UiSettingFlags_DebugShading) {
      rend_draw_set_graphic(draw, ui_resource_graphic_debug(globalRes));
    } else {
      rend_draw_set_graphic(draw, ui_resource_graphic(globalRes));
    }

    const GapVector winSize     = gap_window_param(window, GapParam_WindowSize);
    const f32       scale       = settings ? settings->scale : 1.0f;
    UiRenderState   renderState = {
        .settings      = settings,
        .font          = font,
        .renderer      = renderer,
        .draw          = draw,
        .clipRects[0]  = {.size = {winSize.x / scale, winSize.y / scale}},
        .clipRectCount = 1,
    };

    UiCanvasPtr canvasses[ui_canvas_canvasses_max];
    const u32   canvasCount = ui_canvass_query(world, entity, canvasses);

    sort_quicksort_t(canvasses, canvasses + canvasCount, UiCanvasPtr, ui_canvas_ptr_compare);

    u32  hoveredCanvasIndex = sentinel_u32;
    UiId hoveredId;
    for (u32 i = 0; i != canvasCount; ++i) {
      canvasses[i]->order = i;
      renderState.canvas  = canvasses[i];

      const UiId          debugElem = ui_canvas_debug_elem(canvasses[i], settings);
      const UiBuildResult result    = ui_canvas_build(&renderState, debugElem);
      if (!sentinel_check(result.hoveredId)) {
        hoveredCanvasIndex = i;
        hoveredId          = result.hoveredId;
      }
    }
    if (gap_window_flags(window) & (GapWindowFlags_CursorHide | GapWindowFlags_CursorLock)) {
      // When the cursor is hidden or locked its be considered to not be 'hovering' over ui.
      hoveredCanvasIndex = sentinel_u32;
    }
    for (u32 i = 0; i != canvasCount; ++i) {
      const bool isHovered   = hoveredCanvasIndex == i;
      const UiId hoveredElem = isHovered ? hoveredId : sentinel_u64;
      ui_canvas_update_interaction(canvasses[i], settings, window, hoveredElem);
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
  UiCanvasComp*     canvas       = ecs_world_add_t(
      world,
      canvasEntity,
      UiCanvasComp,
      .window       = window,
      .cmdBuffer    = ui_cmdbuffer_create(g_alloc_heap),
      .trackedElems = dynarray_create_t(g_alloc_heap, UiTrackedElem, 16));

  ui_canvas_to_front(canvas);

  ecs_world_add_t(world, canvasEntity, SceneLifetimeOwnerComp, .owner = window);
  return canvasEntity;
}

void ui_canvas_reset(UiCanvasComp* comp) {
  ui_cmdbuffer_clear(comp->cmdBuffer);
  comp->nextId = 0;
}

void ui_canvas_to_front(UiCanvasComp* comp) { comp->order = i32_max; }
void ui_canvas_to_back(UiCanvasComp* comp) { comp->order = i32_min; }

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
  const UiTrackedElem* elem = ui_canvas_tracked_get(comp, id);
  return elem ? elem->rect : ui_rect(ui_vector(0, 0), ui_vector(0, 0));
}

UiStatus ui_canvas_status(const UiCanvasComp* comp) { return comp->activeStatus; }
UiVector ui_canvas_resolution(const UiCanvasComp* comp) { return comp->resolution; }
bool     ui_canvas_input_any(const UiCanvasComp* comp) {
  return (comp->flags & UiCanvasFlags_InputAny) != 0;
}
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
