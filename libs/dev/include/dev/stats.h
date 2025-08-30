#pragma once
#include "dev/forward.h"

ecs_comp_extern(DevStatsComp);
ecs_comp_extern(DevStatsGlobalComp);

typedef enum {
  DevStatShow_None,
  DevStatShow_Minimal,
  DevStatShow_Full,

  DevStatShow_Default = DevStatShow_Minimal,
} DevStatShow;

typedef enum {
  DevStatDebug_Unavailable,
  DevStatDebug_Off,
  DevStatDebug_On,
} DevStatDebug;

/**
 * Notify the user about a statistics change.
 */
void dev_stats_notify(DevStatsGlobalComp*, String key, String Value);

DevStatShow dev_stats_show(const DevStatsComp*);
void        dev_stats_show_set(DevStatsComp*, DevStatShow);

DevStatDebug dev_stats_debug(const DevStatsComp*);
void         dev_stats_debug_set(DevStatsComp*, DevStatDebug);
void         dev_stats_debug_set_available(DevStatsComp*);
