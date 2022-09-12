#include "ai_blackboard.h"
#include "core_alloc.h"

struct sAiBlackboard {
  Allocator* alloc;
};

AiBlackboard* ai_blackboard_create(Allocator* alloc) {
  AiBlackboard* bb = alloc_alloc_t(alloc, AiBlackboard);

  *bb = (AiBlackboard){
      .alloc = alloc,
  };
  return bb;
}

void ai_blackboard_destroy(AiBlackboard* bb) { alloc_free_t(bb->alloc, bb); }
