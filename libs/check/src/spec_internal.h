#pragma once
#include "check_spec.h"

#include <setjmp.h>

#include "def_internal.h"
#include "result.h"

struct sCheckSpecContext {
  CheckTestContext* (*visitBlock)(CheckSpecContext*, CheckBlock);
};

struct sCheckTestContext {
  bool         started;
  CheckResult* result;
  jmp_buf      finishJumpDest; // Spec blocks can longjmp here to early out, arg: CheckBlockResult.
};

typedef struct {
  const CheckSpecDef* def;
  DynArray            blocks; // CheckBlock[].
  bool                focus;  // Indicates that one or more tests has focus.
} CheckSpec;

CheckSpec    check_spec_create(Allocator*, const CheckSpecDef*);
void         check_spec_destroy(CheckSpec*);
CheckResult* check_exec_block(Allocator*, const CheckSpec*, CheckBlockId);
