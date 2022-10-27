#include "ai_eval.h"
#include "ai_tracer_count.h"

static void tracer_count_begin(const AiEvalContext* ctx, const AssetAiNode* nodeDef) {
  (void)ctx;
  (void)nodeDef;
}

static void
tracer_count_end(const AiEvalContext* ctx, const AssetAiNode* nodeDef, const AiResult res) {
  (void)ctx;
  (void)nodeDef;
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
