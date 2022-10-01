#include "ai_blackboard.h"
#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"
#include "core_stringtable.h"
#include "core_time.h"

static GeoVector node_src_vec(const AssetKnowledgeSourceVector* src) {
  return geo_vector(src->x, src->y, src->z, src->w);
}

static TimeDuration node_src_time(const AssetKnowledgeSourceTime* src, AiBlackboard* bb) {
  // TODO: Keys should be pre-hashed.
  const StringHash   timeNowHash = string_hash_lit("global-time");
  const TimeDuration now         = ai_blackboard_get_time(bb, timeNowHash);
  const TimeDuration offset      = (TimeDuration)time_seconds(src->secondsFromNow);
  return now + offset;
}

AiResult
ai_node_knowledgeset_eval(const AssetBehavior* behavior, AiBlackboard* bb, AiTracer* tracer) {
  diag_assert(behavior->type == AssetBehavior_KnowledgeSet);
  (void)tracer;

  diag_assert_msg(behavior->data_knowledgeset.key.size, "Knowledge key cannot be empty");

  // TODO: Keys should be pre-hashed in the behavior asset.
  const StringHash keyHash = stringtable_add(g_stringtable, behavior->data_knowledgeset.key);
  const AssetKnowledgeSource* valueSource = &behavior->data_knowledgeset.value;

  switch (valueSource->type) {
  case AssetKnowledgeSource_Number: {
    ai_blackboard_set_f64(bb, keyHash, valueSource->data_number.value);
    return AiResult_Success;
  }
  case AssetKnowledgeSource_Bool: {
    ai_blackboard_set_bool(bb, keyHash, valueSource->data_bool.value);
    return AiResult_Success;
  }
  case AssetKnowledgeSource_Vector: {
    ai_blackboard_set_vector(bb, keyHash, node_src_vec(&valueSource->data_vector));
    return AiResult_Success;
  }
  case AssetKnowledgeSource_Time: {
    ai_blackboard_set_time(bb, keyHash, node_src_time(&valueSource->data_time, bb));
    return AiResult_Success;
  }
  case AssetKnowledgeSource_Knowledge: {
    // TODO: Keys should be pre-hashed in the behavior asset.
    const StringHash srcKeyHash = string_hash(valueSource->data_knowledge.key);
    ai_blackboard_copy(bb, srcKeyHash, keyHash);
    return AiResult_Success;
  }
  }
  UNREACHABLE
}
