#pragma once
#include "ecs_module.h"

ecs_comp_extern(DebugStatsComp);
ecs_comp_extern(DebugStatsGlobalComp);

/**
 * Notify the user about a statistics change.
 */
void debug_stats_notify(DebugStatsGlobalComp*, String key, String Value);

bool debug_stats_show(const DebugStatsComp*);
void debug_stats_show_set(DebugStatsComp*, bool);
