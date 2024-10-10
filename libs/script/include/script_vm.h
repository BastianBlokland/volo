#pragma once
#include "script_doc.h"
#include "script_panic.h"

// Forward declare from 'script_mem.h'.
typedef struct sScriptMem ScriptMem;

// Forward declare from 'core_binder.h'.
typedef struct sScriptBinder ScriptBinder;

#define script_vm_regs 32

/**
 * Code operation.
 *
 * Doc format:
 * - '[]' represents data part of the operation itself.
 * - '()' represents registers that are read or written by the operation.
 */
typedef enum {
  ScriptOp_Fail   = 0,   // [   ] (   ) -> ( ) Terminate the execution.
  ScriptOp_Return = 10,  // [s  ] (s  ) -> ( ) Return register 's'.
  ScriptOp_Move   = 20,  // [d,s] (s  ) -> (d) Load value at register 's' into register 'd'.
  ScriptOp_Value  = 30,  // [d,v] (   ) -> (d) Load value with index 'v' into register 'd'.
  ScriptOp_Add    = 100, // [d,s] (d,s) -> (d) Add register 's' to 'd'.
  ScriptOp_Sub    = 101, // [d,s] (d,s) -> (d) Subtract register 's' from 'd'.
  ScriptOp_Mul    = 102, // [d,s] (d,s) -> (d) Multiply register 'd' by register 's'.
  ScriptOp_Div    = 103, // [d,s] (d,s) -> (d) Divide register 'd' by register 's'.
  ScriptOp_Mod    = 104, // [d,s] (d,s) -> (d) Modulo register 'd' by register 's'.
  ScriptOp_Negate = 105, // [d  ] (d  ) -> (d) Negate register 'd'.
  ScriptOp_Invert = 106, // [d  ] (d  ) -> (d) Invert register 'd'.
} ScriptOp;

typedef struct {
  ScriptPanic panic;
  ScriptVal   val;
} ScriptVmResult;

/**
 * Evaluate the given byte-code.
 */
ScriptVmResult
script_vm_eval(const ScriptDoc*, String code, ScriptMem*, const ScriptBinder*, void* bindCtx);

/**
 * Disassemble the given byte-code.
 */
void   script_vm_disasm_write(const ScriptDoc*, String code, DynString* out);
String script_vm_disasm_scratch(const ScriptDoc*, String code);
