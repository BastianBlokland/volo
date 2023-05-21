#include "core_array.h"
#include "core_bits.h"
#include "core_bitset.h"
#include "ecs_world.h"
#include "scene_health.h"
#include "scene_status.h"
#include "scene_time.h"

static const f32 g_sceneStatusDamagePerSec[SceneStatusType_Count] = {
    [SceneStatusType_Burning] = 10,
};

ASSERT(SceneStatusType_Count <= bytes_to_bits(sizeof(SceneStatusMask)), "Status mask too small");

ecs_comp_define_public(SceneStatusComp);
ecs_comp_define_public(SceneStatusRequestComp);

static void ecs_combine_status_request(void* dataA, void* dataB) {
  SceneStatusRequestComp* reqA = dataA;
  SceneStatusRequestComp* reqB = dataB;
  reqA->add |= reqB->add;
  reqA->remove |= reqB->remove;
}

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(StatusView) {
  ecs_access_write(SceneStatusRequestComp);
  ecs_access_write(SceneStatusComp);
  ecs_access_maybe_write(SceneDamageComp);
}

ecs_system_define(SceneStatusUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time     = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32            deltaSec = scene_delta_seconds(time);

  EcsView* statusView = ecs_world_view_t(world, StatusView);
  for (EcsIterator* itr = ecs_view_itr(statusView); ecs_view_walk(itr);) {
    SceneStatusRequestComp* request = ecs_view_write_t(itr, SceneStatusRequestComp);
    SceneStatusComp*        status  = ecs_view_write_t(itr, SceneStatusComp);
    SceneDamageComp*        damage  = ecs_view_write_t(itr, SceneDamageComp);

    // Apply the requests.
    status->active |= (request->add & status->supported);
    status->active &= ~request->remove;

    // Clear the requests.
    request->add = request->remove = 0;

    // Apply damage.
    bitset_for(bitset_from_var(status->active), typeIndex) {
      if (damage) {
        damage->amount += g_sceneStatusDamagePerSec[typeIndex] * deltaSec;
      }
    }
  }
}

ecs_module_init(scene_status_module) {
  ecs_register_comp(SceneStatusComp);
  ecs_register_comp(SceneStatusRequestComp, .combinator = ecs_combine_status_request);

  ecs_register_view(GlobalView);
  ecs_register_view(StatusView);

  ecs_register_system(SceneStatusUpdateSys, ecs_view_id(GlobalView), ecs_view_id(StatusView));
}

bool scene_status_active(const SceneStatusComp* status, const SceneStatusType type) {
  return (status->active & (1 << type)) != 0;
}

String scene_status_name(const SceneStatusType type) {
  static const String g_names[] = {
      string_static("Burning"),
  };
  ASSERT(array_elems(g_names) == SceneStatusType_Count, "Incorrect number of names");
  return g_names[type];
}

void scene_status_add(EcsWorld* world, const EcsEntityId target, const SceneStatusType type) {
  ecs_world_add_t(world, target, SceneStatusRequestComp, .add = 1 << type);
}

void scene_status_remove(EcsWorld* world, const EcsEntityId target, const SceneStatusType type) {
  ecs_world_add_t(world, target, SceneStatusRequestComp, .remove = 1 << type);
}
