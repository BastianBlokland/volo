#include "ai_tracer_count.h"

static void tracer_count_begin(AiTracer* tracer, const AssetAiNode* nodeDef) {
  (void)tracer;
  (void)nodeDef;
}

static void tracer_count_end(AiTracer* tracer, const AssetAiNode* nodeDef, const AiResult res) {
  (void)tracer;
  (void)nodeDef;
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
