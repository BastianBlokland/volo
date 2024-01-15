#include "debug_panel.h"

ecs_comp_define_public(DebugPanelComp);

ecs_module_init(debug_panel_module) { ecs_register_comp_empty(DebugPanelComp); }
