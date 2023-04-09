#pragma once

typedef enum {
  SndResult_Success = 0,
  SndResult_FailedToAcquireObject,
  SndResult_InvalidObject,
  SndResult_InvalidObjectPhase,
} SndResult;
