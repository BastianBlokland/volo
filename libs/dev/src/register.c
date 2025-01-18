#include "dev_register.h"
#include "ecs_def.h"

void dev_register(EcsDef* def) {
  ecs_register_module(def, dev_asset_module);
  ecs_register_module(def, dev_camera_module);
  ecs_register_module(def, dev_ecs_module);
  ecs_register_module(def, dev_finder_module);
  ecs_register_module(def, dev_gizmo_module);
  ecs_register_module(def, dev_grid_module);
  ecs_register_module(def, dev_inspector_module);
  ecs_register_module(def, dev_interface_module);
  ecs_register_module(def, dev_level_module);
  ecs_register_module(def, dev_log_viewer_module);
  ecs_register_module(def, dev_menu_module);
  ecs_register_module(def, dev_panel_module);
  ecs_register_module(def, dev_prefab_module);
  ecs_register_module(def, dev_rend_module);
  ecs_register_module(def, dev_script_module);
  ecs_register_module(def, dev_shape_module);
  ecs_register_module(def, dev_skeleton_module);
  ecs_register_module(def, dev_sound_module);
  ecs_register_module(def, dev_stats_module);
  ecs_register_module(def, dev_text_module);
  ecs_register_module(def, dev_time_module);
  ecs_register_module(def, dev_trace_module);
  ecs_register_module(def, dev_vfx_module);
}
