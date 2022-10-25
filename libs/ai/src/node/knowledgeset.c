#include "ai_blackboard.h"
#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"
#include "core_stringtable.h"

#include "knowledge_source_internal.h"

AiResult
ai_node_knowledgeset_eval(const AssetBehavior* behavior, AiBlackboard* bb, AiTracer* tracer) {
  diag_assert(behavior->type == AssetBehavior_KnowledgeSet);
  (void)tracer;

  diag_assert_msg(behavior->data_knowledgeset.key.size, "Knowledge key cannot be empty");

  // TODO: Keys should be pre-hashed in the behavior asset.
  const StringHash     keyHash = stringtable_add(g_stringtable, behavior->data_knowledgeset.key);
  const AssetAiSource* valueSource = &behavior->data_knowledgeset.value;
  ai_blackboard_set(bb, keyHash, ai_knowledge_source_value(valueSource, bb));

  return AiResult_Success;
}
