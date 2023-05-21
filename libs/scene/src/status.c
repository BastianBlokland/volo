#include "core_bits.h"
#include "ecs_world.h"
#include "scene_status.h"

ASSERT(SceneStatusType_Count <= bytes_to_bits(sizeof(SceneStatusMask)), "Status mask too small");

ecs_comp_define_public(SceneStatusComp);
ecs_comp_define_public(SceneStatusRequestComp);

static void ecs_combine_status_request(void* dataA, void* dataB) {
  SceneStatusRequestComp* reqA = dataA;
  SceneStatusRequestComp* reqB = dataB;
  reqA->add |= reqB->add;
  reqA->remove |= reqB->remove;
}

ecs_view_define(StatusView) {
  ecs_access_write(SceneStatusRequestComp);
  ecs_access_write(SceneStatusComp);
}

ecs_system_define(SceneStatusUpdateSys) {
  EcsView* statusView = ecs_world_view_t(world, StatusView);
  for (EcsIterator* itr = ecs_view_itr(statusView); ecs_view_walk(itr);) {
    SceneStatusRequestComp* request = ecs_view_write_t(itr, SceneStatusRequestComp);
    SceneStatusComp*        status  = ecs_view_write_t(itr, SceneStatusComp);

    // Apply the requests.
    status->active |= request->add;
    status->active &= ~request->remove;

    // Clear the requests.
    request->add = request->remove = 0;
  }
}

ecs_module_init(scene_status_module) {
  ecs_register_comp(SceneStatusComp);
  ecs_register_comp(SceneStatusRequestComp, .combinator = ecs_combine_status_request);

  ecs_register_system(SceneStatusUpdateSys, ecs_register_view(StatusView));
}

bool scene_status_active(const SceneStatusComp* status, const SceneStatusType type) {
  return (status->active & (1 << type)) != 0;
}

void scene_status_add(EcsWorld* world, const EcsEntityId target, const SceneStatusType type) {
  ecs_world_add_t(world, target, SceneStatusRequestComp, .add = 1 << type);
}

void scene_status_remove(EcsWorld* world, const EcsEntityId target, const SceneStatusType type) {
  ecs_world_add_t(world, target, SceneStatusRequestComp, .remove = 1 << type);
}
