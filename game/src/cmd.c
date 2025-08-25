#include "core/alloc.h"
#include "core/diag.h"
#include "core/dynarray.h"
#include "core/stringtable.h"
#include "ecs/entity.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "scene/faction.h"
#include "scene/product.h"
#include "scene/property.h"
#include "scene/set.h"
#include "scene/transform.h"
#include "script/val.h"

#include "cmd.h"

static const SceneFaction g_playerFaction = SceneFaction_A;
static StringHash         g_propMoveTarget, g_propStop, g_propAttackTarget;

typedef enum {
  Cmd_Select,
  Cmd_SelectGroup,
  Cmd_Deselect,
  Cmd_DeselectAll,
  Cmd_Move,
  Cmd_Stop,
  Cmd_Attack,
} CmdType;

typedef struct {
  EcsEntityId object;
  bool        mainObject;
} CmdSelect;

typedef struct {
  u8 groupIndex;
} CmdSelectGroup;

typedef struct {
  EcsEntityId object;
} CmdDeselect;

typedef struct {
  EcsEntityId object;
  GeoVector   position;
} CmdMove;

typedef struct {
  EcsEntityId object;
} CmdStop;

typedef struct {
  EcsEntityId object;
  EcsEntityId target;
} CmdAttack;

typedef struct {
  CmdType type;
  union {
    CmdSelect      select;
    CmdSelectGroup selectGroup;
    CmdDeselect    deselect;
    CmdMove        move;
    CmdStop        stop;
    CmdAttack      attack;
  };
} Cmd;

typedef struct {
  GeoVector position;
  DynArray  entities; // EcsEntityId[], sorted.
} CmdGroup;

static void cmd_group_init(CmdGroup* group) {
  group->entities = dynarray_create_t(g_allocHeap, EcsEntityId, 64);
}

static void cmd_group_destroy(CmdGroup* group) { dynarray_destroy(&group->entities); }

ecs_comp_define(GameCmdComp) {
  DynArray  commands; // Cmd[];
  CmdGroup* groups;   // CmdGroup[game_cmd_group_count];
};

static void ecs_destruct_cmd_comp(void* data) {
  GameCmdComp* comp = data;
  dynarray_destroy(&comp->commands);

  for (u32 groupIdx = 0; groupIdx != game_cmd_group_count; ++groupIdx) {
    cmd_group_destroy(&comp->groups[groupIdx]);
  }
  alloc_free_array_t(g_allocHeap, comp->groups, game_cmd_group_count);
}

ecs_view_define(GlobalUpdateView) {
  ecs_access_maybe_write(GameCmdComp);
  ecs_access_write(SceneSetEnvComp);
}

ecs_view_define(UnitView) {
  ecs_access_read(SceneFactionComp);
  ecs_access_write(ScenePropertyComp);
}

ecs_view_define(ProdView) {
  ecs_access_read(SceneFactionComp);
  ecs_access_write(SceneProductionComp);
}

ecs_view_define(TransformView) { ecs_access_read(SceneTransformComp); }

static void cmd_group_add_internal(CmdGroup* group, const EcsEntityId object) {
  DynArray* entities = &group->entities;
  *(EcsEntityId*)dynarray_find_or_insert_sorted(entities, ecs_compare_entity, &object) = object;
}

static void cmd_group_remove_internal(CmdGroup* group, const EcsEntityId object) {
  DynArray*    entities = &group->entities;
  EcsEntityId* entry    = dynarray_search_binary(entities, ecs_compare_entity, &object);
  if (entry) {
    const usize index = entry - dynarray_begin_t(entities, EcsEntityId);
    dynarray_remove(entities, index, 1);
  }
}

static u32 cmd_group_size_internal(const CmdGroup* group) {
  return (u32)dynarray_size(&group->entities);
}

static const EcsEntityId* cmd_group_begin_internal(const CmdGroup* group) {
  return dynarray_begin_t(&group->entities, EcsEntityId);
}

static const EcsEntityId* cmd_group_end_internal(const CmdGroup* group) {
  return dynarray_end_t(&group->entities, EcsEntityId);
}

static void cmd_group_prune_destroyed_entities(CmdGroup* group, EcsWorld* world) {
  DynArray* entities = &group->entities;
  for (usize i = dynarray_size(entities); i-- != 0;) {
    if (!ecs_world_exists(world, *dynarray_at_t(entities, i, EcsEntityId))) {
      dynarray_remove(entities, i, 1);
    }
  }
}

static void cmd_group_update_position(CmdGroup* group, EcsWorld* world) {
  EcsView*     transformView = ecs_world_view_t(world, TransformView);
  EcsIterator* transformItr  = ecs_view_itr(transformView);
  DynArray*    entities      = &group->entities;

  u32 count = 0;
  dynarray_for_t(entities, EcsEntityId, object) {
    if (ecs_view_maybe_jump(transformItr, *object)) {
      const GeoVector pos = ecs_view_read_t(transformItr, SceneTransformComp)->position;
      group->position     = count ? geo_vector_add(group->position, pos) : pos;
      ++count;
    }
  }
  group->position = count ? geo_vector_div(group->position, count) : geo_vector(0);
}

