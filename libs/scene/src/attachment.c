#include "ecs_world.h"
#include "log_logger.h"
#include "scene_attachment.h"
#include "scene_register.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"
#include "scene_transform.h"

ecs_comp_define_public(SceneAttachmentComp);

ecs_view_define(UpdateView) {
  ecs_access_write(SceneAttachmentComp);
  ecs_access_write(SceneTransformComp);
}

ecs_view_define(TargetView) {
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_read(SceneRenderableComp);
  ecs_access_read(SceneSkeletonComp);
  ecs_access_read(SceneTransformComp);
}

ecs_view_define(TargetGraphicView) { ecs_access_read(SceneSkeletonTemplComp); }

ecs_system_define(SceneAttachmentSys) {
  EcsIterator* targetItr  = ecs_view_itr(ecs_world_view_t(world, TargetView));
  EcsIterator* graphicItr = ecs_view_itr(ecs_world_view_t(world, TargetGraphicView));

  EcsView* updateView = ecs_world_view_t(world, UpdateView);
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    SceneAttachmentComp* attach = ecs_view_write_t(itr, SceneAttachmentComp);
    SceneTransformComp*  trans  = ecs_view_write_t(itr, SceneTransformComp);

    if (UNLIKELY(!ecs_view_maybe_jump(targetItr, attach->target))) {
      /**
       * Target doesn't exist or is missing components.
       * TODO: Consider how to handle this case.
       */
      continue;
    }

    if (UNLIKELY(sentinel_check(attach->jointIndex))) {
      // Joint index not known yet, attempt to query it from the skeleton template by name.
      const SceneRenderableComp* tgtRenderable = ecs_view_read_t(targetItr, SceneRenderableComp);
      if (UNLIKELY(!ecs_view_maybe_jump(graphicItr, tgtRenderable->graphic))) {
        /**
         * Target's graphic is missing a skeleton-template component.
         * Either the graphic is still being loaded or it is not skinned.
         */
        continue;
      }
      const SceneSkeletonTemplComp* skelTempl = ecs_view_read_t(graphicItr, SceneSkeletonTemplComp);
      attach->jointIndex = scene_skeleton_joint_by_name(skelTempl, attach->jointName);
      if (UNLIKELY(sentinel_check(attach->jointIndex))) {
        log_e("Missing attachment joint", log_param("joint-name-hash", fmt_int(attach->jointName)));
        continue;
      }
    }

    const SceneTransformComp* tgtTrans = ecs_view_read_t(targetItr, SceneTransformComp);
    const SceneScaleComp*     tgtScale = ecs_view_read_t(targetItr, SceneScaleComp);
    const SceneSkeletonComp*  tgtSkel  = ecs_view_read_t(targetItr, SceneSkeletonComp);
    const GeoMatrix           tgtMatrix =
        scene_skeleton_joint_world(tgtTrans, tgtScale, tgtSkel, attach->jointIndex);

    const GeoVector pos = geo_matrix_to_translation(&tgtMatrix);
    const GeoVector fwd = geo_matrix_transform3(&tgtMatrix, geo_forward);
    const GeoVector up  = geo_matrix_transform3(&tgtMatrix, geo_up);

    trans->position = pos;
    trans->rotation = geo_quat_look(fwd, up);
  }
}

ecs_module_init(scene_attachment_module) {
  ecs_register_comp(SceneAttachmentComp);

  ecs_register_view(UpdateView);
  ecs_register_view(TargetView);
  ecs_register_view(TargetGraphicView);

  ecs_register_system(
      SceneAttachmentSys,
      ecs_view_id(UpdateView),
      ecs_view_id(TargetView),
      ecs_view_id(TargetGraphicView));

  ecs_order(SceneAttachmentSys, SceneOrder_AttachmentUpdate);
}

void scene_attach_to_joint(
    EcsWorld* world, const EcsEntityId entity, const EcsEntityId target, const u32 jointIndex) {
  ecs_world_add_t(
      world,
      entity,
      SceneAttachmentComp,
      .target     = target,
      .jointName  = 0,
      .jointIndex = jointIndex);
}

void scene_attach_to_joint_name(
    EcsWorld*         world,
    const EcsEntityId entity,
    const EcsEntityId target,
    const StringHash  jointName) {
  ecs_world_add_t(
      world,
      entity,
      SceneAttachmentComp,
      .target     = target,
      .jointName  = jointName,
      .jointIndex = sentinel_u32);
}
