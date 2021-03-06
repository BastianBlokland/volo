#include "asset_ftx.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_sort.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "gap_register.h"
#include "gap_window.h"
#include "input.h"
#include "rend_draw.h"
#include "scene_lifetime.h"
#include "ui_register.h"
#include "ui_settings.h"
#include "ui_stats.h"

#include "builder_internal.h"
#include "cmd_internal.h"
#include "editor_internal.h"
#include "resource_internal.h"

#define ui_canvas_clip_rects_max 50
#define ui_canvas_canvasses_max 100

/**
 * Scaling is applied to match the dpi of a 27 inch 4k monitor.
 */
#define ui_canvas_dpi_reference 163
#define ui_canvas_dpi_min_scale 0.75f

typedef UiCanvasComp*       UiCanvasPtr;
typedef const UiCanvasComp* UiCanvasConstPtr;

/**
 * Element information that is tracked during ui build / render and can be queried next frame.
 * NOTE: Cleared at the start of every ui-build.
 */
typedef struct {
  UiId            id;
  UiRect          rect;
  UiBuildTextInfo textInfo;
} UiTrackedElem;

/**
 * Persistent element data.
 */
typedef struct {
  UiId              id;
  UiPersistentFlags flags;
} UiPersistentElem;

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
  UiCanvasFlags  flags;
  i32            order;
  EcsEntityId    window;
  UiCmdBuffer*   cmdBuffer;
  UiEditor*      textEditor;
  UiId           nextId;
  DynArray       trackedElems;    // UiTrackedElem[]
  DynArray       persistentElems; // UiPersistentElem[]
  f32            scale;
  UiVector       resolution; // Resolution of the canvas in ui-pixels.
  UiLayer        minInteractLayer;
  UiVector       inputDelta, inputPos, inputScroll;
  UiId           activeId;
  UiStatus       activeStatus;
  TimeSteady     activeStatusStart;
  UiFlags        activeElemFlags;
  UiInteractType interactType;
};

static void ecs_destruct_renderer(void* data) {
  UiRendererComp* comp = data;
  dynarray_destroy(&comp->overlayGlyphs);
}

static void ecs_destruct_canvas(void* data) {
  UiCanvasComp* comp = data;
  ui_cmdbuffer_destroy(comp->cmdBuffer);
  ui_editor_destroy(comp->textEditor);
  dynarray_destroy(&comp->trackedElems);
  dynarray_destroy(&comp->persistentElems);
}

static i8 ui_canvas_ptr_compare(const void* a, const void* b) {
  const UiCanvasConstPtr* canvasPtrA = a;
  const UiCanvasConstPtr* canvasPtrB = b;
  return compare_i32(&(*(canvasPtrA))->order, &(*canvasPtrB)->order);
}

static i8 ui_tracked_elem_compare(const void* a, const void* b) {
  return compare_u64(field_ptr(a, UiTrackedElem, id), field_ptr(b, UiTrackedElem, id));
}

static i8 ui_persistent_elem_compare(const void* a, const void* b) {
  return compare_u64(field_ptr(a, UiPersistentElem, id), field_ptr(b, UiPersistentElem, id));
}

typedef struct {
  ALIGNAS(16)
  GeoVector canvasRes;      // x + y = canvas size in ui-pixels, z + w = inverse of x + y.
  f32       invCanvasScale; // Inverse of the canvas scale.
  f32       glyphsPerDim;
  f32       invGlyphsPerDim;
  f32       padding[1];
  UiRect    clipRects[ui_canvas_clip_rects_max];
} UiDrawMetaData;

ASSERT(sizeof(UiDrawMetaData) == 832, "Size needs to match the size defined in glsl");

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
      .invCanvasScale  = 1.0f / state->canvas->scale,
      .glyphsPerDim    = font->glyphsPerDim,
      .invGlyphsPerDim = 1.0f / (f32)font->glyphsPerDim,
  };
  mem_cpy(mem_var(meta.clipRects), mem_var(state->clipRects));
  return meta;
}

static UiTrackedElem* ui_canvas_tracked(UiCanvasComp* canvas, const UiId id) {
  UiTrackedElem* res = dynarray_find_or_insert_sorted(
      &canvas->trackedElems, ui_tracked_elem_compare, mem_struct(UiTrackedElem, .id = id).ptr);
  res->id = id;
  return res;
}

