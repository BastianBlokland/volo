#include "core_diag.h"
#include "script_binder.h"
#include "script_vm.h"

#include "doc_internal.h"
#include "val_internal.h"

ScriptVmResult script_vm_eval(
    const ScriptDoc* doc, const Mem code, ScriptMem* m, const ScriptBinder* binder, void* bindCtx) {
  if (binder) {
    diag_assert_msg(script_binder_hash(binder) == doc->binderHash, "Incompatible binder");
  }

  (void)code;
  (void)m;
  (void)bindCtx;

  ScriptVmResult res;
  res.val   = script_null();
  res.panic = (ScriptPanic){0};

  return res;
}
