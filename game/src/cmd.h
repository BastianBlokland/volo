#pragma once
#include "ecs/module.h"
#include "geo/vector.h"

#define game_cmd_group_count 9

enum {
  GameOrder_CommandUpdate = 730,
};

ecs_comp_extern(GameCmdComp);

void game_cmd_push_select(GameCmdComp*, EcsEntityId object, bool mainObject);
void game_cmd_push_select_group(GameCmdComp*, u8 groupIndex);
void game_cmd_push_deselect(GameCmdComp*, EcsEntityId object);
void game_cmd_push_deselect_all(GameCmdComp*);
void game_cmd_push_move(GameCmdComp*, EcsEntityId object, GeoVector position);
void game_cmd_push_stop(GameCmdComp*, EcsEntityId object);
void game_cmd_push_attack(GameCmdComp*, EcsEntityId object, EcsEntityId target);
void game_cmd_group_clear(GameCmdComp*, u8 groupIndex);
void game_cmd_group_add(GameCmdComp*, u8 groupIndex, EcsEntityId object);
void game_cmd_group_remove(GameCmdComp*, u8 groupIndex, EcsEntityId object);

u32                game_cmd_group_size(const GameCmdComp*, u8 groupIndex);
GeoVector          game_cmd_group_position(const GameCmdComp*, u8 groupIndex);
const EcsEntityId* game_cmd_group_begin(const GameCmdComp*, u8 groupIndex);
const EcsEntityId* game_cmd_group_end(const GameCmdComp*, u8 groupIndex);
