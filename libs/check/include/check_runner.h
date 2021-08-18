#pragma once
#include "check_def.h"

typedef enum {
  CheckResultType_Pass = 0,
  CheckResultType_Fail = 1,
} CheckResultType;

typedef enum {
  CheckRunFlags_None               = 0,
  CheckRunFlags_OutputPassingTests = 1 << 0,
} CheckRunFlags;

/**
 * Run the given TestSuite definition.
 */
CheckResultType check_run(CheckDef*, CheckRunFlags);
