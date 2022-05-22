#include "asset_manager.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs_world.h"
#include "rend_draw.h"
#include "rend_register.h"
#include "rend_reset.h"
#include "rend_resource.h"
#include "rend_settings.h"
#include "ui.h"

// clang-format off

static const String g_tooltipPresent        = string_static("Presentation mode.\n\n"
                                                            "Options:\n"
                                                            "- \a.bImmediate\ar: Don't wait for a vblank but immediately output the new image.\n"
                                                            "- \a.bSync\ar: Wait for the next vblank to output the new image.\n"
                                                            "- \a.bVSyncRelaxed\ar: Wait for the next vblank if the application is early, if the application is late then immediately output the new image.\n"
                                                            "- \a.bMailbox\ar: Wait for the next vblank to output a new image, but does not block acquiring a next image. If the application finishes another image before the vblank then it will replace the currently waiting image.");
static const String g_tooltipScale          = string_static("Render resolution scale.");
static const String g_tooltipLimiter        = string_static("Frame frequency limiter (in hz).\n\a.bNote\ar: 0 disables the limiter.");
static const String g_tooltipFrustumCulling = string_static("Should draws be culled if their bounds are outside of the view frustum?");
static const String g_tooltipValidation     = string_static("Should gpu api validation be enabled?\n\a.bNote\ar: Requires a reset to take effect.");
static const String g_tooltipVerbose        = string_static("Should verbose logging be enabled?");
static const String g_tooltipDefaults       = string_static("Reset all settings to their defaults.");
static const String g_tooltipReset          = string_static("Re-initialize the renderer.");
static const String g_tooltipFreeze         = string_static("Freeze the data set (halts data collection).");

// clang-format on

typedef enum {
  DebugRendTab_Settings,
  DebugRendTab_Draws,
  DebugRendTab_Resources,

  DebugRendTab_Count,
} DebugRendTab;

static const String g_rendTabNames[] = {
    string_static("Settings"),
    string_static("Draws"),
    string_static("Resources"),
};
ASSERT(array_elems(g_rendTabNames) == DebugRendTab_Count, "Incorrect number of names");

typedef enum {
  DebugRendDrawMode_Graphic,
  DebugRendDrawMode_RenderOrder,

  DebugRendDrawMode_Count,
} DebugRendDrawMode;

static const String g_drawSortModeNames[] = {
    string_static("Graphic"),
    string_static("Order"),
};
ASSERT(array_elems(g_drawSortModeNames) == DebugRendDrawMode_Count, "Incorrect number of names");

static const String g_presentOptions[] = {
    string_static("Immediate"),
    string_static("VSync"),
    string_static("VSyncRelaxed"),
    string_static("Mailbox"),
};

typedef struct {
  String graphicName;
  i32    renderOrder;
  u32    instanceCount;
  u32    dataSize, dataInstSize;
} DebugDrawInfo;

ecs_comp_define(DebugRendPanelComp) {
  UiPanel           panel;
  EcsEntityId       window;
  UiScrollview      scrollview;
  DebugRendDrawMode drawSortMode;
  DynArray          draws; // DebugDrawInfo[]
  bool              freeze;
  bool              hideEmptyDraws;
};

ecs_view_define(DrawView) { ecs_access_read(RendDrawComp); }

ecs_view_define(GraphicView) {
  ecs_access_read(AssetComp);
  ecs_access_maybe_read(RendResGraphicComp);
}

static void ecs_destruct_rend_panel(void* data) {
  DebugRendPanelComp* comp = data;
  dynarray_destroy(&comp->draws);
}

static i8 rend_draw_compare_name(const void* a, const void* b) {
  return compare_string(
      field_ptr(a, DebugDrawInfo, graphicName), field_ptr(b, DebugDrawInfo, graphicName));
}

static i8 rend_draw_compare_render_order(const void* a, const void* b) {
  const DebugDrawInfo* drawA = a;
  const DebugDrawInfo* drawB = b;
  i8                   order = compare_i32_reverse(&drawA->renderOrder, &drawB->renderOrder);
  if (!order) {
    order = compare_string(&drawA->graphicName, &drawB->graphicName);
  }
  return order;
}

