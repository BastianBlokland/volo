#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

enum {
  AppOrder_CommandUpdate = 50,
};

ecs_comp_extern(CmdControllerComp);

void cmd_push_select(CmdControllerComp*, EcsEntityId object);
void cmd_push_deselect(CmdControllerComp*);
void cmd_push_move(CmdControllerComp*, EcsEntityId object, GeoVector position);
void cmd_push_spawn_unit(CmdControllerComp*, GeoVector position);
void cmd_push_spawn_wall(CmdControllerComp*, GeoVector position);
void cmd_push_destroy(CmdControllerComp*, EcsEntityId object);
