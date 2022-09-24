#include "ai_tracer.h"

void ai_tracer_destroy(AiTracer* tracer) {
  if (tracer->destruct) {
    tracer->destruct(tracer);
  }
}
