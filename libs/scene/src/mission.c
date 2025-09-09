#include "core/alloc.h"
#include "core/dynarray.h"
#include "ecs/module.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "scene/mission.h"
#include "scene/time.h"

ecs_comp_define(SceneMissionComp) {
  DynArray objectives; // SceneMissionObjective[].
};

static void ecs_destruct_mission(void* data) {
  SceneMissionComp* comp = data;
  dynarray_destroy(&comp->objectives);
}

static SceneMissionComp* scene_mission_init(EcsWorld* world) {
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      SceneMissionComp,
      .objectives = dynarray_create_t(g_allocHeap, SceneMissionObjective, 32));
}

ecs_view_define(UpdateGlobalView) {
  ecs_access_maybe_write(SceneMissionComp);
  ecs_access_read(SceneTimeComp);
}

ecs_system_define(SceneMissionUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not ready.
  }
  SceneMissionComp* mission = ecs_view_write_t(globalItr, SceneMissionComp);
  if (UNLIKELY(!mission)) {
    mission = scene_mission_init(world);
  }
  const SceneTimeComp* time = ecs_view_read_t(globalItr, SceneTimeComp);

  (void)mission;
  (void)time;
}

ecs_module_init(scene_mission_module) {
  ecs_register_comp(SceneMissionComp, .destructor = ecs_destruct_mission);

  ecs_register_view(UpdateGlobalView);

  ecs_register_system(SceneMissionUpdateSys, ecs_view_id(UpdateGlobalView));
}