static UiPersistentElem* ui_canvas_persistent(UiCanvasComp* canvas, const UiId id) {
  UiPersistentElem* res = dynarray_find_or_insert_sorted(
      &canvas->persistentElems,
      ui_persistent_elem_compare,
      mem_struct(UiPersistentElem, .id = id).ptr);
  res->id = id;
  return res;
}

static f32 ui_window_scale(const GapWindowComp* window, const UiSettingsComp* settings) {
  const u16  dpi        = gap_window_dpi(window);
  const bool dpiScaling = settings && (settings->flags & UiSettingFlags_DpiScaling) != 0;
  const f32  dpiScale   = math_max(dpi / (f32)ui_canvas_dpi_reference, ui_canvas_dpi_min_scale);
  return (dpiScaling ? dpiScale : 1.0f) * (settings ? settings->scale : 1.0f);
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
  case UiLayer_OverlayInvisible:
    break;
  case UiLayer_Overlay:
    *dynarray_push_t(&state->renderer->overlayGlyphs, UiGlyphData) = data;
    break;
  }
}

static void ui_canvas_output_rect(void* userCtx, const UiId id, const UiRect rect) {
  UiRenderState* state                       = userCtx;
  ui_canvas_tracked(state->canvas, id)->rect = rect;
}

static void ui_canvas_output_text_info(void* userCtx, const UiId id, const UiBuildTextInfo info) {
  UiRenderState* state                           = userCtx;
  ui_canvas_tracked(state->canvas, id)->textInfo = info;
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
    const UiId           hoveredId,
    const UiFlags        hoveredFlags) {

  const bool inputDown     = gap_window_key_down(window, GapKey_MouseLeft);
  const bool inputPressed  = gap_window_key_pressed(window, GapKey_MouseLeft);
  const bool inputReleased = gap_window_key_released(window, GapKey_MouseLeft);

  if (UNLIKELY(settings->flags & UiSettingFlags_DebugInspector)) {
    if (inputReleased) {
      settings->flags ^= UiSettingFlags_DebugInspector;
    }
    ui_canvas_set_active(canvas, hoveredId, UiStatus_Idle);
    return; // Normal input is disabled while using the debug inspector.
  }

  const UiFlags activeFlags         = canvas->activeElemFlags;
  const bool    hasActiveElem       = !sentinel_check(canvas->activeId);
  const bool    activeElemIsHovered = canvas->activeId == hoveredId;
  const bool    activeInput = activeFlags & UiFlags_InteractOnPress ? inputPressed : inputReleased;

  if (hasActiveElem && activeElemIsHovered && activeInput) {
    ui_canvas_set_active(canvas, canvas->activeId, UiStatus_Activated);
    return;
  }
  if (hasActiveElem && activeElemIsHovered && inputDown) {
    ui_canvas_set_active(canvas, canvas->activeId, UiStatus_Pressed);
    return;
  }
  const bool allowSwitch =
      activeFlags & UiFlags_InteractAllowSwitch && hoveredFlags & UiFlags_InteractAllowSwitch;

  if (inputDown && !allowSwitch) {
    return; // Keep the same element active while holding down the input.
  }

  // Select a new active element.
  ui_canvas_set_active(
      canvas, hoveredId, sentinel_check(hoveredId) ? UiStatus_Idle : UiStatus_Hovered);
  canvas->activeElemFlags = hoveredFlags;
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
      .outputTextInfo = &ui_canvas_output_text_info,
  };
  return ui_build(state->canvas->cmdBuffer, &buildCtx);
}

ecs_view_define(GlobalView) {
  ecs_access_read(UiGlobalResourcesComp);
  ecs_access_maybe_write(InputManagerComp);
}
ecs_view_define(FtxView) { ecs_access_read(AssetFtxComp); }
ecs_view_define(WindowView) {
  ecs_access_write(GapWindowComp);
  ecs_access_maybe_write(UiRendererComp);
  ecs_access_maybe_write(UiSettingsComp);
  ecs_access_maybe_write(UiStatsComp);
}
ecs_view_define(CanvasView) { ecs_access_write(UiCanvasComp); }
ecs_view_define(DrawView) { ecs_access_write(RendDrawComp); }

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
    const GapVector       scrollDelta = gap_window_param(window, GapParam_ScrollDelta);

    if (gap_window_events(window) & GapWindowEvents_FocusLost) {
      ui_canvas_set_active(canvas, sentinel_u64, UiStatus_Idle);
    }

    if (gap_window_events(window) & GapWindowEvents_KeyPressed) {
      canvas->flags |= UiCanvasFlags_InputAny;
    } else {
      canvas->flags &= ~UiCanvasFlags_InputAny;
    }

    canvas->scale       = ui_window_scale(window, settings);
    canvas->resolution  = ui_vector(windowSize.x / canvas->scale, windowSize.y / canvas->scale);
    canvas->inputDelta  = ui_vector(cursorDelta.x / canvas->scale, cursorDelta.y / canvas->scale);
    canvas->inputPos    = ui_vector(cursorPos.x / canvas->scale, cursorPos.y / canvas->scale);
    canvas->inputScroll = ui_vector(scrollDelta.x, scrollDelta.y);
  }
}

