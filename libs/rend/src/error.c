#include "ecs/utils.h"
#include "ecs/world.h"
#include "rend/error.h"

ecs_comp_define(RendErrorComp);

static void ecs_combine_rend_error(void* dataA, void* dataB) {
  RendErrorComp* compA = dataA;
  RendErrorComp* compB = dataB;
  if (compB->type <= compA->type) {
    /**
     * Higher priority error.
     */
    compA->type = compB->type;
  }
}

ecs_module_init(rend_error_module) {
  ecs_register_comp(RendErrorComp, .combinator = ecs_combine_rend_error);
}

String rend_error_str(const RendErrorType type) {
  static const String g_errorMsgs[RendErrorType_Count] = {
      [RendErrorType_VulkanNotFound] = string_static("No compatible Vulkan library found"),
      [RendErrorType_DeviceNotFound] = string_static("No compatible graphics device found"),
  };
  return g_errorMsgs[type];
}

bool rend_error_check(EcsWorld* world) {
  return ecs_world_has_t(world, ecs_world_global(world), RendErrorComp);
}

void rend_error_clear(EcsWorld* world) {
  ecs_utils_maybe_remove_t(world, ecs_world_global(world), RendErrorComp);
}

void rend_error_report(EcsWorld* world, const RendErrorType type) {
  ecs_world_add_t(world, ecs_world_global(world), RendErrorComp, .type = type);
}
