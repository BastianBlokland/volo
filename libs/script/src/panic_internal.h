#pragma once
#include "script_panic.h"

#include <setjmp.h>

typedef struct sScriptPanicHandler {
  ScriptPanic result;
  jmp_buf     anchor;
} ScriptPanicHandler;
