#pragma once
#include "dev.h"

ecs_comp_extern(DevStatsComp);
ecs_comp_extern(DevStatsGlobalComp);

typedef enum {
  DevStatShow_None,
  DevStatShow_Minimal,
  DevStatShow_Full,
} DevStatShow;

/**
 * Notify the user about a statistics change.
 */
void dev_stats_notify(DevStatsGlobalComp*, String key, String Value);

DevStatShow dev_stats_show(const DevStatsComp*);
void        dev_stats_show_set(DevStatsComp*, DevStatShow);
