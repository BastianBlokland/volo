#include "vfx_stats.h"

ecs_comp_define_public(VfxStatsComp);

ecs_module_init(vfx_stats_module) { ecs_register_comp(VfxStatsComp); }
