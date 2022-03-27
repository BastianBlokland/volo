#pragma once
#include "ecs_module.h"

ecs_comp_extern(DebugStatsComp);

bool debug_stats_show(const DebugStatsComp*);
void debug_stats_show_set(DebugStatsComp*, bool);
