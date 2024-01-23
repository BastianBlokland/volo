#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern(SceneLevelManagerComp);

/**
 * Component to mark entities that are part of the level (will be destroyed on level unload).
 */
ecs_comp_extern_public(SceneLevelInstanceComp);

bool        scene_level_is_loading(const SceneLevelManagerComp*);
EcsEntityId scene_level_asset(const SceneLevelManagerComp*);

String scene_level_name(const SceneLevelManagerComp*);
void   scene_level_name_update(SceneLevelManagerComp*, String name);

void scene_level_load(EcsWorld*, EcsEntityId levelAsset);
void scene_level_reload(EcsWorld*);
void scene_level_unload(EcsWorld*);
void scene_level_save(EcsWorld*, EcsEntityId levelAsset);
