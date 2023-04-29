#include "core_diag.h"
#include "core_math.h"
#include "ecs_world.h"
#include "scene_lifetime.h"
#include "scene_renderable.h"

ecs_comp_define_public(SceneRenderableComp);
ecs_comp_define_public(SceneRenderableFadeoutComp);

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

ecs_module_init(scene_renderable_module) {
  ecs_register_comp(SceneRenderableComp);
  ecs_register_comp(SceneRenderableFadeoutComp);

  ecs_register_system(SceneRenderableFadeoutSys, ecs_register_view(FadeoutView));
}
