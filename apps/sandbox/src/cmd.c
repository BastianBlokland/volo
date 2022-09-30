#include "ai_blackboard.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_stringtable.h"
#include "ecs_world.h"
#include "scene_brain.h"
#include "scene_selection.h"

#include "cmd_internal.h"
#include "object_internal.h"

static StringHash g_blackboardKeyMoveTarget;

typedef enum {
  Cmd_Select,
  Cmd_Deselect,
  Cmd_Move,
  Cmd_SpawnUnit,
  Cmd_SpawnWall,
  Cmd_Destroy,
} CmdType;

typedef struct {
  EcsEntityId object;
} CmdSelect;

typedef struct {
  EcsEntityId object;
  GeoVector   position;
} CmdMove;

typedef struct {
  GeoVector position;
} CmdSpawnUnit;

typedef struct {
  GeoVector position;
} CmdSpawnWall;

typedef struct {
  EcsEntityId object;
} CmdDestroy;

typedef struct {
  CmdType type;
  union {
    CmdSelect    select;
    CmdMove      move;
    CmdSpawnUnit spawnUnit;
    CmdSpawnWall spawnWall;
    CmdDestroy   destroy;
  };
} Cmd;

ecs_comp_define(CmdControllerComp) {
  DynArray commands; // Cmd[];
};

static void ecs_destruct_controller(void* data) {
  CmdControllerComp* comp = data;
  dynarray_destroy(&comp->commands);
}

ecs_view_define(ControllerWriteView) { ecs_access_write(CmdControllerComp); }

ecs_view_define(GlobalUpdateView) {
  ecs_access_read(ObjectDatabaseComp);
  ecs_access_write(SceneSelectionComp);
}

ecs_view_define(BrainView) { ecs_access_write(SceneBrainComp); }

static CmdControllerComp* cmd_controller_get(EcsWorld* world) {
  EcsView*     view = ecs_world_view_t(world, ControllerWriteView);
  EcsIterator* itr  = ecs_view_maybe_at(view, ecs_world_global(world));
  return itr ? ecs_view_write_t(itr, CmdControllerComp) : null;
}

static void cmd_execute_move(EcsWorld* world, const CmdMove* cmdMove) {
  EcsIterator* brainItr = ecs_view_maybe_at(ecs_world_view_t(world, BrainView), cmdMove->object);
  if (brainItr) {
    SceneBrainComp* brain = ecs_view_write_t(brainItr, SceneBrainComp);

    AiBlackboard* bb = scene_brain_blackboard_mutable(brain);
    ai_blackboard_set_vector(bb, g_blackboardKeyMoveTarget, cmdMove->position);
  }
}

static void cmd_execute(
    EcsWorld*                 world,
    const ObjectDatabaseComp* objectDb,
    SceneSelectionComp*       selection,
    const Cmd*                cmd) {
  switch (cmd->type) {
  case Cmd_Select:
    diag_assert_msg(ecs_entity_valid(cmd->select.object), "Selecting invalid entity");
    if (ecs_world_exists(world, cmd->select.object)) {
      scene_selection_add(selection, cmd->select.object);
    }
    break;
  case Cmd_Deselect:
    scene_selection_clear(selection);
    break;
  case Cmd_Move:
    cmd_execute_move(world, &cmd->move);
    break;
  case Cmd_SpawnUnit:
    object_spawn_unit(world, objectDb, cmd->spawnUnit.position);
    break;
  case Cmd_SpawnWall:
    object_spawn_wall(world, objectDb, cmd->spawnWall.position);
    break;
  case Cmd_Destroy:
    diag_assert_msg(ecs_entity_valid(cmd->destroy.object), "Destroying invalid entity");
    if (ecs_world_exists(world, cmd->select.object)) {
      scene_selection_remove(selection, cmd->destroy.object);
      ecs_world_entity_destroy(world, cmd->destroy.object);
    }
    break;
  }
}

ecs_system_define(CmdControllerUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const ObjectDatabaseComp* objectDb   = ecs_view_read_t(globalItr, ObjectDatabaseComp);
  SceneSelectionComp*       selection  = ecs_view_write_t(globalItr, SceneSelectionComp);
  CmdControllerComp*        controller = cmd_controller_get(world);
  if (!controller) {
    controller = ecs_world_add_t(
        world,
        ecs_world_global(world),
        CmdControllerComp,
        .commands = dynarray_create_t(g_alloc_heap, Cmd, 2));
  }

  dynarray_for_t(&controller->commands, Cmd, cmd) { cmd_execute(world, objectDb, selection, cmd); }
  dynarray_clear(&controller->commands);
}

ecs_module_init(sandbox_cmd_module) {
  g_blackboardKeyMoveTarget = stringtable_add(g_stringtable, string_lit("user-move-target"));

  ecs_register_comp(CmdControllerComp, .destructor = ecs_destruct_controller);

  ecs_register_view(ControllerWriteView);
  ecs_register_view(GlobalUpdateView);
  ecs_register_view(BrainView);

  ecs_register_system(
      CmdControllerUpdateSys,
      ecs_view_id(GlobalUpdateView),
      ecs_view_id(ControllerWriteView),
      ecs_view_id(BrainView));

  ecs_order(CmdControllerUpdateSys, AppOrder_CommandUpdate);
}

void cmd_push_select(CmdControllerComp* controller, const EcsEntityId object) {
  *dynarray_push_t(&controller->commands, Cmd) = (Cmd){
      .type   = Cmd_Select,
      .select = {.object = object},
  };
}

void cmd_push_deselect(CmdControllerComp* controller) {
  *dynarray_push_t(&controller->commands, Cmd) = (Cmd){
      .type = Cmd_Deselect,
  };
}

void cmd_push_move(
    CmdControllerComp* controller, const EcsEntityId object, const GeoVector position) {
  *dynarray_push_t(&controller->commands, Cmd) = (Cmd){
      .type = Cmd_Move,
      .move = {.object = object, .position = position},
  };
}

void cmd_push_spawn_unit(CmdControllerComp* controller, const GeoVector position) {
  *dynarray_push_t(&controller->commands, Cmd) = (Cmd){
      .type      = Cmd_SpawnUnit,
      .spawnUnit = {.position = position},
  };
}

void cmd_push_spawn_wall(CmdControllerComp* controller, const GeoVector position) {
  *dynarray_push_t(&controller->commands, Cmd) = (Cmd){
      .type      = Cmd_SpawnWall,
      .spawnWall = {.position = position},
  };
}

void cmd_push_destroy(CmdControllerComp* controller, const EcsEntityId object) {
  *dynarray_push_t(&controller->commands, Cmd) = (Cmd){
      .type    = Cmd_Destroy,
      .destroy = {.object = object},
  };
}
