#include "ecs/utils.h"
#include "ecs/world.h"
#include "gap/error.h"

ecs_comp_define(GapErrorComp);

static void ecs_combine_gap_error(void* dataA, void* dataB) {
  GapErrorComp* compA = dataA;
  GapErrorComp* compB = dataB;
  if (compB->type <= compA->type) {
    /**
     * Higher priority error.
     */
    compA->type = compB->type;
  }
}

ecs_module_init(gap_error_module) {
  ecs_register_comp(GapErrorComp, .combinator = ecs_combine_gap_error);
}

String gap_error_str(const GapErrorType type) {
  static const String g_errorMsgs[GapErrorType_Count] = {
      [GapErrorType_PlatformInitFailed] = string_static("Platform initialization failed"),
  };
  return g_errorMsgs[type];
}

bool gap_error_check(EcsWorld* world) {
  return ecs_world_has_t(world, ecs_world_global(world), GapErrorComp);
}

void gap_error_clear(EcsWorld* world) {
  ecs_utils_maybe_remove_t(world, ecs_world_global(world), GapErrorComp);
}

void gap_error_report(EcsWorld* world, const GapErrorType type) {
  ecs_world_add_t(world, ecs_world_global(world), GapErrorComp, .type = type);
}
