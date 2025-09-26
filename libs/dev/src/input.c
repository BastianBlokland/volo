#include "core/array.h"
#include "core/dynstring.h"
#include "core/format.h"
#include "core/stringtable.h"
#include "dev/panel.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "gap/input.h"
#include "gap/window.h"
#include "input/manager.h"
#include "ui/canvas.h"
#include "ui/layout.h"
#include "ui/panel.h"
#include "ui/scrollview.h"
#include "ui/shape.h"
#include "ui/table.h"
#include "ui/widget.h"

typedef enum {
  DevInputTab_Actions,
  DevInputTab_Platform,

  DevInputTab_Count,
} DevInputTab;

static const String g_inputTabNames[] = {
    string_static("Actions"),
    string_static("Platform"),
};
ASSERT(array_elems(g_inputTabNames) == DevInputTab_Count, "Incorrect number of names");

ecs_comp_define(DevInputPanelComp) {
  UiPanel      panel;
  UiScrollview scrollview;
  bool         downKeysOnly;
  u32          lastRowCount;
};

static void actions_panel_tab_draw(
    UiCanvasComp* c, DevInputPanelComp* panelComp, const InputManagerComp* input) {
  ui_layout_container_push(c, UiClip_None, UiLayer_Normal);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 300);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      c,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Name"), string_lit("Action name.")},
          {string_lit("Triggered"), string_lit("Is this action currently being triggered?")},
          {string_lit("Label"), string_lit("Action label.")},
      });

  const InputActionInfo* actionData  = input_actions_data(input);
  const u32              actionCount = input_actions_count(input);

  const f32 height = ui_table_height(&table, panelComp->lastRowCount);
  ui_scrollview_begin(c, &panelComp->scrollview, UiLayer_Normal, height);

  ui_canvas_id_block_next(c); // Start the list of actions on its own id block.
  panelComp->lastRowCount = 0;
  for (u32 actionIndex = 0; actionIndex != actionCount; ++actionIndex) {
    const InputActionInfo* actionInfo = &actionData[actionIndex];
    const f32              y          = ui_table_height(&table, panelComp->lastRowCount++);
    const UiScrollviewCull cull = ui_scrollview_cull(&panelComp->scrollview, y, table.rowHeight);
    if (cull != UiScrollviewCull_Inside) {
      ui_canvas_id_skip(c, 3);
      continue;
    }
    const String actionName  = stringtable_lookup(g_stringtable, actionInfo->nameHash);
    const bool   isTriggered = input_triggered(input, actionInfo->nameHash);

    ui_table_jump_row(c, &table, panelComp->lastRowCount - 1);
    ui_table_draw_row_bg(
        c, &table, isTriggered ? ui_color(16, 64, 16, 192) : ui_color(48, 48, 48, 192));
    if (string_is_empty(actionName)) {
      ui_label(c, fmt_write_scratch("#{}", fmt_int(actionInfo->nameHash)));
    } else {
      ui_label(c, actionName, .selectable = true);
    }

    ui_table_next_column(c, &table);
    ui_label(c, isTriggered ? string_lit("yes") : string_lit("no"));

    ui_table_next_column(c, &table);
    ui_label(c, actionInfo->label, .selectable = true);
  }
  ui_canvas_id_block_next(c);

  ui_scrollview_end(c, &panelComp->scrollview);
  ui_layout_container_pop(c);
}

static void platform_options_draw(UiCanvasComp* canvas, DevInputPanelComp* panelComp) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Fixed, 25);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Down only:"));
  ui_table_next_column(canvas, &table);
  ui_toggle(canvas, &panelComp->downKeysOnly);

  ui_layout_pop(canvas);
}