static bool cmd_is_player_owned(EcsIterator* itr) {
  return ecs_view_read_t(itr, SceneFactionComp)->id == g_playerFaction;
}

static void
cmd_execute_move(EcsWorld* world, const SceneSetEnvComp* setEnv, const CmdMove* cmdMove) {
  EcsIterator* unitItr = ecs_view_maybe_at(ecs_world_view_t(world, UnitView), cmdMove->object);
  if (unitItr && cmd_is_player_owned(unitItr)) {
    ScenePropertyComp* propComp = ecs_view_write_t(unitItr, ScenePropertyComp);
    scene_prop_store(propComp, g_propMoveTarget, script_vec3(cmdMove->position));
    scene_prop_store(propComp, g_propAttackTarget, script_null());
    scene_prop_store(propComp, g_propStop, script_null());
    return;
  }

  if (cmdMove->object == scene_set_main(setEnv, g_sceneSetSelected)) {
    EcsIterator* prodItr = ecs_view_maybe_at(ecs_world_view_t(world, ProdView), cmdMove->object);
    if (prodItr && cmd_is_player_owned(prodItr)) {
      SceneProductionComp* prod = ecs_view_write_t(prodItr, SceneProductionComp);
      scene_product_rallypos_set_world(prod, cmdMove->position);
      return;
    }
  }
}

static void cmd_execute_stop(EcsWorld* world, const CmdStop* cmdStop) {
  EcsIterator* unitItr = ecs_view_maybe_at(ecs_world_view_t(world, UnitView), cmdStop->object);
  if (unitItr && cmd_is_player_owned(unitItr)) {
    ScenePropertyComp* propCOmp = ecs_view_write_t(unitItr, ScenePropertyComp);
    scene_prop_store(propCOmp, g_propStop, script_bool(true));
    scene_prop_store(propCOmp, g_propMoveTarget, script_null());
    scene_prop_store(propCOmp, g_propAttackTarget, script_null());
  }
}

static void cmd_execute_attack(EcsWorld* world, const CmdAttack* cmdAttack) {
  EcsIterator* unitItr = ecs_view_maybe_at(ecs_world_view_t(world, UnitView), cmdAttack->object);
  if (unitItr && cmd_is_player_owned(unitItr)) {
    ScenePropertyComp* propComp = ecs_view_write_t(unitItr, ScenePropertyComp);
    scene_prop_store(propComp, g_propAttackTarget, script_entity(cmdAttack->target));
    scene_prop_store(propComp, g_propMoveTarget, script_null());
    scene_prop_store(propComp, g_propStop, script_null());
  }
}

static void
cmd_execute(EcsWorld* world, const GameCmdComp* comp, SceneSetEnvComp* setEnv, const Cmd* cmd) {
  switch (cmd->type) {
  case Cmd_Select:
    if (ecs_world_exists(world, cmd->select.object)) {
      SceneSetFlags setFlags = SceneSetFlags_None;
      if (cmd->select.mainObject) {
        setFlags |= SceneSetFlags_MakeMain;
      }
      scene_set_add(setEnv, g_sceneSetSelected, cmd->select.object, setFlags);
    }
    break;
  case Cmd_SelectGroup:
    scene_set_clear(setEnv, g_sceneSetSelected);
    dynarray_for_t(&comp->groups[cmd->selectGroup.groupIndex].entities, EcsEntityId, entity) {
      scene_set_add(setEnv, g_sceneSetSelected, *entity, SceneSetFlags_None);
    }
    break;
  case Cmd_Deselect:
    scene_set_remove(setEnv, g_sceneSetSelected, cmd->deselect.object);
    break;
  case Cmd_DeselectAll:
    scene_set_clear(setEnv, g_sceneSetSelected);
    break;
  case Cmd_Move:
    cmd_execute_move(world, setEnv, &cmd->move);
    break;
  case Cmd_Stop:
    cmd_execute_stop(world, &cmd->stop);
    break;
  case Cmd_Attack:
    cmd_execute_attack(world, &cmd->attack);
    break;
  }
}

ecs_system_define(GameCmdUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SceneSetEnvComp* setEnv = ecs_view_write_t(globalItr, SceneSetEnvComp);
  GameCmdComp*     comp   = ecs_view_write_t(globalItr, GameCmdComp);
  if (!comp) {
    comp           = ecs_world_add_t(world, ecs_world_global(world), GameCmdComp);
    comp->commands = dynarray_create_t(g_allocHeap, Cmd, 512);
    comp->groups   = alloc_array_t(g_allocHeap, CmdGroup, game_cmd_group_count);
    for (u32 groupIdx = 0; groupIdx != game_cmd_group_count; ++groupIdx) {
      cmd_group_init(&comp->groups[groupIdx]);
    }
  }

  // Update all groups.
  for (u32 groupIdx = 0; groupIdx != game_cmd_group_count; ++groupIdx) {
    cmd_group_prune_destroyed_entities(&comp->groups[groupIdx], world);
    cmd_group_update_position(&comp->groups[groupIdx], world);
  }

  // Execute all commands.
  dynarray_for_t(&comp->commands, Cmd, cmd) { cmd_execute(world, comp, setEnv, cmd); }
  dynarray_clear(&comp->commands);
}