static void ui_canvas_cursor_update(GapWindowComp* window, const UiInteractType interact) {
  switch (interact) {
  case UiInteractType_None:
    gap_window_cursor_set(window, GapCursor_Normal);
    break;
  case UiInteractType_Action:
    gap_window_cursor_set(window, GapCursor_Click);
    break;
  case UiInteractType_Resize:
    gap_window_cursor_set(window, GapCursor_ResizeDiag);
    break;
  case UiInteractType_Text:
    gap_window_cursor_set(window, GapCursor_Text);
    break;
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

  ecs_world_add_t(world, window, UiStatsComp);
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
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not initialized yet.
  }
  const UiGlobalResourcesComp* globalRes = ecs_view_read_t(globalItr, UiGlobalResourcesComp);
  InputManagerComp*            input     = ecs_view_write_t(globalItr, InputManagerComp);

  const AssetFtxComp* font = ui_global_font(world, ui_resource_font(globalRes));
  if (!font) {
    return; // Global font not loaded yet.
  }

  for (EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, WindowView)); ecs_view_walk(itr);) {
    const EcsEntityId entity   = ecs_view_entity(itr);
    GapWindowComp*    window   = ecs_view_write_t(itr, GapWindowComp);
    UiRendererComp*   renderer = ecs_view_write_t(itr, UiRendererComp);
    UiSettingsComp*   settings = ecs_view_write_t(itr, UiSettingsComp);
    UiStatsComp*      stats    = ecs_view_write_t(itr, UiStatsComp);
    if (!renderer) {
      ui_renderer_create(world, entity);
      continue;
    }
    const bool activeWindow = !input || input_active_window(input) == entity;
    if (input && activeWindow && input_triggered_lit(input, "DisableUiToggle")) {
      renderer->flags ^= UiRendererFlags_Disabled;
    }

    stats->trackedElemCount = 0;
    stats->persistElemCount = 0;
    stats->commandCount     = 0;

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
    const f32       scale       = ui_window_scale(window, settings);
    const UiVector  canvasSize  = ui_vector(winSize.x / scale, winSize.y / scale);
    UiRenderState   renderState = {
        .settings      = settings,
        .font          = font,
        .renderer      = renderer,
        .draw          = draw,
        .clipRects[0]  = {.size = canvasSize},
        .clipRectCount = 1,
    };

    UiCanvasPtr canvasses[ui_canvas_canvasses_max];
    const u32   canvasCount = ui_canvass_query(world, entity, canvasses);

    sort_quicksort_t(canvasses, canvasses + canvasCount, UiCanvasPtr, ui_canvas_ptr_compare);

    UiInteractType interactType       = UiInteractType_None;
    u32            hoveredCanvasIndex = sentinel_u32;
    UiBuildHover   hover              = {0};
    for (u32 i = 0; i != canvasCount; ++i) {
      UiCanvasComp* canvas = canvasses[i];
      canvas->order        = i;
      renderState.canvas   = canvas;

      const UiId          debugElem = ui_canvas_debug_elem(canvas, settings);
      const UiBuildResult result    = ui_canvas_build(&renderState, debugElem);
      if (!sentinel_check(result.hover.id) && result.hover.layer >= hover.layer) {
        hoveredCanvasIndex = i;
        hover              = result.hover;
        interactType       = canvas->interactType;
      }
      canvas->interactType = UiInteractType_None; // Interact type does not persist across frames.

      stats->commandCount += result.commandCount;
    }
    if (input && input_cursor_mode(input) == InputCursorMode_Locked) {
      // When the cursor is locked its be considered to not be 'hovering' over ui.
      hoveredCanvasIndex = sentinel_u32;
    }

    bool textEditActive = false;
    for (u32 i = canvasCount; i-- > 0;) { // Iterate from the top canvas to the bottom canvas.
      UiCanvasComp* canvas    = canvasses[i];
      const bool    isHovered = hoveredCanvasIndex == i && hover.layer >= canvas->minInteractLayer;
      const UiId    hoveredElem = isHovered ? hover.id : sentinel_u64;
      ui_canvas_update_interaction(canvas, settings, window, hoveredElem, hover.flags);

      if (ui_editor_active(canvas->textEditor)) {
        if (textEditActive) { // A text editor on a higher canvas is already active.
          ui_editor_stop(canvas->textEditor);
        } else {
          textEditActive         = true;
          UiTrackedElem* tracked = ui_canvas_tracked(canvas, ui_editor_element(canvas->textEditor));
          ui_editor_update(canvas->textEditor, window, hover, tracked->textInfo);
        }
      }

      stats->trackedElemCount += (u32)canvas->trackedElems.size;
      stats->persistElemCount += (u32)canvas->persistentElems.size;
    }
    if (input && activeWindow) {
      input_blocker_update(input, InputBlocker_TextInput, textEditActive);
      input_blocker_update(input, InputBlocker_HoveringUi, !sentinel_check(hoveredCanvasIndex));
    }
    ui_canvas_cursor_update(window, interactType);

    stats->canvasSize        = canvasSize;
    stats->canvasCount       = canvasCount;
    stats->glyphCount        = rend_draw_instance_count(draw);
    stats->glyphOverlayCount = (u32)renderer->overlayGlyphs.size;
    stats->clipRectCount     = renderState.clipRectCount;

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
  ecs_register_view(GlobalView);
  ecs_register_view(FtxView);
  ecs_register_view(WindowView);

  ecs_register_system(UiCanvasInputSys, ecs_view_id(CanvasView), ecs_view_id(WindowView));

  ecs_register_system(
      UiRenderSys,
      ecs_view_id(GlobalView),
      ecs_view_id(FtxView),
      ecs_view_id(WindowView),
      ecs_view_id(CanvasView),
      ecs_view_id(DrawView));

  ecs_order(UiCanvasInputSys, GapOrder_WindowUpdate + 1);
  ecs_order(UiRenderSys, UiOrder_Render);
}

