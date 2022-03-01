#include "asset_ftx.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "scene_renderable.h"

#include "builder_internal.h"
#include "cmd_internal.h"
#include "resource_internal.h"
#include "shape_internal.h"

ecs_comp_define(UiCanvasComp) {
  EcsEntityId  window;
  UiCmdBuffer* cmdBuffer;
  UiId         nextId;
  UiId         activeId;
  UiStatus     activeStatus;
};

static void ecs_destruct_canvas(void* data) {
  UiCanvasComp* comp = data;
  ui_cmdbuffer_destroy(comp->cmdBuffer);
}

typedef struct {
  DynString* output;
  u32        outputNumGlyphs;
} UiRenderState;

static void ui_canvas_output_draw(void* userCtx, const UiDrawData data) {
  UiRenderState* renderState                                                = userCtx;
  *(UiDrawData*)dynstring_push(renderState->output, sizeof(UiDrawData)).ptr = data;
}

static void ui_canvas_output_glyph(void* userCtx, const UiGlyphData data) {
  UiRenderState* renderState                                                  = userCtx;
  *(UiGlyphData*)dynstring_push(renderState->output, sizeof(UiGlyphData)).ptr = data;
  ++renderState->outputNumGlyphs;
}

static void ui_canvas_update_active(
    UiCanvasComp* canvas, const GapWindowComp* window, const UiBuildResult result) {

  if (gap_window_key_released(window, GapKey_MouseLeft)) {
    canvas->activeStatus =
        result.hoveredId == canvas->activeId ? UiStatus_Activated : UiStatus_Idle;
  } else if (gap_window_key_down(window, GapKey_MouseLeft)) {
    canvas->activeStatus = result.hoveredId == canvas->activeId ? UiStatus_Down : UiStatus_Idle;
  } else {
    canvas->activeId     = result.hoveredId;
    canvas->activeStatus = UiStatus_Hovered;
  }
}

static void ui_canvas_render(
    UiCanvasComp*              canvas,
    SceneRenderableUniqueComp* renderable,
    const GapWindowComp*       window,
    const AssetFtxComp*        font) {

  DynString     dataBuffer  = dynstring_create(g_alloc_heap, 512);
  UiRenderState renderState = {
      .output = &dataBuffer,
  };

  const UiBuildCtx buildCtx = {
      .window      = window,
      .font        = font,
      .userCtx     = &renderState,
      .outputDraw  = &ui_canvas_output_draw,
      .outputGlyph = &ui_canvas_output_glyph,
  };
  const UiBuildResult result = ui_build(canvas->cmdBuffer, &buildCtx);

  const Mem instData = scene_renderable_unique_data_set(renderable, renderState.output->size);
  mem_cpy(instData, dynstring_view(renderState.output));
  renderable->vertexCountOverride = renderState.outputNumGlyphs * 6; // 6 verts per quad.

  dynstring_destroy(&dataBuffer);

  ui_canvas_update_active(canvas, window, result);
}

ecs_view_define(GlobalResourcesView) { ecs_access_read(UiGlobalResourcesComp); }
ecs_view_define(FtxView) { ecs_access_read(AssetFtxComp); }
ecs_view_define(WindowView) { ecs_access_read(GapWindowComp); }

ecs_view_define(CanvasBuildView) {
  ecs_access_write(UiCanvasComp);
  ecs_access_write(SceneRenderableUniqueComp);
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
    UiCanvasComp*              canvasComp = ecs_view_write_t(itr, UiCanvasComp);
    SceneRenderableUniqueComp* renderable = ecs_view_write_t(itr, SceneRenderableUniqueComp);
    if (!ecs_view_maybe_jump(windowItr, canvasComp->window)) {
      // Destroy the canvas if the associated window is destroyed.
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
      continue;
    }
    const GapWindowComp* window = ecs_view_read_t(windowItr, GapWindowComp);

    renderable->graphic = ui_resource_graphic(globalRes);
    ui_canvas_render(canvasComp, renderable, window, font);
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
}

EcsEntityId ui_canvas_create(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId canvasEntity = ecs_world_entity_create(world);
  ecs_world_add_t(
      world,
      canvasEntity,
      UiCanvasComp,
      .window    = window,
      .cmdBuffer = ui_cmdbuffer_create(g_alloc_heap));
  ecs_world_add_t(world, canvasEntity, SceneRenderableUniqueComp);
  return canvasEntity;
}

void ui_canvas_reset(UiCanvasComp* comp) {
  ui_cmdbuffer_clear(comp->cmdBuffer);
  comp->nextId = 0;
}

UiId ui_canvas_next_id(const UiCanvasComp* comp) { return comp->nextId; }

UiStatus ui_canvas_status(const UiCanvasComp* comp, const UiId id) {
  if (comp->activeId == id) {
    return comp->activeStatus;
  }
  return UiStatus_Idle;
}

void ui_canvas_move(
    UiCanvasComp* comp, const UiVector pos, const UiOrigin origin, const UiUnits unit) {
  ui_cmd_push_move(comp->cmdBuffer, pos, origin, unit);
}

void ui_canvas_size(UiCanvasComp* comp, const UiVector size, const UiUnits unit) {
  diag_assert_msg(size.x >= 0.0f && size.y >= 0.0f, "Negative sizes are not supported");
  ui_cmd_push_size(comp->cmdBuffer, size, unit);
}

void ui_canvas_size_to(
    UiCanvasComp* comp, const UiVector pos, const UiOrigin origin, const UiUnits unit) {
  ui_cmd_push_size_to(comp->cmdBuffer, pos, origin, unit);
}

void ui_canvas_style(UiCanvasComp* comp, const UiColor color, const u8 outline) {
  ui_cmd_push_style(comp->cmdBuffer, color, outline);
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

UiId ui_canvas_draw_square(UiCanvasComp* comp, const UiFlags flags) {
  return ui_canvas_draw_glyph(comp, ui_shape_square, 25, flags);
}

UiId ui_canvas_draw_circle(UiCanvasComp* comp, const u16 maxCorner, const UiFlags flags) {
  return ui_canvas_draw_glyph(comp, ui_shape_circle, maxCorner, flags);
}
