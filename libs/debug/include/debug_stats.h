#pragma once
#include "debug.h"

ecs_comp_extern(DebugStatsComp);
ecs_comp_extern(DebugStatsGlobalComp);

typedef enum {
  DebugStatShow_None,
  DebugStatShow_Minimal,
  DebugStatShow_Full,
} DebugStatShow;

/**
 * Notify the user about a statistics change.
 */
void debug_stats_notify(DebugStatsGlobalComp*, String key, String Value);

DebugStatShow debug_stats_show(const DebugStatsComp*);
void          debug_stats_show_set(DebugStatsComp*, DebugStatShow);
