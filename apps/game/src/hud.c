#include "ecs_world.h"
#include "scene_camera.h"
#include "scene_transform.h"
#include "ui.h"

#include "hud_internal.h"

ecs_comp_define(HudComp) { EcsEntityId uiCanvas; };

ecs_view_define(HudView) {
  ecs_access_read(HudComp);
  ecs_access_read(SceneCameraComp);
}
ecs_view_define(UiCanvasView) { ecs_access_write(UiCanvasComp); }

ecs_system_define(HudDrawUiSys) {
  EcsView* hudView    = ecs_world_view_t(world, HudView);
  EcsView* canvasView = ecs_world_view_t(world, UiCanvasView);

  EcsIterator* canvasItr = ecs_view_itr(canvasView);

  for (EcsIterator* itr = ecs_view_itr(hudView); ecs_view_walk(itr);) {
    HudComp* state = ecs_view_write_t(itr, HudComp);
    if (!ecs_view_maybe_jump(canvasItr, state->uiCanvas)) {
      continue;
    }
    UiCanvasComp* canvas = ecs_view_write_t(canvasItr, UiCanvasComp);
    ui_canvas_reset(canvas);
    ui_canvas_to_back(canvas);
  }
}

ecs_module_init(game_hud_module) {
  ecs_register_comp(HudComp);

  ecs_register_view(HudView);
  ecs_register_view(UiCanvasView);

  ecs_register_system(HudDrawUiSys, ecs_view_id(HudView), ecs_view_id(UiCanvasView));

  enum {
    Order_Normal    = 0,
    Order_HudDrawUi = 1,
  };
  ecs_order(HudDrawUiSys, Order_HudDrawUi);
}

void hud_init(EcsWorld* world, const EcsEntityId cameraEntity) {
  ecs_world_add_t(
      world,
      cameraEntity,
      HudComp,
      .uiCanvas = ui_canvas_create(world, cameraEntity, UiCanvasCreateFlags_None));
}
