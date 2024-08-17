#pragma once
#include "asset_level.h"
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern(SceneLevelManagerComp);

/**
 * Component to mark entities that are part of the level (will be destroyed on level unload).
 */
ecs_comp_extern_public(SceneLevelInstanceComp);

bool        scene_level_loading(const SceneLevelManagerComp*);
bool        scene_level_loaded(const SceneLevelManagerComp*);
EcsEntityId scene_level_asset(const SceneLevelManagerComp*);
u32         scene_level_counter(const SceneLevelManagerComp*);

String scene_level_name(const SceneLevelManagerComp*);
void   scene_level_name_update(SceneLevelManagerComp*, String name);

EcsEntityId scene_level_terrain(const SceneLevelManagerComp*);
void        scene_level_terrain_update(SceneLevelManagerComp*, EcsEntityId terrainAsset);

GeoVector scene_level_startpoint(const SceneLevelManagerComp*);
void      scene_level_startpoint_update(SceneLevelManagerComp*, GeoVector startpoint);

AssetLevelFog scene_level_fog(const SceneLevelManagerComp*);
void          scene_level_fog_update(SceneLevelManagerComp*, AssetLevelFog);

void scene_level_load(EcsWorld*, EcsEntityId levelAsset);
void scene_level_reload(EcsWorld*);
void scene_level_unload(EcsWorld*);
void scene_level_save(EcsWorld*, EcsEntityId levelAsset);
