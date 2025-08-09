#include "ecs_world.h"
#include "rend_error.h"

ecs_comp_define_public(RendErrorComp);

static void ecs_combine_rend_error(void* dataA, void* dataB) {
  RendErrorComp* compA = dataA;
  RendErrorComp* compB = dataB;
  if (compB->type >= compA->type) {
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
      [RendErrorType_DeviceNotFound] = string_static("No compatible graphics device found"),
  };
  return g_errorMsgs[type];
}

bool rend_error(const RendErrorComp* comp) { return comp->type != RendErrorType_None; }
void rend_error_clear(RendErrorComp* comp) { comp->type = RendErrorType_None; }

void rend_error_report(EcsWorld* world, const RendErrorType type) {
  ecs_world_add_t(world, ecs_world_global(world), RendErrorComp, .type = type);
}

void rend_error_report_direct(RendErrorComp* comp, const RendErrorType type) {
  if (type >= comp->type) {
    /**
     * Higher priority error.
     */
    comp->type = type;
  }
}
