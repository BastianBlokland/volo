#include "core_diag.h"
#include "core_math.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_lifetime.h"
#include "scene_renderable.h"
#include "scene_time.h"

ecs_comp_define_public(SceneRenderableComp);
ecs_comp_define_public(SceneRenderableFadeinComp);
ecs_comp_define_public(SceneRenderableFadeoutComp);

ecs_view_define(FadeinGlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(FadeinView) {
  ecs_access_write(SceneRenderableComp);
  ecs_access_write(SceneRenderableFadeinComp);
}

ecs_system_define(SceneRenderableFadeinSys) {
  EcsView*     globalView = ecs_world_view_t(world, FadeinGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time = ecs_view_read_t(globalItr, SceneTimeComp);

  EcsView* fadeinView = ecs_world_view_t(world, FadeinView);
  for (EcsIterator* itr = ecs_view_itr(fadeinView); ecs_view_walk(itr);) {
    SceneRenderableFadeinComp* fadein     = ecs_view_write_t(itr, SceneRenderableFadeinComp);
    SceneRenderableComp*       renderable = ecs_view_write_t(itr, SceneRenderableComp);
    if (fadein->elapsed > fadein->duration) {
      renderable->color.a = 1.0f;
      ecs_world_remove_t(world, ecs_view_entity(itr), SceneRenderableFadeinComp);
      continue;
    }
    const f64 frac      = (f64)fadein->elapsed / (f64)fadein->duration;
    renderable->color.a = math_clamp_f32((f32)frac, 0.0f, 1.0f);
    fadein->elapsed += time->delta;
  }
}

ecs_view_define(FadeoutView) {
  ecs_access_read(SceneLifetimeDurationComp);
  ecs_access_read(SceneRenderableFadeoutComp);
  ecs_access_without(SceneRenderableFadeinComp);
  ecs_access_write(SceneRenderableComp);
}

ecs_system_define(SceneRenderableFadeoutSys) {
  EcsView* fadeoutView = ecs_world_view_t(world, FadeoutView);
  for (EcsIterator* itr = ecs_view_itr(fadeoutView); ecs_view_walk(itr);) {
    const SceneLifetimeDurationComp*  lifetime   = ecs_view_read_t(itr, SceneLifetimeDurationComp);
    const SceneRenderableFadeoutComp* fadeout    = ecs_view_read_t(itr, SceneRenderableFadeoutComp);
    SceneRenderableComp*              renderable = ecs_view_write_t(itr, SceneRenderableComp);

    diag_assert(fadeout->duration > time_microsecond);

    const f64 frac      = (f64)lifetime->duration / (f64)fadeout->duration;
    renderable->color.a = math_clamp_f32((f32)frac, 0.0f, 1.0f);
  }
}

ecs_module_init(scene_renderable_module) {
  ecs_register_comp(SceneRenderableComp);
  ecs_register_comp(SceneRenderableFadeinComp);
  ecs_register_comp(SceneRenderableFadeoutComp);

  ecs_register_system(SceneRenderableFadeoutSys, ecs_register_view(FadeoutView));
  ecs_register_system(
      SceneRenderableFadeinSys, ecs_register_view(FadeinView), ecs_register_view(FadeinGlobalView));
}