static void rend_settings_tab_draw(
    EcsWorld*               world,
    UiCanvasComp*           canvas,
    RendSettingsComp*       settings,
    RendGlobalSettingsComp* globalSettings) {
  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 200);
  ui_table_add_column(&table, UiTableColumn_Fixed, 300);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Present mode"));
  ui_table_next_column(canvas, &table);
  ui_select(
      canvas,
      (i32*)&settings->presentMode,
      g_presentOptions,
      array_elems(g_presentOptions),
      .tooltip = g_tooltipPresent);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Limiter"));
  ui_table_next_column(canvas, &table);
  f32 limiterFreq = globalSettings->limiterFreq;
  if (ui_slider(
          canvas, &limiterFreq, .min = 0, .max = 240, .step = 30, .tooltip = g_tooltipLimiter)) {
    globalSettings->limiterFreq = (u16)limiterFreq;
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Scale"));
  ui_table_next_column(canvas, &table);
  ui_slider(
      canvas,
      &settings->resolutionScale,
      .min     = 0.2f,
      .max     = 2.0f,
      .step    = 0.1f,
      .tooltip = g_tooltipScale);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Frustum culling"));
  ui_table_next_column(canvas, &table);
  bool frustumCulling = (settings->flags & RendFlags_FrustumCulling) != 0;
  if (ui_toggle(canvas, &frustumCulling, .tooltip = g_tooltipFrustumCulling)) {
    settings->flags ^= RendFlags_FrustumCulling;
  }
  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Wireframe"));
  ui_table_next_column(canvas, &table);
  bool wireframe = (settings->flags & RendFlags_Wireframe) != 0;
  if (ui_toggle(canvas, &wireframe)) {
    settings->flags ^= RendFlags_Wireframe;
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Validation"));
  ui_table_next_column(canvas, &table);
  bool validation = (globalSettings->flags & RendGlobalFlags_Validation) != 0;
  if (ui_toggle(canvas, &validation, .tooltip = g_tooltipValidation)) {
    globalSettings->flags ^= RendGlobalFlags_Validation;
  }

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Verbose"));
  ui_table_next_column(canvas, &table);
  bool verbose = (globalSettings->flags & RendGlobalFlags_Verbose) != 0;
  if (ui_toggle(canvas, &verbose, .tooltip = g_tooltipVerbose)) {
    globalSettings->flags ^= RendGlobalFlags_Verbose;
  }

  ui_table_next_row(canvas, &table);
  if (ui_button(canvas, .label = string_lit("Defaults"), .tooltip = g_tooltipDefaults)) {
    rend_settings_to_default(settings);
    rend_global_settings_to_default(globalSettings);
  }
  ui_table_next_row(canvas, &table);
  if (ui_button(
          canvas,
          .label      = string_lit("Reset"),
          .frameColor = ui_color(255, 0, 0, 192),
          .tooltip    = g_tooltipReset)) {
    rend_reset(world);
  }
}

static UiColor rend_draw_bg_color(const DebugDrawInfo* drawInfo) {
  return drawInfo->instanceCount ? ui_color(16, 64, 16, 192) : ui_color(48, 48, 48, 192);
}

static void rend_draw_options_draw(UiCanvasComp* canvas, DebugRendPanelComp* panelComp) {
  ui_layout_push(canvas);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);
  ui_table_add_column(&table, UiTableColumn_Fixed, 150);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 25);
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Fixed, 25);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Sort:"));
  ui_table_next_column(canvas, &table);
  ui_select(canvas, (i32*)&panelComp->drawSortMode, g_drawSortModeNames, DebugRendDrawMode_Count);
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Freeze:"));
  ui_table_next_column(canvas, &table);
  ui_toggle(canvas, &panelComp->freeze, .tooltip = g_tooltipFreeze);
  ui_table_next_column(canvas, &table);
  ui_label(canvas, string_lit("Hide empty:"));
  ui_table_next_column(canvas, &table);
  ui_toggle(canvas, &panelComp->hideEmptyDraws);

  ui_layout_pop(canvas);
}

