#pragma once
#include "core_string.h"

typedef enum {
  AiResult_Success,
  AiResult_Failure,
} AiResult;

String ai_result_str(AiResult);
