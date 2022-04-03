#include "ecs_world.h"
#include "ui_stats.h"

ecs_comp_define_public(UiStatsComp);

ecs_module_init(ui_stats_module) { ecs_register_comp(UiStatsComp); }
