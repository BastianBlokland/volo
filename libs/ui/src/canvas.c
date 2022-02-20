#include "asset_ftx.h"
#include "core_dynstring.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "scene_renderable.h"
#include "ui_canvas.h"

#include "builder_internal.h"
#include "cmd_internal.h"
#include "resource_internal.h"

ecs_comp_define(UiCanvasComp) {
  EcsEntityId  window;
  UiCmdBuffer* cmdBuffer;
  UiElementId  nextId;
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

static void ui_canvas_render(
    const UiCanvasComp*        canvas,
    SceneRenderableUniqueComp* renderable,
    const GapWindowComp*       window,
    const AssetFtxComp*        font) {

  DynString     dataBuffer  = dynstring_create(g_alloc_heap, 512);
  UiRenderState renderState = {
      .output = &dataBuffer,
  };

  UiBuildCtx buildCtx = {
      .userCtx     = &renderState,
      .outputDraw  = &ui_canvas_output_draw,
      .outputGlyph = &ui_canvas_output_glyph,
  };
  ui_build(canvas->cmdBuffer, window, font, &buildCtx);

  const Mem instData = scene_renderable_unique_data_set(renderable, renderState.output->size);
  mem_cpy(instData, dynstring_view(renderState.output));
  renderable->vertexCountOverride = renderState.outputNumGlyphs * 6; // 6 verts per quad.

  dynstring_destroy(&dataBuffer);
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

void ui_canvas_set_pos(UiCanvasComp* comp, const UiVector pos, const UiOrigin origin) {
  ui_cmd_push_set_pos(comp->cmdBuffer, pos, origin);
}

void ui_canvas_set_size(UiCanvasComp* comp, const UiVector size, const UiUnits units) {
  ui_cmd_push_set_size(comp->cmdBuffer, size, units);
}

void ui_canvas_set_color(UiCanvasComp* comp, const UiColor color) {
  ui_cmd_push_set_color(comp->cmdBuffer, color);
}

UiElementId ui_canvas_draw_glyph(UiCanvasComp* comp, const Unicode cp) {
  const UiElementId id = comp->nextId++;
  ui_cmd_push_draw_glyph(comp->cmdBuffer, id, cp);
  return id;
}