ecs_module_init(game_cmd_module) {
  g_propMoveTarget   = stringtable_add(g_stringtable, string_lit("cmdMoveTarget"));
  g_propStop         = stringtable_add(g_stringtable, string_lit("cmdStop"));
  g_propAttackTarget = stringtable_add(g_stringtable, string_lit("cmdAttackTarget"));

  ecs_register_comp(GameCmdComp, .destructor = ecs_destruct_cmd_comp);

  ecs_register_view(GlobalUpdateView);
  ecs_register_view(UnitView);
  ecs_register_view(ProdView);
  ecs_register_view(TransformView);

  ecs_register_system(
      GameCmdUpdateSys,
      ecs_view_id(GlobalUpdateView),
      ecs_view_id(UnitView),
      ecs_view_id(ProdView),
      ecs_view_id(TransformView));

  ecs_order(GameCmdUpdateSys, GameOrder_CommandUpdate);
}

void game_cmd_push_select(GameCmdComp* comp, const EcsEntityId object, const bool mainObj) {
  diag_assert(ecs_entity_valid(object));

  *dynarray_push_t(&comp->commands, Cmd) = (Cmd){
      .type   = Cmd_Select,
      .select = {.object = object, .mainObject = mainObj},
  };
}

void game_cmd_push_select_group(GameCmdComp* comp, const u8 groupIndex) {
  diag_assert(groupIndex < game_cmd_group_count);

  *dynarray_push_t(&comp->commands, Cmd) = (Cmd){
      .type        = Cmd_SelectGroup,
      .selectGroup = {.groupIndex = groupIndex},
  };
}

void game_cmd_push_deselect(GameCmdComp* comp, const EcsEntityId object) {
  diag_assert(ecs_entity_valid(object));

  *dynarray_push_t(&comp->commands, Cmd) = (Cmd){
      .type     = Cmd_Deselect,
      .deselect = {.object = object},
  };
}

void game_cmd_push_deselect_all(GameCmdComp* comp) {
  *dynarray_push_t(&comp->commands, Cmd) = (Cmd){
      .type = Cmd_DeselectAll,
  };
}

void game_cmd_push_move(GameCmdComp* comp, const EcsEntityId object, const GeoVector position) {
  diag_assert(ecs_entity_valid(object));

  *dynarray_push_t(&comp->commands, Cmd) = (Cmd){
      .type = Cmd_Move,
      .move = {.object = object, .position = position},
  };
}

void game_cmd_push_stop(GameCmdComp* comp, const EcsEntityId object) {
  diag_assert(ecs_entity_valid(object));

  *dynarray_push_t(&comp->commands, Cmd) = (Cmd){
      .type = Cmd_Stop,
      .stop = {.object = object},
  };
}

void game_cmd_push_attack(GameCmdComp* comp, const EcsEntityId object, const EcsEntityId target) {
  diag_assert(ecs_entity_valid(object));
  diag_assert(ecs_entity_valid(target));

  *dynarray_push_t(&comp->commands, Cmd) = (Cmd){
      .type   = Cmd_Attack,
      .attack = {.object = object, .target = target},
  };
}

void game_cmd_group_clear(GameCmdComp* comp, const u8 groupIndex) {
  diag_assert(groupIndex < game_cmd_group_count);

  dynarray_clear(&comp->groups[groupIndex].entities);
}

void game_cmd_group_add(GameCmdComp* comp, const u8 groupIndex, const EcsEntityId object) {
  diag_assert(groupIndex < game_cmd_group_count);
  diag_assert(ecs_entity_valid(object));

  cmd_group_add_internal(&comp->groups[groupIndex], object);
}

void game_cmd_group_remove(GameCmdComp* comp, const u8 groupIndex, const EcsEntityId object) {
  diag_assert(groupIndex < game_cmd_group_count);
  diag_assert(ecs_entity_valid(object));

  cmd_group_remove_internal(&comp->groups[groupIndex], object);
}

u32 game_cmd_group_size(const GameCmdComp* comp, const u8 groupIndex) {
  diag_assert(groupIndex < game_cmd_group_count);

  return cmd_group_size_internal(&comp->groups[groupIndex]);
}

GeoVector game_cmd_group_position(const GameCmdComp* comp, const u8 groupIndex) {
  diag_assert(groupIndex < game_cmd_group_count);

  return comp->groups[groupIndex].position;
}

const EcsEntityId* game_cmd_group_begin(const GameCmdComp* comp, const u8 groupIndex) {
  diag_assert(groupIndex < game_cmd_group_count);

  return cmd_group_begin_internal(&comp->groups[groupIndex]);
}

const EcsEntityId* game_cmd_group_end(const GameCmdComp* comp, const u8 groupIndex) {
  diag_assert(groupIndex < game_cmd_group_count);

  return cmd_group_end_internal(&comp->groups[groupIndex]);
}