static void rend_draw_info_query(DebugRendPanelComp* panelComp, EcsWorld* world) {
  if (!panelComp->freeze) {
    dynarray_clear(&panelComp->draws);
    EcsView*     graphicView = ecs_world_view_t(world, GraphicView);
    EcsIterator* graphicItr  = ecs_view_itr(graphicView);
    EcsView*     drawView    = ecs_world_view_t(world, DrawView);
    for (EcsIterator* itr = ecs_view_itr(drawView); ecs_view_walk(itr);) {
      const RendDrawComp* drawComp = ecs_view_read_t(itr, RendDrawComp);
      if (panelComp->hideEmptyDraws && !rend_draw_instance_count(drawComp)) {
        continue;
      }

      String graphicName = string_lit("< unknown >");
      i32    renderOrder = 0;
      if (ecs_view_contains(graphicView, rend_draw_graphic(drawComp))) {
        ecs_view_jump(graphicItr, rend_draw_graphic(drawComp));
        const AssetComp*          graphicAssetComp = ecs_view_read_t(graphicItr, AssetComp);
        const RendResGraphicComp* graphicComp = ecs_view_read_t(graphicItr, RendResGraphicComp);
        graphicName                           = asset_id(graphicAssetComp);
        if (graphicComp) {
          renderOrder = rend_res_render_order(graphicComp);
        }
      }
      *dynarray_push_t(&panelComp->draws, DebugDrawInfo) = (DebugDrawInfo){
          .graphicName   = graphicName,
          .instanceCount = rend_draw_instance_count(drawComp),
          .dataSize      = rend_draw_data_size(drawComp),
          .dataInstSize  = rend_draw_data_inst_size(drawComp),
          .renderOrder   = renderOrder,
      };
    }
  }

  switch (panelComp->drawSortMode) {
  case DebugRendDrawMode_Graphic:
    dynarray_sort(&panelComp->draws, rend_draw_compare_name);
    break;
  case DebugRendDrawMode_RenderOrder:
    dynarray_sort(&panelComp->draws, rend_draw_compare_render_order);
    break;
  case DebugRendDrawMode_Count:
    break;
  }
}

static void rend_draw_tab_draw(UiCanvasComp* canvas, DebugRendPanelComp* panelComp) {
  rend_draw_options_draw(canvas, panelComp);
  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(canvas, UiClip_None);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 250);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Graphic"), string_lit("Name of this draw's graphic asset.")},
          {string_lit("Order"), string_lit("Render order for this draw's graphic.")},
          {string_lit("Instances"), string_lit("Number of instances in this draw.")},
          {string_lit("Draw Size"), string_lit("Per draw data-size.")},
          {string_lit("Inst Size"), string_lit("Per instance data-size.")},
          {string_lit("Total Size"), string_lit("Total data-size.")},
      });

  const u32 numDraws = (u32)panelComp->draws.size;
  ui_scrollview_begin(canvas, &panelComp->scrollview, ui_table_height(&table, numDraws));

  ui_canvas_id_block_next(canvas); // Start the list of draws on its own id block.
  dynarray_for_t(&panelComp->draws, DebugDrawInfo, drawInfo) {
    ui_table_next_row(canvas, &table);
    ui_table_draw_row_bg(canvas, &table, rend_draw_bg_color(drawInfo));

    ui_canvas_id_block_string(canvas, drawInfo->graphicName); // Set a stable canvas id.

    ui_label(canvas, drawInfo->graphicName, .selectable = true);
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(drawInfo->renderOrder)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_int(drawInfo->instanceCount)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_size(drawInfo->dataSize)));
    ui_table_next_column(canvas, &table);
    ui_label(canvas, fmt_write_scratch("{}", fmt_size(drawInfo->dataInstSize)));
    ui_table_next_column(canvas, &table);
    const u32 totalDataSize = drawInfo->dataSize + drawInfo->dataInstSize * drawInfo->instanceCount;
    ui_label(canvas, fmt_write_scratch("{}", fmt_size(totalDataSize)));
  }
  ui_canvas_id_block_next(canvas);

  ui_scrollview_end(canvas, &panelComp->scrollview);
  ui_layout_container_pop(canvas);
}

