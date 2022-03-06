#include "asset_ftx.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "rend_draw.h"
#include "ui_register.h"

#include "builder_internal.h"
#include "cmd_internal.h"
#include "resource_internal.h"

typedef struct {
  UiRect   rect;
  UiStatus status;
} UiElement;

ecs_comp_define(UiCanvasComp) {
  EcsEntityId  window;
  UiCmdBuffer* cmdBuffer;
  UiId         nextId;
  DynArray     elements; // UiElement[]
  UiId         activeId;
  UiVector     inputDelta, inputPos;
};

static void ecs_destruct_canvas(void* data) {
  UiCanvasComp* comp = data;
  ui_cmdbuffer_destroy(comp->cmdBuffer);
  dynarray_destroy(&comp->elements);
}

typedef struct {
  DynString* output;
  u32        outputNumGlyphs;
  DynArray*  elementsOutput; // UiElement[]*
} UiRenderState;

static const UiElement* ui_build_elem(const UiCanvasComp* canvas, const UiId id) {
  if (id >= canvas->elements.size) {
    return null;
  }
  return dynarray_at_t(&canvas->elements, id, UiElement);
}

static UiElement* ui_build_elem_mutable(UiCanvasComp* canvas, const UiId id) {
  return (UiElement*)ui_build_elem(canvas, id);
}

static void ui_canvas_output_draw(void* userCtx, const UiDrawData data) {
  UiRenderState* renderState                                                = userCtx;
  *(UiDrawData*)dynstring_push(renderState->output, sizeof(UiDrawData)).ptr = data;
}

static void ui_canvas_output_glyph(void* userCtx, const UiGlyphData data) {
  UiRenderState* renderState                                                  = userCtx;
  *(UiGlyphData*)dynstring_push(renderState->output, sizeof(UiGlyphData)).ptr = data;
  ++renderState->outputNumGlyphs;
}

static void ui_canvas_output_rect(void* userCtx, const UiId id, const UiRect rect) {
  UiRenderState* renderState                                      = userCtx;
  dynarray_at_t(renderState->elementsOutput, id, UiElement)->rect = rect;
}

static void ui_canvas_update_input(
    UiCanvasComp* canvas, const GapWindowComp* window, const UiBuildResult result) {

  const GapVector cursorDelta = gap_window_param(window, GapParam_CursorDelta);
  const GapVector cursorPos   = gap_window_param(window, GapParam_CursorPos);
  canvas->inputDelta          = ui_vector(cursorDelta.x, cursorDelta.y);
  canvas->inputPos            = ui_vector(cursorPos.x, cursorPos.y);

  const bool inputDown     = gap_window_key_down(window, GapKey_MouseLeft);
  const bool inputReleased = gap_window_key_released(window, GapKey_MouseLeft);

  UiElement* activeElem  = ui_build_elem_mutable(canvas, canvas->activeId);
  UiElement* hoveredElem = ui_build_elem_mutable(canvas, result.hoveredId);

  if (activeElem && activeElem == hoveredElem && inputReleased) {
    activeElem->status = UiStatus_Activated;
    return;
  }
  if (activeElem && activeElem == hoveredElem && inputDown) {
    activeElem->status = UiStatus_Pressed;
    return;
  }
  if (inputDown) {
    return; // Keep the same element active while holding down the input.
  }

  // Select a new active element.
  if (activeElem) {
    activeElem->status = UiStatus_Idle;
  }
  if (hoveredElem) {
    hoveredElem->status = UiStatus_Hovered;
  }
  canvas->activeId = result.hoveredId;
}

static void ui_canvas_render(
    UiCanvasComp*        canvas,
    RendDrawComp*        draw,
    const GapWindowComp* window,
    const AssetFtxComp*  font) {

  // Ensure we have UiElement structures for all elements that are requested to be drawn.
  dynarray_resize(&canvas->elements, canvas->nextId);

  DynString     dataBuffer  = dynstring_create(g_alloc_heap, 512);
  UiRenderState renderState = {
      .output         = &dataBuffer,
      .elementsOutput = &canvas->elements,
  };

  const UiBuildCtx buildCtx = {
      .window      = window,
      .font        = font,
      .userCtx     = &renderState,
      .outputDraw  = &ui_canvas_output_draw,
      .outputGlyph = &ui_canvas_output_glyph,
      .outputRect  = &ui_canvas_output_rect,
  };
  const UiBuildResult result = ui_build(canvas->cmdBuffer, &buildCtx);

  rend_draw_set_data_size(draw, (u32)renderState.output->size);
  rend_draw_set_vertex_count(draw, renderState.outputNumGlyphs * 6); // 6 verts per quad.

  const SceneTags tags = SceneTags_Default;
  const Mem       data = dynstring_view(renderState.output);
  const GeoBox    aabb = geo_box_inverted3();
  mem_cpy(rend_draw_add_instance(draw, tags, aabb), data);

  dynstring_destroy(&dataBuffer);

  ui_canvas_update_input(canvas, window, result);
}

ecs_view_define(GlobalResourcesView) { ecs_access_read(UiGlobalResourcesComp); }
ecs_view_define(FtxView) { ecs_access_read(AssetFtxComp); }
ecs_view_define(WindowView) { ecs_access_read(GapWindowComp); }

ecs_view_define(CanvasBuildView) {
  ecs_access_write(UiCanvasComp);
  ecs_access_write(RendDrawComp);
}

