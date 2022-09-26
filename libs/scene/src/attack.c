#include "core_diag.h"
#include "ecs_world.h"
#include "scene_attack.h"
#include "scene_collision.h"
#include "scene_lifetime.h"
#include "scene_renderable.h"
#include "scene_time.h"
#include "scene_transform.h"

ecs_comp_define_public(SceneAttackComp);

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(AttackEntityView) {
  ecs_access_read(SceneTransformComp);
  ecs_access_write(SceneAttackComp);
}

ecs_view_define(TargetEntityView) {
  ecs_access_read(SceneCollisionComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_maybe_read(SceneScaleComp);
}

static GeoVector attack_target_position(EcsIterator* targetItr) {
  const SceneCollisionComp* collision    = ecs_view_read_t(targetItr, SceneCollisionComp);
  const SceneTransformComp* trans        = ecs_view_read_t(targetItr, SceneTransformComp);
  const SceneScaleComp*     scale        = ecs_view_read_t(targetItr, SceneScaleComp);
  const GeoBox              targetBounds = scene_collision_world_bounds(collision, trans, scale);
  return geo_box_center(&targetBounds);
}

static void attack_projectile_spawn(
    EcsWorld*         world,
    const EcsEntityId graphic,
    const GeoVector   sourcePos,
    const GeoVector   targetPos) {
  diag_assert_msg(graphic, "Projectile graphic missing");

  const EcsEntityId e        = ecs_world_entity_create(world);
  const GeoVector   dir      = geo_vector_norm(geo_vector_sub(targetPos, sourcePos));
  const GeoQuat     rotation = geo_quat_look(dir, geo_up);

  ecs_world_add_t(world, e, SceneRenderableComp, .graphic = graphic);
  ecs_world_add_t(world, e, SceneTransformComp, .position = sourcePos, .rotation = rotation);
  ecs_world_add_t(world, e, SceneLifetimeDurationComp, .duration = time_seconds(5));
}

static void attack_execute(
    EcsWorld* world, EcsIterator* itr, EcsIterator* targetItr, const SceneTimeComp* time) {
  SceneAttackComp* attack = ecs_view_write_t(itr, SceneAttackComp);
  attack->lastAttackTime  = time->time;

  const SceneTransformComp* trans     = ecs_view_read_t(itr, SceneTransformComp);
  const GeoVector           sourcePos = geo_vector_add(trans->position, geo_vector(0, 1.5f, 0));
  const GeoVector           targetPos = attack_target_position(targetItr);

  attack_projectile_spawn(world, attack->projectileGraphic, sourcePos, targetPos);
}

ecs_system_define(SceneAttackSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time      = ecs_view_read_t(globalItr, SceneTimeComp);
  EcsIterator*         targetItr = ecs_view_itr(ecs_world_view_t(world, TargetEntityView));

  EcsView* attackView = ecs_world_view_t(world, AttackEntityView);
  for (EcsIterator* itr = ecs_view_itr(attackView); ecs_view_walk(itr);) {
    SceneAttackComp* attack = ecs_view_write_t(itr, SceneAttackComp);
    if (!ecs_view_maybe_jump(targetItr, attack->attackTarget)) {
      continue;
    }
    const TimeDuration timeSinceAttack = time->time - attack->lastAttackTime;
    if (timeSinceAttack > attack->attackInterval) {
      attack_execute(world, itr, targetItr, time);
    }
  }
}

ecs_module_init(scene_attack_module) {
  ecs_register_comp(SceneAttackComp);

  ecs_register_view(GlobalView);
  ecs_register_view(AttackEntityView);
  ecs_register_view(TargetEntityView);

  ecs_register_system(
      SceneAttackSys,
      ecs_view_id(GlobalView),
      ecs_view_id(AttackEntityView),
      ecs_view_id(TargetEntityView));
}
