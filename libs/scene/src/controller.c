#include "core_stringtable.h"
#include "ecs_world.h"
#include "scene_attack.h"
#include "scene_brain.h"
#include "scene_nav.h"
#include "scene_target.h"

// clang-format off

static StringHash g_brainKeyNavTarget,
                  g_brainKeyNavStop,
                  g_brainKeyTargetOverride,
                  g_brainKeyAttackTarget;

// clang-format on

ecs_view_define(BrainView) {
  ecs_access_maybe_write(SceneAttackComp);
  ecs_access_maybe_write(SceneNavAgentComp);
  ecs_access_maybe_write(SceneTargetFinderComp);
  ecs_access_write(SceneBrainComp);
}

ecs_system_define(SceneControllerUpdateSys) {
  EcsView* view = ecs_world_view_t(world, BrainView);
  for (EcsIterator* itr = ecs_view_itr(view); ecs_view_walk(itr);) {
    SceneBrainComp* brain = ecs_view_write_t(itr, SceneBrainComp);
    if (scene_brain_flags(brain) & SceneBrainFlags_PauseControllers) {
      continue;
    }

    SceneNavAgentComp* navAgent = ecs_view_write_t(itr, SceneNavAgentComp);
    if (navAgent) {
      // Start moving when the nav-target value is set.
      const ScriptVal navTarget = scene_brain_get(brain, g_brainKeyNavTarget);
      if (script_val_has(navTarget)) {
        const GeoVector navTargetPos = script_get_vector3(navTarget, geo_vector(0));
        if (!geo_vector_equal3(navAgent->target, navTargetPos, 1e-4f)) {
          scene_nav_move_to(navAgent, navTargetPos);
        } else if (!(navAgent->flags & SceneNavAgent_Traveling)) {
          scene_brain_set_null(brain, g_brainKeyNavTarget);
        }
      }

      // Stop moving when nav-stop value is set.
      if (script_val_has(scene_brain_get(brain, g_brainKeyNavStop))) {
        scene_nav_stop(navAgent);
        scene_brain_set_null(brain, g_brainKeyNavTarget);
        scene_brain_set_null(brain, g_brainKeyNavStop);
      }
    }

    // Set target override.
    SceneTargetFinderComp* target = ecs_view_write_t(itr, SceneTargetFinderComp);
    if (target) {
      const ScriptVal targetOverride = scene_brain_get(brain, g_brainKeyTargetOverride);
      target->targetOverride         = script_get_entity(targetOverride, 0);
    }

    // Set attack target.
    SceneAttackComp* attack = ecs_view_write_t(itr, SceneAttackComp);
    if (attack) {
      const ScriptVal attackTarget = scene_brain_get(brain, g_brainKeyAttackTarget);
      attack->targetEntity         = script_get_entity(attackTarget, 0);
      scene_brain_set_null(brain, g_brainKeyAttackTarget);
    }
  }
}

ecs_module_init(scene_controller_module) {
  g_brainKeyNavTarget      = stringtable_add(g_stringtable, string_lit("cmd_nav_target"));
  g_brainKeyNavStop        = stringtable_add(g_stringtable, string_lit("cmd_nav_stop"));
  g_brainKeyTargetOverride = stringtable_add(g_stringtable, string_lit("cmd_target_override"));
  g_brainKeyAttackTarget   = stringtable_add(g_stringtable, string_lit("cmd_attack_target"));

  ecs_register_view(BrainView);

  ecs_register_system(SceneControllerUpdateSys, ecs_view_id(BrainView));
}