static const UiGlobalResourcesComp* ui_global_resources(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourcesView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_read_t(globalItr, UiGlobalResourcesComp) : null;
}

static const AssetFtxComp* ui_global_font(EcsWorld* world, const EcsEntityId entity) {
  EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(world, FtxView), entity);
  return itr ? ecs_view_read_t(itr, AssetFtxComp) : null;
}

ecs_system_define(UiCanvasBuildSys) {
  const UiGlobalResourcesComp* globalRes = ui_global_resources(world);
  if (!globalRes) {
    return; // Global resources not initialized yet.
  }
  const AssetFtxComp* font = ui_global_font(world, ui_resource_font(globalRes));
  if (!font) {
    return; // Global font not loaded yet.
  }
  EcsView*     windowView = ecs_world_view_t(world, WindowView);
  EcsIterator* windowItr  = ecs_view_itr(windowView);

  EcsView* buildView = ecs_world_view_t(world, CanvasBuildView);
  for (EcsIterator* itr = ecs_view_itr(buildView); ecs_view_walk(itr);) {
    UiCanvasComp* canvasComp = ecs_view_write_t(itr, UiCanvasComp);
    RendDrawComp* draw       = ecs_view_write_t(itr, RendDrawComp);
    if (!ecs_view_maybe_jump(windowItr, canvasComp->window)) {
      // Destroy the canvas if the associated window is destroyed.
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
      continue;
    }
    const GapWindowComp* window = ecs_view_read_t(windowItr, GapWindowComp);

    rend_draw_set_graphic(draw, ui_resource_graphic(globalRes));

    ui_canvas_render(canvasComp, draw, window, font);
  }
}

ecs_module_init(ui_canvas_module) {
  ecs_register_comp(UiCanvasComp, .destructor = ecs_destruct_canvas);

  ecs_register_view(CanvasBuildView);
  ecs_register_view(GlobalResourcesView);
  ecs_register_view(FtxView);
  ecs_register_view(WindowView);

  ecs_register_system(
      UiCanvasBuildSys,
      ecs_view_id(CanvasBuildView),
      ecs_view_id(GlobalResourcesView),
      ecs_view_id(FtxView),
      ecs_view_id(WindowView));

  ecs_order(UiCanvasBuildSys, UiOrder_CanvasBuild);
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
  rend_draw_create(world, canvasEntity);
  return canvasEntity;
}

void ui_canvas_reset(UiCanvasComp* comp) {
  ui_cmdbuffer_clear(comp->cmdBuffer);
  comp->nextId = 0;
}

UiId ui_canvas_id_peek(const UiCanvasComp* comp) { return comp->nextId; }
void ui_canvas_id_skip(UiCanvasComp* comp) { ++comp->nextId; }

UiStatus ui_canvas_elem_status(const UiCanvasComp* comp, const UiId id) {
  const UiElement* elem = ui_build_elem(comp, id);
  return elem ? elem->status : UiStatus_Idle;
}

UiRect ui_canvas_elem_rect(const UiCanvasComp* comp, const UiId id) {
  const UiElement* elem = ui_build_elem(comp, id);
  return elem ? elem->rect : ui_rect(ui_vector(0, 0), ui_vector(0, 0));
}

UiVector ui_canvas_input_delta(const UiCanvasComp* comp) { return comp->inputDelta; }
UiVector ui_canvas_input_pos(const UiCanvasComp* comp) { return comp->inputPos; }

void ui_canvas_rect_push(UiCanvasComp* comp) { ui_cmd_push_rect_push(comp->cmdBuffer); }
void ui_canvas_rect_pop(UiCanvasComp* comp) { ui_cmd_push_rect_pop(comp->cmdBuffer); }

void ui_canvas_rect_move(
    UiCanvasComp*  comp,
    const UiVector pos,
    const UiOrigin origin,
    const UiUnits  unit,
    const UiAxis   axis) {
  ui_cmd_push_rect_move(comp->cmdBuffer, pos, origin, unit, axis);
}

void ui_canvas_rect_resize(
    UiCanvasComp* comp, const UiVector size, const UiUnits unit, const UiAxis axis) {
  diag_assert_msg(size.x >= 0.0f && size.y >= 0.0f, "Negative sizes are not supported");
  ui_cmd_push_rect_resize(comp->cmdBuffer, size, unit, axis);
}

void ui_canvas_rect_resize_to(
    UiCanvasComp*  comp,
    const UiVector pos,
    const UiOrigin origin,
    const UiUnits  unit,
    const UiAxis   axis) {
  ui_cmd_push_rect_resize_to(comp->cmdBuffer, pos, origin, unit, axis);
}

void ui_canvas_style_push(UiCanvasComp* comp) { ui_cmd_push_style_push(comp->cmdBuffer); }
void ui_canvas_style_pop(UiCanvasComp* comp) { ui_cmd_push_style_pop(comp->cmdBuffer); }

void ui_canvas_style_color(UiCanvasComp* comp, const UiColor color) {
  ui_cmd_push_style_color(comp->cmdBuffer, color);
}

void ui_canvas_style_outline(UiCanvasComp* comp, const u8 outline) {
  ui_cmd_push_style_outline(comp->cmdBuffer, outline);
}

UiId ui_canvas_draw_text(
    UiCanvasComp*     comp,
    const String      text,
    const u16         fontSize,
    const UiTextAlign align,
    const UiFlags     flags) {

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
