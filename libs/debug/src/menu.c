#include "core_alloc.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "ui.h"

static const UiVector g_debugActionBarButtonSize = {50, 50};
static const f32      g_debugActionBarSpacing    = 10;

ecs_comp_define(DebugMenuComp) {
  EcsEntityId window;
  GapVector   lastWindowedSize;
};

ecs_view_define(MenuUpdateView) {
  ecs_access_write(DebugMenuComp);
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(WindowUpdateView) { ecs_access_write(GapWindowComp); }

static void debug_action_bar_next(UiCanvasComp* canvas) {
  ui_canvas_rect_move(canvas, ui_vector(0, -1), UiOrigin_Current, UiUnits_Current, Ui_Y);
  ui_canvas_rect_move(
      canvas, ui_vector(0, -g_debugActionBarSpacing), UiOrigin_Current, UiUnits_Absolute, Ui_Y);
}

static void debug_action_bar(DebugMenuComp* menu, UiCanvasComp* canvas, GapWindowComp* window) {
  const UiVector offset = {g_debugActionBarButtonSize.x + g_debugActionBarSpacing, 0};
  ui_canvas_rect_move(canvas, offset, UiOrigin_WindowTopRight, UiUnits_Absolute, Ui_XY);
  ui_canvas_rect_resize(canvas, g_debugActionBarButtonSize, UiUnits_Absolute, Ui_XY);

  debug_action_bar_next(canvas);
  const bool    isFullScreen   = gap_window_mode(window) == GapWindowMode_Fullscreen;
  const Unicode fullscreenIcon = isFullScreen ? UiShape_FullscreenExit : UiShape_Fullscreen;
  if (ui_button(canvas, .label = ui_shape_scratch(fullscreenIcon))) {
    if (isFullScreen) {
      gap_window_resize(window, menu->lastWindowedSize, GapWindowMode_Windowed);
    } else {
      menu->lastWindowedSize = gap_window_param(window, GapParam_WindowSize);
      gap_window_resize(window, gap_vector(0, 0), GapWindowMode_Fullscreen);
    }
  }

  debug_action_bar_next(canvas);
  if (ui_button(canvas, .label = ui_shape_scratch(UiShape_Logout))) {
    gap_window_close(window);
  }
}

ecs_system_define(DebugMenuUpdateSys) {
  EcsView*     windowView = ecs_world_view_t(world, WindowUpdateView);
  EcsIterator* windowItr  = ecs_view_itr(windowView);

  EcsView* menuView = ecs_world_view_t(world, MenuUpdateView);
  for (EcsIterator* itr = ecs_view_itr(menuView); ecs_view_walk(itr);) {
    DebugMenuComp* menu   = ecs_view_write_t(itr, DebugMenuComp);
    UiCanvasComp*  canvas = ecs_view_write_t(itr, UiCanvasComp);

    if (!ecs_view_maybe_jump(windowItr, menu->window)) {
      continue;
    }
    GapWindowComp* window = ecs_view_write_t(windowItr, GapWindowComp);

    ui_canvas_reset(canvas);
    debug_action_bar(menu, canvas, window);
  }
}

ecs_module_init(debug_menu_module) {
  ecs_register_comp(DebugMenuComp);

  ecs_register_view(MenuUpdateView);
  ecs_register_view(WindowUpdateView);

  ecs_register_system(
      DebugMenuUpdateSys, ecs_view_id(MenuUpdateView), ecs_view_id(WindowUpdateView));
}

EcsEntityId debug_menu_create(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId menuEntity = ui_canvas_create(world, window);
  ecs_world_add_t(world, menuEntity, DebugMenuComp, .window = window);
  return menuEntity;
}
