#include "ai_tracer.h"

void ai_tracer_destroy(AiTracer* tracer) {
  if (tracer->destruct) {
    tracer->destruct(tracer);
  }
}

static void ai_tracer_count_begin(AiTracer* tracer, const AssetBehavior* behavior) {
  (void)tracer;
  (void)behavior;
}

static void
ai_tracer_count_end(AiTracer* tracer, const AssetBehavior* behavior, const AiResult result) {
  (void)tracer;
  (void)behavior;
  (void)result;
  ++((AiTracerCount*)tracer)->count;
}

AiTracerCount ai_tracer_count() {
  return (AiTracerCount){
      .api =
          {
              .begin = &ai_tracer_count_begin,
              .end   = &ai_tracer_count_end,
          },
  };
}
