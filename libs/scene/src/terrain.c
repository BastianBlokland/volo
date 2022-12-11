#include "scene_renderable.h"

ecs_comp_define(SceneTerrainComp) { f32 size; };

ecs_module_init(scene_terrain_module) { ecs_register_comp(SceneTerrainComp); }
