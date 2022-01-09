#include "ecs_world.h"
#include "rend_instance.h"

#include "resource_internal.h"

ecs_comp_define_public(RendInstanceComp);
ecs_comp_define_public(RendInstanceCustomComp);

ecs_view_define(InstanceView) { ecs_access_read(RendInstanceComp); }
ecs_view_define(InstanceCustomView) { ecs_access_read(RendInstanceCustomComp); }

ecs_system_define(RendInstanceRequestResourcesSys) {
  // Request the graphic resource for RendInstanceComp's to be loaded.
  EcsView* instView = ecs_world_view_t(world, InstanceView);
  for (EcsIterator* itr = ecs_view_itr(instView); ecs_view_walk(itr);) {
    const RendInstanceComp* instComp = ecs_view_read_t(itr, RendInstanceComp);
    rend_resource_request(world, instComp->graphic);
  }

  // Request the graphic resource for RendInstanceCustomComp's to be loaded.
  EcsView* instCustomView = ecs_world_view_t(world, InstanceCustomView);
  for (EcsIterator* itr = ecs_view_itr(instCustomView); ecs_view_walk(itr);) {
    const RendInstanceCustomComp* instCustomComp = ecs_view_read_t(itr, RendInstanceCustomComp);
    rend_resource_request(world, instCustomComp->graphic);
  }
}

ecs_module_init(rend_instance_module) {
  ecs_register_comp(RendInstanceComp);
  ecs_register_comp(RendInstanceCustomComp);

  ecs_register_view(InstanceView);
  ecs_register_view(InstanceCustomView);

  ecs_register_system(
      RendInstanceRequestResourcesSys, ecs_view_id(InstanceView), ecs_view_id(InstanceCustomView));
}
