#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum eSceneTags {
  SceneTags_None         = 0,
  SceneTags_Unit         = 1 << 0,
  SceneTags_Selected     = 1 << 1,
  SceneTags_Emit         = 1 << 2,
  SceneTags_Damaged      = 1 << 3,
  SceneTags_Geometry     = 1 << 4,
  SceneTags_Transparent  = 1 << 5,
  SceneTags_Terrain      = 1 << 6,
  SceneTags_Vfx          = 1 << 7,
  SceneTags_Debug        = 1 << 8,
  SceneTags_Light        = 1 << 9,
  SceneTags_ShadowCaster = 1 << 10,

  SceneTags_Count   = 11,
  SceneTags_Default = SceneTags_Geometry | SceneTags_ShadowCaster | SceneTags_Emit,
} SceneTags;

typedef struct sSceneTagFilter {
  SceneTags required, illegal;
} SceneTagFilter;

ecs_comp_extern_public(SceneTagComp) { SceneTags tags; };

/**
 * Lookup the name of the given tag.
 * Pre-condition: Only a single bit is set.
 */
String scene_tag_name(SceneTags);

void scene_tag_add(EcsWorld* world, EcsEntityId, SceneTags);

bool scene_tag_filter(SceneTagFilter filter, SceneTags);
