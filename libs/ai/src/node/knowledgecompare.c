#include "ai_blackboard.h"
#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"

#include "knowledge_source_internal.h"

AiResult
ai_node_knowledgecompare_eval(const AssetBehavior* behavior, AiBlackboard* bb, AiTracer* tracer) {
  diag_assert(behavior->type == AssetBehavior_KnowledgeCompare);
  (void)tracer;

  // TODO: Keys should be pre-hashed in the behavior asset.
  const StringHash keyHash = string_hash(behavior->data_knowledgecompare.key);
  const AiValue    value   = ai_blackboard_get(bb, keyHash);
  const AiValue compValue  = ai_knowledge_source_value(&behavior->data_knowledgecompare.value, bb);

  bool result;
  switch (behavior->data_knowledgecompare.comparison) {
  case AssetAiComparison_Equal:
    result = ai_value_equal(value, compValue);
    break;
  case AssetAiComparison_Less:
    result = ai_value_less(value, compValue);
    break;
  case AssetAiComparison_Greater:
    result = ai_value_greater(value, compValue);
    break;
  }
  return result ? AiResult_Success : AiResult_Failure;
}
