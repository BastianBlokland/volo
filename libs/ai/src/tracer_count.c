#include "ai_tracer_count.h"

static void tracer_count_begin(AiTracer* tracer, const AssetBehavior* behavior) {
  (void)tracer;
  (void)behavior;
}

static void tracer_count_end(AiTracer* tracer, const AssetBehavior* behavior, const AiResult res) {
  (void)tracer;
  (void)behavior;
  (void)res;
  ++((AiTracerCount*)tracer)->count;
}

AiTracerCount ai_tracer_count() {
  return (AiTracerCount){
      .api =
          {
              .begin = &tracer_count_begin,
              .end   = &tracer_count_end,
          },
  };
}
