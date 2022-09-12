#pragma once
#include "core_string.h"

typedef enum {
  AiKnowledgeType_f64,

  AiKnowledgeType_Count,
} AiKnowledgeType;

String ai_knowledge_type_str(AiKnowledgeType);
