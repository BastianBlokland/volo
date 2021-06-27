#pragma once
#include "check_def.h"

/**
 * Test run result code.
 */
typedef enum {
  CheckResultType_Pass = 0,
  CheckResultType_Fail = 1,
} CheckResultType;

/**
 * TODO: Document
 */
CheckResultType check_run(CheckDef*);