EcsEntityId
ui_canvas_create(EcsWorld* world, const EcsEntityId window, const UiCanvasCreateFlags flags) {
  const EcsEntityId canvasEntity = ecs_world_entity_create(world);
  UiCanvasComp*     canvas       = ecs_world_add_t(
      world,
      canvasEntity,
      UiCanvasComp,
      .window          = window,
      .cmdBuffer       = ui_cmdbuffer_create(g_alloc_heap),
      .textEditor      = ui_editor_create(g_alloc_heap),
      .trackedElems    = dynarray_create_t(g_alloc_heap, UiTrackedElem, 16),
      .persistentElems = dynarray_create_t(g_alloc_heap, UiPersistentElem, 16));

  if (flags & UiCanvasCreateFlags_ToFront) {
    ui_canvas_to_front(canvas);
  } else if (flags & UiCanvasCreateFlags_ToBack) {
    ui_canvas_to_back(canvas);
  }

  ecs_world_add_t(world, canvasEntity, SceneLifetimeOwnerComp, .owner = window);
  return canvasEntity;
}

void ui_canvas_reset(UiCanvasComp* comp) {
  ui_cmdbuffer_clear(comp->cmdBuffer);
  comp->nextId           = 0;
  comp->minInteractLayer = 0;
}

void ui_canvas_to_front(UiCanvasComp* comp) { comp->order = i32_max; }
void ui_canvas_to_back(UiCanvasComp* comp) { comp->order = i32_min; }

void ui_canvas_min_interact_layer(UiCanvasComp* comp, const UiLayer layer) {
  comp->minInteractLayer = layer;
}

void ui_canvas_interact_type(UiCanvasComp* comp, const UiInteractType type) {
  comp->interactType = type;
}

UiId ui_canvas_id_peek(const UiCanvasComp* comp) { return comp->nextId; }
void ui_canvas_id_skip(UiCanvasComp* comp, const u64 count) { comp->nextId += count; }

void ui_canvas_id_block_next(UiCanvasComp* comp) {
  // Jump to the next 32 bit id space.
  comp->nextId = bits_align_64(comp->nextId + 1, u64_lit(1) << 32);
}

