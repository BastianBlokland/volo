#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_stringtable.h"
#include "ecs_entity.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_faction.h"
#include "scene_product.h"
#include "scene_property.h"
#include "scene_set.h"
#include "scene_transform.h"
#include "script_val.h"

#include "cmd_internal.h"

static const SceneFaction g_playerFaction = SceneFaction_A;
static StringHash         g_knowledgeKeyMoveTarget, g_knowledgeKeyStop, g_knowledgeKeyAttackTarget;

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

ecs_comp_define(CmdControllerComp) {
  DynArray  commands; // Cmd[];
  CmdGroup* groups;   // CmdGroup[cmd_group_count];
};

static void ecs_destruct_controller(void* data) {
  CmdControllerComp* comp = data;
  dynarray_destroy(&comp->commands);

  for (u32 groupIdx = 0; groupIdx != cmd_group_count; ++groupIdx) {
    cmd_group_destroy(&comp->groups[groupIdx]);
  }
  alloc_free_array_t(g_allocHeap, comp->groups, cmd_group_count);
}

ecs_view_define(GlobalUpdateView) {
  ecs_access_maybe_write(CmdControllerComp);
  ecs_access_write(SceneSetEnvComp);
}

ecs_view_define(UnitView) {
  ecs_access_read(SceneFactionComp);
  ecs_access_write(SceneKnowledgeComp);
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
    SceneKnowledgeComp* knowledge = ecs_view_write_t(unitItr, SceneKnowledgeComp);
    scene_knowledge_store(knowledge, g_knowledgeKeyMoveTarget, script_vec3(cmdMove->position));
    scene_knowledge_store(knowledge, g_knowledgeKeyAttackTarget, script_null());
    scene_knowledge_store(knowledge, g_knowledgeKeyStop, script_null());
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
    SceneKnowledgeComp* knowledge = ecs_view_write_t(unitItr, SceneKnowledgeComp);

    scene_knowledge_store(knowledge, g_knowledgeKeyStop, script_bool(true));
    scene_knowledge_store(knowledge, g_knowledgeKeyMoveTarget, script_null());
    scene_knowledge_store(knowledge, g_knowledgeKeyAttackTarget, script_null());
  }
}

static void cmd_execute_attack(EcsWorld* world, const CmdAttack* cmdAttack) {
  EcsIterator* unitItr = ecs_view_maybe_at(ecs_world_view_t(world, UnitView), cmdAttack->object);
  if (unitItr && cmd_is_player_owned(unitItr)) {
    SceneKnowledgeComp* knowledge = ecs_view_write_t(unitItr, SceneKnowledgeComp);

    scene_knowledge_store(knowledge, g_knowledgeKeyAttackTarget, script_entity(cmdAttack->target));
    scene_knowledge_store(knowledge, g_knowledgeKeyMoveTarget, script_null());
    scene_knowledge_store(knowledge, g_knowledgeKeyStop, script_null());
  }
}

