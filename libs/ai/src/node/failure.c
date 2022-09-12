#include "ai_eval.h"

AiResult ai_node_failure_eval(const AssetBehavior* behavior, AiBlackboard* bb) {
  (void)behavior;
  (void)bb;
  return AiResult_Failure;
}
