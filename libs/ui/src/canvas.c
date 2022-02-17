#include "asset_ftx.h"
#include "core_alloc.h"
#include "core_dynarray.h"
#include "ecs_world.h"
#include "scene_renderable.h"
#include "ui_canvas.h"

#include "resource_internal.h"

typedef enum {
  UiCmd_DrawGlyph,
} UiCmdType;

typedef struct {
  UiElementId id;
  Unicode     cp;
} UiDrawGlyph;

typedef struct {
  UiCmdType type;
  union {
    UiDrawGlyph drawGlyph;
  };
} UiCmd;

typedef enum {
  UiFlags_Dirty = 1 << 0,
} UiFlags;

ecs_comp_define(UiCanvasComp) {
  UiFlags     flags;
  DynArray    commands; // UiCmd[]
  UiElementId nextId;
};

static void ecs_destruct_commands(void* data) {
  UiCanvasComp* comp = data;
  dynarray_destroy(&comp->commands);
}

typedef struct {
  const AssetFtxComp*        font;
  SceneRenderableUniqueComp* renderable;
} UiBuilder;

ecs_view_define(GlobalResourcesView) { ecs_access_read(UiGlobalResourcesComp); }
ecs_view_define(FtxView) { ecs_access_read(AssetFtxComp); }

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
  if (!globalRes) {
    return; // Global font not loaded yet.
  }

  EcsView* buildView = ecs_world_view_t(world, CanvasBuildView);
  for (EcsIterator* itr = ecs_view_itr(buildView); ecs_view_walk(itr);) {
    UiCanvasComp*              canvasComp = ecs_view_write_t(itr, UiCanvasComp);
    SceneRenderableUniqueComp* renderable = ecs_view_write_t(itr, SceneRenderableUniqueComp);
    if (!(canvasComp->flags & UiFlags_Dirty)) {
      continue; // Text did not change, no need to rebuild.
    }
    canvasComp->flags &= ~UiFlags_Dirty;
    renderable->graphic = ui_resource_graphic(globalRes);

    (void)font;
    // ui_canvas_build(&(UiBuilder){
    //     .font       = font,
    //     .renderable = renderable,
    // });
  }
}

ecs_module_init(ui_canvas_module) {
  ecs_register_comp(UiCanvasComp, .destructor = ecs_destruct_commands);

  ecs_register_view(CanvasBuildView);
  ecs_register_view(GlobalResourcesView);
  ecs_register_view(FtxView);

  ecs_register_system(
      UiCanvasBuildSys,
      ecs_view_id(CanvasBuildView),
      ecs_view_id(GlobalResourcesView),
      ecs_view_id(FtxView));
}

UiCanvasComp* ui_canvas_create(EcsWorld* world, const EcsEntityId entity) {
  UiCanvasComp* canvasComp = ecs_world_add_t(world, entity, UiCanvasComp);
  ecs_world_add_t(world, entity, SceneRenderableUniqueComp);
  return canvasComp;
}

void ui_canvas_reset(UiCanvasComp* comp) {
  comp->flags |= UiFlags_Dirty;
  dynarray_clear(&comp->commands);
  comp->nextId = 0;
}

UiElementId ui_canvas_draw_glyph(UiCanvasComp* comp, const Unicode cp) {
  const UiElementId id = comp->nextId++;

  *dynarray_push_t(&comp->commands, UiCmd) = (UiCmd){
      .type      = UiCmd_DrawGlyph,
      .drawGlyph = {
          .id = id,
          .cp = cp,
      }};
  return id;
}
