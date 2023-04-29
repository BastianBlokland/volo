#include "ecs_world.h"
#include "scene_blink.h"
#include "scene_lifetime.h"
#include "scene_prefab.h"
#include "scene_renderable.h"
#include "scene_tag.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_vfx.h"

ecs_comp_define_public(SceneBlinkComp);

ecs_view_define(BlinkGlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(BlinkView) {
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_maybe_write(SceneVfxDecalComp);
  ecs_access_maybe_write(SceneVfxSystemComp);
  ecs_access_with(SceneRenderableComp);
  ecs_access_write(SceneBlinkComp);
  ecs_access_write(SceneTagComp);
}

ecs_system_define(SceneBlinkSys) {
  EcsView*     globalView = ecs_world_view_t(world, BlinkGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time    = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32            timeSec = scene_time_seconds(time);

  EcsView* blinkView = ecs_world_view_t(world, BlinkView);
  for (EcsIterator* itr = ecs_view_itr(blinkView); ecs_view_walk(itr);) {
    const EcsEntityId         entity    = ecs_view_entity(itr);
    SceneBlinkComp*           blink     = ecs_view_write_t(itr, SceneBlinkComp);
    SceneTagComp*             tagComp   = ecs_view_write_t(itr, SceneTagComp);
    SceneVfxSystemComp*       vfxSys    = ecs_view_write_t(itr, SceneVfxSystemComp);
    SceneVfxDecalComp*        vfxDecal  = ecs_view_write_t(itr, SceneVfxDecalComp);
    const SceneTransformComp* transComp = ecs_view_read_t(itr, SceneTransformComp);

    const bool newState = (u32)(timeSec * blink->frequency) % 2;
    if (newState) {
      tagComp->tags |= SceneTags_Emit;
    } else {
      tagComp->tags &= ~SceneTags_Emit;
    }
    if (vfxSys) {
      vfxSys->alpha = newState ? 1.0f : 0.0f;
    }
    if (vfxDecal) {
      vfxDecal->alpha = newState ? 1.0f : 0.0f;
    }
    if (newState && !blink->state && blink->effectPrefab) {
      const EcsEntityId effectEntity = scene_prefab_spawn(
          world,
          &(ScenePrefabSpec){
              .prefabId = blink->effectPrefab,
              .faction  = SceneFaction_None,
              .position = transComp ? transComp->position : geo_vector(0),
              .rotation = transComp ? transComp->rotation : geo_quat_ident});

      ecs_world_add_t(world, effectEntity, SceneLifetimeOwnerComp, .owner = entity);
    }
    blink->state = newState;
  }
}

ecs_module_init(scene_blink_module) {
  ecs_register_comp(SceneBlinkComp);

  ecs_register_system(
      SceneBlinkSys, ecs_register_view(BlinkGlobalView), ecs_register_view(BlinkView));
}
