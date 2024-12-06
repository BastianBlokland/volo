#pragma once
#include "core_string.h"

typedef enum eSndResult {
  SndResult_Success,
  SndResult_FailedToAcquireObject,
  SndResult_InvalidObject,
  SndResult_InvalidObjectPhase,
  SndResult_ObjectStopped,
  SndResult_ParameterOutOfRange,

  SndResult_Count,
} SndResult;

String snd_result_str(SndResult);
