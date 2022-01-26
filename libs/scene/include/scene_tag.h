#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  SceneTags_None = 0,
  SceneTags_Cam0 = 1 << 0,
  SceneTags_Cam1 = 1 << 1,
  SceneTags_Cam2 = 1 << 2,
  SceneTags_Cam3 = 1 << 3,

  SceneTags_CamAny  = SceneTags_Cam0 | SceneTags_Cam1 | SceneTags_Cam2 | SceneTags_Cam3,
  SceneTags_Default = SceneTags_CamAny,
} SceneTags;

typedef struct {
  SceneTags required, illegal;
} SceneTagFilter;

ecs_comp_extern_public(SceneTagComp) { SceneTags tags; };

void scene_tag_add(EcsWorld* world, EcsEntityId, SceneTags);

bool scene_tag_filter(SceneTagFilter filter, SceneTags);
