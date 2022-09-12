#include "ai_knowledge.h"
#include "core_array.h"
#include "core_diag.h"

String ai_knowledge_type_str(const AiKnowledgeType type) {
  static const String g_names[] = {
      string_static("f64"),
  };
  ASSERT(array_elems(g_names) == AiKnowledgeType_Count, "Incorrect number of names");
  return g_names[type];
}
