#include "ai_blackboard.h"
#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"
#include "core_stringtable.h"

#include "source_internal.h"

AiResult ai_node_knowledgeset_eval(const AiEvalContext* ctx, const AssetAiNodeId nodeId) {
  const AssetAiNode* def = &ctx->nodeDefs[nodeId];
  diag_assert(def->type == AssetAiNode_KnowledgeSet);

  const StringHash     key         = def->data_knowledgeset.key;
  const AssetAiSource* valueSource = &def->data_knowledgeset.value;
  ai_blackboard_set(ctx->memory, key, ai_source_value(valueSource, ctx->memory));

  return AiResult_Success;
}
