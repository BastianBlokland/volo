#include "ai_blackboard.h"
#include "core_stringtable.h"
#include "ecs_world.h"
#include "scene_attack.h"
#include "scene_brain.h"
#include "scene_nav.h"

// clang-format off

static StringHash g_blackboardKeyNavTarget,
                  g_blackboardKeyNavStop,
                  g_blackboardKeyAttackTarget;

// clang-format on

ecs_view_define(BrainView) {
  ecs_access_maybe_write(SceneAttackComp);
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
    if (navAgent) {
      // Start moving when the nav-target knowledge is set.
      const AiValue navTarget = ai_blackboard_get(bb, g_blackboardKeyNavTarget);
      if (navTarget.type == AiValueType_Vector) {
        if (!geo_vector_equal3(navAgent->target, navTarget.data_vector, 1e-4f)) {
          scene_nav_move_to(navAgent, navTarget.data_vector);
        } else if (!(navAgent->flags & SceneNavAgent_Traveling)) {
          ai_blackboard_set_none(bb, g_blackboardKeyNavTarget);
        }
      }

      // Stop moving when nav-stop knowledge is set.
      if (ai_value_has(ai_blackboard_get(bb, g_blackboardKeyNavStop))) {
        scene_nav_stop(navAgent);
        ai_blackboard_set_none(bb, g_blackboardKeyNavTarget);
        ai_blackboard_set_none(bb, g_blackboardKeyNavStop);
      }
    }

    // Set attack target.
    SceneAttackComp* attack = ecs_view_write_t(itr, SceneAttackComp);
    if (attack) {
      const AiValue attackTarget = ai_blackboard_get(bb, g_blackboardKeyAttackTarget);
      attack->targetEntity = attackTarget.type == AiValueType_Entity ? attackTarget.data_entity : 0;
      ai_blackboard_set_none(bb, g_blackboardKeyAttackTarget);
    }
  }
}

ecs_module_init(scene_controller_module) {
  g_blackboardKeyNavTarget    = stringtable_add(g_stringtable, string_lit("cmd-nav-target"));
  g_blackboardKeyNavStop      = stringtable_add(g_stringtable, string_lit("cmd-nav-stop"));
  g_blackboardKeyAttackTarget = stringtable_add(g_stringtable, string_lit("cmd-attack-target"));

  ecs_register_view(BrainView);

  ecs_register_system(SceneControllerUpdateSys, ecs_view_id(BrainView));
}
