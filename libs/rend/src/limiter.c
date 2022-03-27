#include "core_thread.h"
#include "ecs_world.h"
#include "rend_register.h"
#include "rend_settings.h"

#include "limiter_internal.h"

ecs_comp_define_public(RendLimiterComp);

ecs_view_define(GlobalView) {
  ecs_access_read(RendGlobalSettingsComp);
  ecs_access_maybe_write(RendLimiterComp);
}

ecs_system_define(RendFrameLimiterSys) {
  EcsView*     view = ecs_world_view_t(world, GlobalView);
  EcsIterator* itr  = ecs_view_maybe_at(view, ecs_world_global(world));
  if (UNLIKELY(!itr)) {
    return;
  }
  const RendGlobalSettingsComp* globalSettings = ecs_view_read_t(itr, RendGlobalSettingsComp);
  RendLimiterComp*              limiter        = ecs_view_write_t(itr, RendLimiterComp);
  if (UNLIKELY(!limiter)) {
    limiter = ecs_world_add_t(
        world, ecs_world_global(world), RendLimiterComp, .previousTime = time_steady_clock());
  }

  if (!globalSettings->limiterFreq) {
    limiter->sleepDur = 0;
    limiter->freq     = 0;
    return; // Limiter not active.
  }
  if (globalSettings->limiterFreq != limiter->freq) {
    /**
     * Very crude way of 'syncing' up to the last presented image.
     */
    thread_sleep(time_milliseconds(50));
    limiter->freq         = globalSettings->limiterFreq;
    limiter->previousTime = time_steady_clock();
  }

  const TimeDuration targetDuration = time_second / globalSettings->limiterFreq;
  const TimeSteady   start          = time_steady_clock();
  const TimeDuration elapsed        = time_steady_duration(limiter->previousTime, start);

  limiter->sleepDur = targetDuration - elapsed;
  if (limiter->sleepDur > limiter->sleepOverhead) {
    limiter->sleepDur -= limiter->sleepOverhead;
    thread_sleep(limiter->sleepDur);

    /**
     * Keep a moving average of the additional time a 'thread_sleep()' takes to avoid always waking
     * up late.
     */
    const TimeDuration sinceStart = time_steady_duration(start, time_steady_clock());
    limiter->sleepOverhead = (sinceStart - limiter->sleepDur + limiter->sleepOverhead * 99) / 100;
  }
  limiter->previousTime = time_steady_clock();
}

ecs_module_init(rend_limiter_module) {
  ecs_register_comp(RendLimiterComp);

  ecs_register_view(GlobalView);

  ecs_register_system_with_flags(
      RendFrameLimiterSys, EcsSystemFlags_Exclusive, ecs_view_id(GlobalView));
  ecs_order(RendFrameLimiterSys, RendOrder_FrameLimiter);
}
