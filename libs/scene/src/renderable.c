#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_time.h"
#include "ecs_world.h"
#include "scene_lifetime.h"
#include "scene_renderable.h"
#include "scene_tag.h"
#include "scene_time.h"
#include "scene_vfx.h"

ecs_comp_define_public(SceneRenderableComp);
ecs_comp_define_public(SceneRenderableFadeoutComp);
ecs_comp_define_public(SceneRenderableBlinkComp);

ecs_view_define(FadeoutView) {
  ecs_access_read(SceneLifetimeDurationComp);
  ecs_access_read(SceneRenderableFadeoutComp);
  ecs_access_write(SceneRenderableComp);
}

ecs_system_define(SceneRenderableFadeoutSys) {
  EcsView* fadeoutView = ecs_world_view_t(world, FadeoutView);
  for (EcsIterator* itr = ecs_view_itr(fadeoutView); ecs_view_walk(itr);) {
    const SceneLifetimeDurationComp*  lifetime   = ecs_view_read_t(itr, SceneLifetimeDurationComp);
    const SceneRenderableFadeoutComp* fadeout    = ecs_view_read_t(itr, SceneRenderableFadeoutComp);
    SceneRenderableComp*              renderable = ecs_view_write_t(itr, SceneRenderableComp);

    diag_assert(fadeout->duration > time_microsecond);

    const f64 frac    = (f64)lifetime->duration / (f64)fadeout->duration;
    renderable->alpha = math_clamp_f32((f32)frac, 0.0f, 1.0f);
  }
}

ecs_view_define(BlinkGlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(BlinkView) {
  ecs_access_maybe_write(SceneVfxSystemComp);
  ecs_access_maybe_write(SceneVfxDecalComp);
  ecs_access_read(SceneRenderableBlinkComp);
  ecs_access_with(SceneRenderableComp);
  ecs_access_write(SceneTagComp);
}

ecs_system_define(SceneRenderableBlinkSys) {
  EcsView*     globalView = ecs_world_view_t(world, BlinkGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time    = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32            timeSec = scene_time_seconds(time);

  EcsView* blinkView = ecs_world_view_t(world, BlinkView);
  for (EcsIterator* itr = ecs_view_itr(blinkView); ecs_view_walk(itr);) {
    const SceneRenderableBlinkComp* blink    = ecs_view_read_t(itr, SceneRenderableBlinkComp);
    SceneTagComp*                   tagComp  = ecs_view_write_t(itr, SceneTagComp);
    SceneVfxSystemComp*             vfxSys   = ecs_view_write_t(itr, SceneVfxSystemComp);
    SceneVfxDecalComp*              vfxDecal = ecs_view_write_t(itr, SceneVfxDecalComp);

    const bool on = (u32)(timeSec * blink->blinkFrequency) % 2;
    if (on) {
      tagComp->tags |= SceneTags_Emit;
    } else {
      tagComp->tags &= ~SceneTags_Emit;
    }
    if (vfxSys) {
      vfxSys->alpha = on ? 1.0f : 0.0f;
    }
    if (vfxDecal) {
      vfxDecal->alpha = on ? 1.0f : 0.0f;
    }
  }
}

ecs_module_init(scene_renderable_module) {
  ecs_register_comp(SceneRenderableComp);
  ecs_register_comp(SceneRenderableFadeoutComp);
  ecs_register_comp(SceneRenderableBlinkComp);

  ecs_register_system(SceneRenderableFadeoutSys, ecs_register_view(FadeoutView));
  ecs_register_system(
      SceneRenderableBlinkSys, ecs_register_view(BlinkGlobalView), ecs_register_view(BlinkView));
}