void ui_canvas_id_block_index(UiCanvasComp* comp, u32 index) {
  const u64 blockEnd   = bits_align_64(comp->nextId + 1, u64_lit(1) << 32);
  const u64 blockBegin = blockEnd - (u64_lit(1) << 32);
  comp->nextId         = blockBegin + index;
}

void ui_canvas_id_block_string(UiCanvasComp* comp, const String str) {
  ui_canvas_id_block_index(comp, string_hash(str));
}

UiStatus ui_canvas_elem_status(const UiCanvasComp* comp, const UiId id) {
  return id == comp->activeId ? comp->activeStatus : UiStatus_Idle;
}

TimeDuration ui_canvas_elem_status_duration(const UiCanvasComp* comp, const UiId id) {
  return id == comp->activeId ? time_steady_duration(comp->activeStatusStart, time_steady_clock())
                              : 0;
}

UiRect ui_canvas_elem_rect(const UiCanvasComp* comp, const UiId id) {
  return ui_canvas_tracked((UiCanvasComp*)comp, id)->rect;
}

UiStatus ui_canvas_status(const UiCanvasComp* comp) { return comp->activeStatus; }
UiVector ui_canvas_resolution(const UiCanvasComp* comp) { return comp->resolution; }
bool     ui_canvas_input_any(const UiCanvasComp* comp) {
  return (comp->flags & UiCanvasFlags_InputAny) != 0;
}
UiVector ui_canvas_input_delta(const UiCanvasComp* comp) { return comp->inputDelta; }
UiVector ui_canvas_input_pos(const UiCanvasComp* comp) { return comp->inputPos; }
UiVector ui_canvas_input_scroll(const UiCanvasComp* comp) { return comp->inputScroll; }

UiPersistentFlags ui_canvas_persistent_flags(const UiCanvasComp* comp, const UiId id) {
  return ui_canvas_persistent((UiCanvasComp*)comp, id)->flags;
}

void ui_canvas_persistent_flags_set(
    UiCanvasComp* comp, const UiId id, const UiPersistentFlags flags) {
  ui_canvas_persistent(comp, id)->flags |= flags;
}

void ui_canvas_persistent_flags_unset(
    UiCanvasComp* comp, const UiId id, const UiPersistentFlags flags) {
  ui_canvas_persistent(comp, id)->flags &= ~flags;
}

void ui_canvas_persistent_flags_toggle(
    UiCanvasComp* comp, const UiId id, const UiPersistentFlags flags) {
  ui_canvas_persistent(comp, id)->flags ^= flags;
}

UiId ui_canvas_draw_text(
    UiCanvasComp* comp,
    const String  text,
    const u16     fontSize,
    const UiAlign align,
    const UiFlags flags) {

  const UiId id = comp->nextId++;
  ui_cmd_push_draw_text(comp->cmdBuffer, id, text, fontSize, align, flags);
  return id;
}

UiId ui_canvas_draw_text_editor(
    UiCanvasComp* comp, const u16 fontSize, const UiAlign align, UiFlags flags) {

  const String text = ui_editor_visual_text(comp->textEditor);
  const UiId   id   = comp->nextId++;
  ui_cmd_push_draw_text(comp->cmdBuffer, id, text, fontSize, align, flags | UiFlags_TrackTextInfo);
  return id;
}

void ui_canvas_text_editor_start(
    UiCanvasComp*      comp,
    const String       text,
    const usize        maxLen,
    const UiId         id,
    const UiTextFilter filter) {
  ui_editor_start(comp->textEditor, text, id, maxLen, filter);
}

void ui_canvas_text_editor_stop(UiCanvasComp* comp) { ui_editor_stop(comp->textEditor); }

bool ui_canvas_text_editor_active(const UiCanvasComp* comp, const UiId id) {
  return ui_editor_active(comp->textEditor) && ui_editor_element(comp->textEditor) == id;
}

String ui_canvas_text_editor_result(UiCanvasComp* comp) {
  return ui_editor_result_text(comp->textEditor);
}

bool ui_canvas_text_is_editing(UiCanvasComp* comp, const UiId id) {
  return ui_editor_element(comp->textEditor) == id;
}

UiId ui_canvas_draw_glyph(
    UiCanvasComp* comp, const Unicode cp, const u16 maxCorner, const UiFlags flags) {
  const UiId id = comp->nextId++;
  ui_cmd_push_draw_glyph(comp->cmdBuffer, id, cp, maxCorner, flags);
  return id;
}

UiCmdBuffer* ui_canvas_cmd_buffer(UiCanvasComp* canvas) { return canvas->cmdBuffer; }
