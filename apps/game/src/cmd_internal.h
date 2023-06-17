#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

#define cmd_group_count 10

enum {
  AppOrder_CommandUpdate = 50,
};

ecs_comp_extern(CmdControllerComp);

void cmd_push_select(CmdControllerComp*, EcsEntityId object);
void cmd_push_select_group(CmdControllerComp*, u8 groupIndex);
void cmd_push_deselect(CmdControllerComp*, EcsEntityId object);
void cmd_push_deselect_all(CmdControllerComp*);
void cmd_push_move(CmdControllerComp*, EcsEntityId object, GeoVector position);
void cmd_push_stop(CmdControllerComp*, EcsEntityId object);
void cmd_push_attack(CmdControllerComp*, EcsEntityId object, EcsEntityId target);
void cmd_group_clear(CmdControllerComp*, u8 groupIndex);
void cmd_group_add(CmdControllerComp*, u8 groupIndex, EcsEntityId object);
void cmd_group_remove(CmdControllerComp*, u8 groupIndex, EcsEntityId object);
