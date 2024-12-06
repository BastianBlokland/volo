#include "core_sentinel.h"
#include "core_stringtable.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "geo_matrix.h"
#include "log_logger.h"
#include "scene_footstep.h"
#include "scene_lifetime.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"
#include "scene_transform.h"
#include "scene_vfx.h"
#include "scene_visibility.h"

#define scene_footstep_lift_threshold 0.05f
#define scene_footstep_decal_lifetime time_seconds(2)
#define scene_footstep_max_per_task 75

ASSERT(scene_footstep_feet_max <= 8, "Feet state needs to be representable with 8 bits")

ecs_comp_define_public(SceneFootstepComp);

ecs_comp_define(SceneFootstepStateComp) {
  u8 jointIndices[scene_footstep_feet_max];
  u8 feetUpBits; // bool[scene_footstep_feet_max]
};

ecs_view_define(InitView) {
  ecs_access_read(SceneFootstepComp);
  ecs_access_read(SceneRenderableComp);
  ecs_access_with(SceneSkeletonComp);
  ecs_access_without(SceneFootstepStateComp);
}

ecs_view_define(UpdateView) {
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneVisibilityComp);
  ecs_access_read(SceneFootstepComp);
  ecs_access_read(SceneSkeletonComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_write(SceneFootstepStateComp);
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
    for (u32 footIdx = 0; footIdx != scene_footstep_feet_max; ++footIdx) {
      if (!footstep->jointNames[footIdx]) {
        state->jointIndices[footIdx] = sentinel_u8;
        goto JointUnused;
      }
      const u32 jointIndex = scene_skeleton_joint_by_name(skelTempl, footstep->jointNames[footIdx]);
      if (UNLIKELY(sentinel_check(jointIndex))) {
        const String name = stringtable_lookup(g_stringtable, footstep->jointNames[footIdx]);
        log_e("Footstep joint missing", log_param("name", fmt_text(name)));
        goto JointUnused;
      }
      if (jointIndex > 254) {
        log_e("Footstep joint index exceeds maximum", log_param("index", fmt_int(jointIndex)));
        goto JointUnused;
      }
      state->jointIndices[footIdx] = (u8)jointIndex;
      continue;

    JointUnused:
      state->jointIndices[footIdx] = sentinel_u8;
    }
  }
}

static void footstep_decal_spawn(
    EcsWorld*                 world,
    const SceneTransformComp* trans,
    const GeoVector           footPos,
    const EcsEntityId         decalAsset) {

  const EcsEntityId e = ecs_world_entity_create(world);
  ecs_world_add_empty_t(world, e, SceneLevelInstanceComp);
  ecs_world_add_t(world, e, SceneTransformComp, .position = footPos, .rotation = trans->rotation);
  ecs_world_add_t(world, e, SceneLifetimeDurationComp, .duration = scene_footstep_decal_lifetime);
  ecs_world_add_t(world, e, SceneVfxDecalComp, .asset = decalAsset, .alpha = 1.0f);
  ecs_world_add_t(world, e, SceneVisibilityComp); // Seeing footsteps requires visibility.
}

ecs_system_define(SceneFootstepUpdateSys) {
  u32 numFootsteps = 0;

  EcsView* updateView = ecs_world_view_t(world, UpdateView);
  for (EcsIterator* itr = ecs_view_itr_step(updateView, parCount, parIndex); ecs_view_walk(itr);) {
    const SceneFootstepComp*  footstep  = ecs_view_read_t(itr, SceneFootstepComp);
    const SceneScaleComp*     scaleComp = ecs_view_read_t(itr, SceneScaleComp);
    const SceneTransformComp* transComp = ecs_view_read_t(itr, SceneTransformComp);
    const SceneSkeletonComp*  skeleton  = ecs_view_read_t(itr, SceneSkeletonComp);
    SceneFootstepStateComp*   state     = ecs_view_write_t(itr, SceneFootstepStateComp);

    for (u32 footIdx = 0; footIdx != scene_footstep_feet_max; ++footIdx) {
      const u8 jointIndex = state->jointIndices[footIdx];
      if (sentinel_check(jointIndex)) {
        continue; // Joint unused.
      }

      const GeoMatrix* jointLocalTrans = &skeleton->jointTransforms[jointIndex];
      const bool       footLifted = jointLocalTrans->columns[3].y > scene_footstep_lift_threshold;
      const bool       footWasUp  = (state->feetUpBits & (1 << footIdx)) != 0;

      if (!footLifted && footWasUp) {
        state->feetUpBits &= ~(1 << footIdx);
        ++numFootsteps;

        const GeoMatrix localToWorld = scene_matrix_world(transComp, scaleComp);
        const GeoVector footLocalPos = geo_matrix_to_translation(jointLocalTrans);
        const GeoVector footWorldPos = geo_matrix_transform3_point(&localToWorld, footLocalPos);
        footstep_decal_spawn(world, transComp, footWorldPos, footstep->decalAssets[footIdx]);
      } else if (footLifted && !footWasUp) {
        state->feetUpBits |= 1 << footIdx;
      }
    }

    if (numFootsteps >= scene_footstep_max_per_task) {
      /**
       * Throttle the maximum amount of footsteps in a single task.
       * As long as the feet are down for enough ticks no steps will be missed.
       */
      break;
    }
  }
}

ecs_module_init(scene_footstep_module) {
  ecs_register_comp(SceneFootstepComp);
  ecs_register_comp(SceneFootstepStateComp);

  ecs_register_view(InitView);
  ecs_register_view(GraphicView);
  ecs_register_view(UpdateView);

  ecs_register_system(SceneFootstepInitSys, ecs_view_id(InitView), ecs_view_id(GraphicView));
  ecs_register_system(SceneFootstepUpdateSys, ecs_view_id(UpdateView));
  ecs_parallel(SceneFootstepUpdateSys, 2);
}