static void rend_resources_tab_draw(UiCanvasComp* canvas, DebugRendPanelComp* panelComp) {
  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 250);
  ui_table_add_column(&table, UiTableColumn_Fixed, 75);

  ui_table_draw_header(
      canvas,
      &table,
      (const UiTableColumnName[]){
          {string_lit("A"), string_lit("Test A.")},
          {string_lit("B"), string_lit("Test B.")},
      });

  (void)panelComp;
}

static void rend_panel_draw(
    EcsWorld*               world,
    UiCanvasComp*           canvas,
    DebugRendPanelComp*     panelComp,
    RendSettingsComp*       settings,
    RendGlobalSettingsComp* globalSettings) {

  const String title = fmt_write_scratch("{} Renderer Settings", fmt_ui_shape(Brush));
  ui_panel_begin(
      canvas,
      &panelComp->panel,
      .title    = title,
      .tabNames = g_rendTabNames,
      .tabCount = DebugRendTab_Count);

  switch (panelComp->panel.activeTab) {
  case DebugRendTab_Settings:
    rend_settings_tab_draw(world, canvas, settings, globalSettings);
    break;
  case DebugRendTab_Draws:
    rend_draw_info_query(panelComp, world);
    rend_draw_tab_draw(canvas, panelComp);
    break;
  case DebugRendTab_Resources:
    rend_resources_tab_draw(canvas, panelComp);
    break;
  }

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_view_define(GlobalView) { ecs_access_write(RendGlobalSettingsComp); }
ecs_view_define(WindowView) { ecs_access_write(RendSettingsComp); }

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugRendPanelComp);
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(DebugRendUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  RendGlobalSettingsComp* globalSettings = ecs_view_write_t(globalItr, RendGlobalSettingsComp);

  EcsIterator* windowItr = ecs_view_itr(ecs_world_view_t(world, WindowView));

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    DebugRendPanelComp* panelComp = ecs_view_write_t(itr, DebugRendPanelComp);
    UiCanvasComp*       canvas    = ecs_view_write_t(itr, UiCanvasComp);

    if (!ecs_view_maybe_jump(windowItr, panelComp->window)) {
      continue; // Window has been destroyed, or has no render settings.
    }
    RendSettingsComp* settings = ecs_view_write_t(windowItr, RendSettingsComp);

    ui_canvas_reset(canvas);
    rend_panel_draw(world, canvas, panelComp, settings, globalSettings);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_rend_module) {
  ecs_register_comp(DebugRendPanelComp, .destructor = ecs_destruct_rend_panel);

  ecs_register_view(DrawView);
  ecs_register_view(GraphicView);
  ecs_register_view(GlobalView);
  ecs_register_view(WindowView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DebugRendUpdatePanelSys,
      ecs_view_id(DrawView),
      ecs_view_id(GraphicView),
      ecs_view_id(PanelUpdateView),
      ecs_view_id(WindowView),
      ecs_view_id(GlobalView));

  // NOTE: Update the panel before clearing the draws so we can inspect the last frame's draw.
  ecs_order(DebugRendUpdatePanelSys, RendOrder_DrawClear - 1);
}

EcsEntityId debug_rend_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugRendPanelComp,
      .panel          = ui_panel(ui_vector(700, 400)),
      .window         = window,
      .scrollview     = ui_scrollview(),
      .drawSortMode   = DebugRendDrawMode_RenderOrder,
      .draws          = dynarray_create_t(g_alloc_heap, DebugDrawInfo, 256),
      .hideEmptyDraws = true);
  return panelEntity;
}
