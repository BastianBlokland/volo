#include "core_sentinel.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_footstep.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"

typedef enum {
  SceneFootstepPhase_Up,
  SceneFootstepPhase_Down,
} SceneFootstepPhase;

ecs_comp_define_public(SceneFootstepComp);

ecs_comp_define(SceneFootstepStateComp) {
  u8                 jointIndices[scene_footstep_joint_max];
  SceneFootstepPhase phases[scene_footstep_joint_max];
};

ecs_view_define(InitView) {
  ecs_access_read(SceneFootstepComp);
  ecs_access_read(SceneRenderableComp);
  ecs_access_without(SceneFootstepStateComp);
}

ecs_view_define(GraphicView) { ecs_access_read(SceneSkeletonTemplComp); }

ecs_system_define(SceneFootstepInitSys) {
  EcsIterator* graphicItr = ecs_view_itr(ecs_world_view_t(world, GraphicView));

  EcsView* loadView = ecs_world_view_t(world, InitView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId          entity     = ecs_view_entity(itr);
    const SceneFootstepComp*   footstep   = ecs_view_read_t(itr, SceneFootstepComp);
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);

    if (UNLIKELY(!ecs_view_maybe_jump(graphicItr, renderable->graphic))) {
      /**
       * Target's graphic is missing a skeleton-template component.
       * Either the graphic is still being loaded or it is not skinned.
       */
      continue;
    }
    const SceneSkeletonTemplComp* skelTempl = ecs_view_read_t(graphicItr, SceneSkeletonTemplComp);

    SceneFootstepStateComp* state = ecs_world_add_t(world, entity, SceneFootstepStateComp);
    for (u32 i = 0; i != scene_footstep_joint_max; ++i) {
      const u32 jointIndex = scene_skeleton_joint_by_name(skelTempl, footstep->jointNames[i]);
      if (UNLIKELY(sentinel_check(jointIndex))) {
        log_e("Footstep joint missing", log_param("namehash", fmt_int(footstep->jointNames[i])));
        continue;
      }
      if (jointIndex > u8_max) {
        log_e("Footstep joint index exceeds maximum", log_param("index", fmt_int(jointIndex)));
        continue;
      }
      state->jointIndices[i] = (u8)jointIndex;
    }
  }
}

ecs_module_init(scene_footstep_module) {
  ecs_register_comp(SceneFootstepComp);
  ecs_register_comp(SceneFootstepStateComp);

  ecs_register_view(InitView);
  ecs_register_view(GraphicView);

  ecs_register_system(SceneFootstepInitSys, ecs_view_id(InitView), ecs_view_id(GraphicView));
}
