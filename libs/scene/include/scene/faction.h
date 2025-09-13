#pragma once
#include "core/sentinel.h"
#include "ecs/module.h"
#include "scene/forward.h"

typedef enum eSceneFaction {
  SceneFaction_A,
  SceneFaction_B,
  SceneFaction_C,
  SceneFaction_D,

  SceneFaction_Count,
  SceneFaction_None = sentinel_u32
} SceneFaction;

typedef enum eSceneFactionStat {
  SceneFactionStat_Kills,
  SceneFactionStat_Losses,

  SceneFactionStat_Count,
} SceneFactionStat;

ecs_comp_extern_public(SceneFactionComp) { SceneFaction id; };
ecs_comp_extern_public(SceneFactionStatsComp) {
  i32 stats[SceneFactionStat_Count][SceneFaction_Count];
};

String     scene_faction_name(SceneFaction);
SceneLayer scene_faction_layers(SceneFaction);

bool scene_is_friendly(const SceneFactionComp*, const SceneFactionComp*);
bool scene_is_hostile(const SceneFactionComp*, const SceneFactionComp*);

void                   scene_faction_stats_clear(SceneFactionStatsComp*);
SceneFactionStatsComp* scene_faction_stats_report(EcsWorld*);
void scene_faction_stats_report_single(EcsWorld*, SceneFaction, SceneFactionStat, i32 delta);
