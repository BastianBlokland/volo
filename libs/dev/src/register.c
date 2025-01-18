#include "dev_register.h"
#include "ecs_def.h"

void debug_register(EcsDef* def) {
  ecs_register_module(def, debug_asset_module);
  ecs_register_module(def, debug_camera_module);
  ecs_register_module(def, debug_ecs_module);
  ecs_register_module(def, debug_finder_module);
  ecs_register_module(def, debug_gizmo_module);
  ecs_register_module(def, debug_grid_module);
  ecs_register_module(def, debug_inspector_module);
  ecs_register_module(def, debug_interface_module);
  ecs_register_module(def, debug_level_module);
  ecs_register_module(def, debug_log_viewer_module);
  ecs_register_module(def, dev_menu_module);
  ecs_register_module(def, dev_panel_module);
  ecs_register_module(def, debug_prefab_module);
  ecs_register_module(def, debug_rend_module);
  ecs_register_module(def, debug_script_module);
  ecs_register_module(def, debug_shape_module);
  ecs_register_module(def, debug_skeleton_module);
  ecs_register_module(def, debug_sound_module);
  ecs_register_module(def, debug_stats_module);
  ecs_register_module(def, debug_text_module);
  ecs_register_module(def, debug_time_module);
  ecs_register_module(def, debug_trace_module);
  ecs_register_module(def, debug_vfx_module);
}
