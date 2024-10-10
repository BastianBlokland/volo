#pragma once
#include "script_doc.h"
#include "script_panic.h"

// Forward declare from 'script_mem.h'.
typedef struct sScriptMem ScriptMem;

// Forward declare from 'core_binder.h'.
typedef struct sScriptBinder ScriptBinder;

#define script_vm_regs 8

/**
 * Code operation.
 *
 * Doc format:
 * - '[]' represents data part of the operation itself.
 * - '()' represents registers that are read or written by the operation.
 */
typedef enum {
  ScriptOp_Fail   = 0,   // [     ] (    ) -> ( ) Terminate the execution.
  ScriptOp_Return = 10,  // [r    ] (r   ) -> ( ) Return register 'r'.
  ScriptOp_Move   = 20,  // [d,s  ] (o   ) -> (r) Load value at register 's' into register 'd'.
  ScriptOp_Value  = 30,  // [d,v  ] (    ) -> (r) Load value with index 'v' into register 'd'.
  ScriptOp_Add    = 100, // [r,a,b] (a, b) -> (r) Add register 'a' to 'b' and store in 'r'.
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
