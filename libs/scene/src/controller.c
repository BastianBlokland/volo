#include "ai_blackboard.h"
#include "core_stringtable.h"
#include "ecs_world.h"
#include "scene_brain.h"
#include "scene_nav.h"

// clang-format off

static StringHash g_blackboardKeyNavTarget;

// clang-format on

ecs_view_define(BrainView) {
  ecs_access_maybe_write(SceneNavAgentComp);
  ecs_access_write(SceneBrainComp);
}

ecs_system_define(SceneControllerUpdateSys) {
  EcsView* view = ecs_world_view_t(world, BrainView);
  for (EcsIterator* itr = ecs_view_itr(view); ecs_view_walk(itr);) {
    SceneBrainComp* brain = ecs_view_write_t(itr, SceneBrainComp);
    if (scene_brain_flags(brain) & SceneBrainFlags_PauseControllers) {
      continue;
    }
    AiBlackboard* bb = scene_brain_blackboard_mutable(brain);

    SceneNavAgentComp* navAgent = ecs_view_write_t(itr, SceneNavAgentComp);
    if (navAgent && ai_blackboard_exists(bb, g_blackboardKeyNavTarget)) {
      const GeoVector target = ai_blackboard_get_vector(bb, g_blackboardKeyNavTarget);
      scene_nav_move_to(navAgent, target);
      ai_blackboard_unset(bb, g_blackboardKeyNavTarget);
    }
  }
}

ecs_module_init(scene_controller_module) {
  g_blackboardKeyNavTarget = stringtable_add(g_stringtable, string_lit("cmd-nav-target"));

  ecs_register_view(BrainView);

  ecs_register_system(SceneControllerUpdateSys, ecs_view_id(BrainView));
}
