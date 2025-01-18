#pragma once
#include "dev.h"

ecs_comp_extern(DevStatsComp);
ecs_comp_extern(DevStatsGlobalComp);

typedef enum {
  DebugStatShow_None,
  DebugStatShow_Minimal,
  DebugStatShow_Full,
} DebugStatShow;

/**
 * Notify the user about a statistics change.
 */
void debug_stats_notify(DevStatsGlobalComp*, String key, String Value);

DebugStatShow debug_stats_show(const DevStatsComp*);
void          debug_stats_show_set(DevStatsComp*, DebugStatShow);
