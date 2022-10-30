#include "ai_eval.h"
#include "ai_tracer_count.h"

static void tracer_count_begin(const AiEvalContext* ctx, const AssetAiNodeId nodeId) {
  (void)ctx;
  (void)nodeId;
}

static void
tracer_count_end(const AiEvalContext* ctx, const AssetAiNodeId nodeId, const AiResult res) {
  (void)ctx;
  (void)nodeId;
  (void)res;
  ++((AiTracerCount*)ctx->tracer)->count;
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
