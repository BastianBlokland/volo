#include "core_diag.h"
#include "ecs_def.h"
#include "ecs_world.h"
#include "rend_register.h"

#include "platform_internal.h"
#include "resource_internal.h"

void rend_register(EcsDef* def) {
  ecs_register_module(def, rend_platform_module);
  ecs_register_module(def, rend_canvas_module);
  ecs_register_module(def, rend_resource_module);
}

void rend_teardown(EcsWorld* world) {
  diag_assert_msg(!ecs_world_busy(world), "Unable to teardown renderer: World still busy");

  /**
   * In the renderer many objects need to be destroyed in a very specific order.
   * This can unfortunatly not be represented with ecs-destructors as the order is not defined, to
   * work around this applications need to specifically call rend_teardown() before exit.
   */

  rend_resource_teardown(world);
  rend_platform_teardown(world);
}