static void platform_panel_tab_draw(
    UiCanvasComp*          c,
    DevInputPanelComp*     panelComp,
    const GapPlatformComp* platform,
    const GapWindowComp*   activeWindow) {
  platform_options_draw(c, panelComp);
  ui_layout_grow(c, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(c, UiClip_None, UiLayer_Normal);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 150);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      c,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Key"), string_lit("Platform key.")},
          {string_lit("Index"), string_lit("Platform key index.")},
          {string_lit("Down"), string_lit("Is the key currently held down?")},
          {string_lit("Label"), string_lit("Platform key label.")},
      });

  const f32 height = ui_table_height(&table, panelComp->lastRowCount);
  ui_scrollview_begin(c, &panelComp->scrollview, UiLayer_Normal, height);

  DynString labelBuffer = dynstring_create_over(mem_stack(64));

  ui_canvas_id_block_next(c); // Start the list of keys on its own id block.
  panelComp->lastRowCount = 0;
  for (GapKey key = 0; key != GapKey_Count; ++key) {
    const bool isDown = activeWindow && gap_window_key_down(activeWindow, key);
    if (panelComp->downKeysOnly && !isDown) {
      continue;
    }
    const f32              y    = ui_table_height(&table, panelComp->lastRowCount++);
    const UiScrollviewCull cull = ui_scrollview_cull(&panelComp->scrollview, y, table.rowHeight);
    if (cull != UiScrollviewCull_Inside) {
      ui_canvas_id_skip(c, 4);
      continue;
    }
    dynstring_clear(&labelBuffer);
    gap_key_label(platform, key, &labelBuffer);

    ui_table_jump_row(c, &table, panelComp->lastRowCount - 1);
    ui_table_draw_row_bg(c, &table, isDown ? ui_color(16, 64, 16, 192) : ui_color(48, 48, 48, 192));
    ui_label(c, gap_key_str(key), .selectable = true);

    ui_table_next_column(c, &table);
    ui_label(c, fmt_write_scratch("{}", fmt_int(key)));

    ui_table_next_column(c, &table);
    ui_label(c, isDown ? string_lit("yes") : string_lit("no"));

    ui_table_next_column(c, &table);
    ui_label(c, dynstring_view(&labelBuffer), .selectable = true);
  }
  ui_canvas_id_block_next(c);

  ui_scrollview_end(c, &panelComp->scrollview);
  ui_layout_container_pop(c);
}

static void input_panel_draw(
    UiCanvasComp*           c,
    DevInputPanelComp*      panelComp,
    const InputManagerComp* input,
    const GapPlatformComp*  platform,
    const GapWindowComp*    activeWindow) {
  const String title = fmt_write_scratch("{} Input Panel", fmt_ui_shape(Keyboard));
  ui_panel_begin(
      c,
      &panelComp->panel,
      .title       = title,
      .tabNames    = g_inputTabNames,
      .tabCount    = DevInputTab_Count,
      .topBarColor = ui_color(100, 0, 0, 192));

  switch (panelComp->panel.activeTab) {
  case DevInputTab_Actions:
    actions_panel_tab_draw(c, panelComp, input);
    break;
  case DevInputTab_Platform:
    platform_panel_tab_draw(c, panelComp, platform, activeWindow);
    break;
  }

  ui_panel_end(c, &panelComp->panel);
}

ecs_view_define(WindowView) { ecs_access_read(GapWindowComp); }

ecs_view_define(PanelUpdateGlobalView) {
  ecs_access_read(InputManagerComp);
  ecs_access_read(GapPlatformComp);
}

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DevInputPanelComp's are exclusively managed here.

  ecs_access_read(DevPanelComp);
  ecs_access_write(DevInputPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(DevInputUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, PanelUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const InputManagerComp* input    = ecs_view_read_t(globalItr, InputManagerComp);
  const GapPlatformComp*  platform = ecs_view_read_t(globalItr, GapPlatformComp);

  EcsView* windowView = ecs_world_view_t(world, WindowView);
  EcsView* panelView  = ecs_world_view_t(world, PanelUpdateView);

  const EcsEntityId    activeWindowEntity = input_active_window(input);
  EcsIterator*         activeWindowItr    = ecs_view_maybe_at(windowView, activeWindowEntity);
  const GapWindowComp* activeWindow =
      activeWindowItr ? ecs_view_read_t(activeWindowItr, GapWindowComp) : null;

  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DevInputPanelComp* panelComp = ecs_view_write_t(itr, DevInputPanelComp);
    UiCanvasComp*      canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    const bool pinned = ui_panel_pinned(&panelComp->panel);
    if (dev_panel_hidden(ecs_view_read_t(itr, DevPanelComp)) && !pinned) {
      continue;
    }
    input_panel_draw(canvas, panelComp, input, platform, activeWindow);

    if (ui_panel_closed(&panelComp->panel)) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(dev_input_module) {
  ecs_register_comp(DevInputPanelComp);

  ecs_register_system(
      DevInputUpdatePanelSys,
      ecs_register_view(WindowView),
      ecs_register_view(PanelUpdateGlobalView),
      ecs_register_view(PanelUpdateView));
}

EcsEntityId
dev_input_panel_open(EcsWorld* world, const EcsEntityId window, const DevPanelType type) {
  const EcsEntityId  panelEntity = dev_panel_create(world, window, type);
  DevInputPanelComp* inputPanel  = ecs_world_add_t(
      world, panelEntity, DevInputPanelComp, .panel = ui_panel(.size = ui_vector(800, 600)));

  if (type == DevPanelType_Detached) {
    ui_panel_maximize(&inputPanel->panel);
  }

  return panelEntity;
}
