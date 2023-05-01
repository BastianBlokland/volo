#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

enum {
  AppOrder_CommandUpdate = 50,
};

ecs_comp_extern(CmdControllerComp);

void cmd_push_select(CmdControllerComp*, EcsEntityId object);
void cmd_push_deselect(CmdControllerComp*, EcsEntityId object);
void cmd_push_deselect_all(CmdControllerComp*);
void cmd_push_move(CmdControllerComp*, EcsEntityId object, GeoVector position);
void cmd_push_stop(CmdControllerComp*, EcsEntityId object);
void cmd_push_attack(CmdControllerComp*, EcsEntityId object, EcsEntityId target);