static void cmd_execute(
    EcsWorld* world, const CmdControllerComp* controller, SceneSetEnvComp* setEnv, const Cmd* cmd) {
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
    dynarray_for_t(&controller->groups[cmd->selectGroup.groupIndex].entities, EcsEntityId, entity) {
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

ecs_system_define(CmdControllerUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SceneSetEnvComp*   setEnv     = ecs_view_write_t(globalItr, SceneSetEnvComp);
  CmdControllerComp* controller = ecs_view_write_t(globalItr, CmdControllerComp);
  if (!controller) {
    controller           = ecs_world_add_t(world, ecs_world_global(world), CmdControllerComp);
    controller->commands = dynarray_create_t(g_allocHeap, Cmd, 512);
    controller->groups   = alloc_array_t(g_allocHeap, CmdGroup, cmd_group_count);
    for (u32 groupIdx = 0; groupIdx != cmd_group_count; ++groupIdx) {
      cmd_group_init(&controller->groups[groupIdx]);
    }
  }

  // Update all groups.
  for (u32 groupIdx = 0; groupIdx != cmd_group_count; ++groupIdx) {
    cmd_group_prune_destroyed_entities(&controller->groups[groupIdx], world);
    cmd_group_update_position(&controller->groups[groupIdx], world);
  }

  // Execute all commands.
  dynarray_for_t(&controller->commands, Cmd, cmd) { cmd_execute(world, controller, setEnv, cmd); }
  dynarray_clear(&controller->commands);
}

ecs_module_init(game_cmd_module) {
  g_knowledgeKeyMoveTarget   = stringtable_add(g_stringtable, string_lit("cmdMoveTarget"));
  g_knowledgeKeyStop         = stringtable_add(g_stringtable, string_lit("cmdStop"));
  g_knowledgeKeyAttackTarget = stringtable_add(g_stringtable, string_lit("cmdAttackTarget"));

  ecs_register_comp(CmdControllerComp, .destructor = ecs_destruct_controller);

  ecs_register_view(GlobalUpdateView);
  ecs_register_view(UnitView);
  ecs_register_view(ProdView);
  ecs_register_view(TransformView);

  ecs_register_system(
      CmdControllerUpdateSys,
      ecs_view_id(GlobalUpdateView),
      ecs_view_id(UnitView),
      ecs_view_id(ProdView),
      ecs_view_id(TransformView));

  ecs_order(CmdControllerUpdateSys, AppOrder_CommandUpdate);
}

void cmd_push_select(CmdControllerComp* controller, const EcsEntityId object, const bool mainObj) {
  diag_assert(ecs_entity_valid(object));

  *dynarray_push_t(&controller->commands, Cmd) = (Cmd){
      .type   = Cmd_Select,
      .select = {.object = object, .mainObject = mainObj},
  };
}

void cmd_push_select_group(CmdControllerComp* controller, const u8 groupIndex) {
  diag_assert(groupIndex < cmd_group_count);

  *dynarray_push_t(&controller->commands, Cmd) = (Cmd){
      .type        = Cmd_SelectGroup,
      .selectGroup = {.groupIndex = groupIndex},
  };
}

void cmd_push_deselect(CmdControllerComp* controller, const EcsEntityId object) {
  diag_assert(ecs_entity_valid(object));

  *dynarray_push_t(&controller->commands, Cmd) = (Cmd){
      .type     = Cmd_Deselect,
      .deselect = {.object = object},
  };
}

void cmd_push_deselect_all(CmdControllerComp* controller) {
  *dynarray_push_t(&controller->commands, Cmd) = (Cmd){
      .type = Cmd_DeselectAll,
  };
}

void cmd_push_move(
    CmdControllerComp* controller, const EcsEntityId object, const GeoVector position) {
  diag_assert(ecs_entity_valid(object));

  *dynarray_push_t(&controller->commands, Cmd) = (Cmd){
      .type = Cmd_Move,
      .move = {.object = object, .position = position},
  };
}

void cmd_push_stop(CmdControllerComp* controller, const EcsEntityId object) {
  diag_assert(ecs_entity_valid(object));

  *dynarray_push_t(&controller->commands, Cmd) = (Cmd){
      .type = Cmd_Stop,
      .stop = {.object = object},
  };
}

void cmd_push_attack(
    CmdControllerComp* controller, const EcsEntityId object, const EcsEntityId target) {
  diag_assert(ecs_entity_valid(object));
  diag_assert(ecs_entity_valid(target));

  *dynarray_push_t(&controller->commands, Cmd) = (Cmd){
      .type   = Cmd_Attack,
      .attack = {.object = object, .target = target},
  };
}

void cmd_group_clear(CmdControllerComp* controller, const u8 groupIndex) {
  diag_assert(groupIndex < cmd_group_count);

  dynarray_clear(&controller->groups[groupIndex].entities);
}

void cmd_group_add(CmdControllerComp* controller, const u8 groupIndex, const EcsEntityId object) {
  diag_assert(groupIndex < cmd_group_count);
  diag_assert(ecs_entity_valid(object));

  cmd_group_add_internal(&controller->groups[groupIndex], object);
}

void cmd_group_remove(
    CmdControllerComp* controller, const u8 groupIndex, const EcsEntityId object) {
  diag_assert(groupIndex < cmd_group_count);
  diag_assert(ecs_entity_valid(object));

  cmd_group_remove_internal(&controller->groups[groupIndex], object);
}

u32 cmd_group_size(const CmdControllerComp* controller, const u8 groupIndex) {
  diag_assert(groupIndex < cmd_group_count);

  return cmd_group_size_internal(&controller->groups[groupIndex]);
}

GeoVector cmd_group_position(const CmdControllerComp* controller, const u8 groupIndex) {
  diag_assert(groupIndex < cmd_group_count);

  return controller->groups[groupIndex].position;
}

const EcsEntityId* cmd_group_begin(const CmdControllerComp* controller, const u8 groupIndex) {
  diag_assert(groupIndex < cmd_group_count);

  return cmd_group_begin_internal(&controller->groups[groupIndex]);
}

const EcsEntityId* cmd_group_end(const CmdControllerComp* controller, const u8 groupIndex) {
  diag_assert(groupIndex < cmd_group_count);

  return cmd_group_end_internal(&controller->groups[groupIndex]);
}
