#include "ai_blackboard.h"
#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"
#include "core_stringtable.h"

#include "source_internal.h"

AiResult ai_node_knowledgeset_eval(const AssetAiNode* nodeDef, AiBlackboard* bb, AiTracer* tracer) {
  diag_assert(nodeDef->type == AssetAiNode_KnowledgeSet);
  (void)tracer;

  diag_assert_msg(nodeDef->data_knowledgeset.key.size, "Knowledge key cannot be empty");

  // TODO: Keys should be pre-hashed in the behavior asset.
  const StringHash     keyHash     = stringtable_add(g_stringtable, nodeDef->data_knowledgeset.key);
  const AssetAiSource* valueSource = &nodeDef->data_knowledgeset.value;
  ai_blackboard_set(bb, keyHash, ai_source_value(valueSource, bb));

  return AiResult_Success;
}
