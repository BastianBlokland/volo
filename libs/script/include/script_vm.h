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
 *
 * Operation data:
 * - op-code:        1 byte(s).
 * - register-id:    1 byte(s).
 * - register-count: 1 byte(s).
 * - extern-func:    2 byte(s).
 * - value-id:       1 byte(s).
 * - memory-key:     4 byte(s).
 *
 * NOTE: Multi-byte operation data is encoded in little-endian.
 * NOTE: There is no alignment requirement for operation data.
 */
typedef enum {
  ScriptOp_Fail     = 0, // [       ] (   ) -> ( ) Terminate the execution.
  ScriptOp_Return   = 1, // [s      ] (s  ) -> ( ) Return register 's'.
  ScriptOp_Move     = 2, // [d,s    ] (s  ) -> (d) Load value at register 's' into register 'd'.
  ScriptOp_Value    = 3, // [d,v    ] (   ) -> (d) Load value with index 'v' into register 'd'.
  ScriptOp_MemLoad  = 4, // [d,k    ] (   ) -> (d) Load from memory at key 'k' into register 'd'.
  ScriptOp_MemStore = 5, // [s,k    ] (s  ) -> ( ) Store to memory at key 'k' from register 's'.
  ScriptOp_Extern   = 6, // [d,f,r,c] (r:c) -> (d) Invoke extern func 'f' using count 'c' regs
                         //                        starting from 'r' and store result in reg 'd'.
  ScriptOp_Add    = 7,   // [d,s    ] (d,s) -> (d) Add register 's' to 'd'.
  ScriptOp_Sub    = 8,   // [d,s    ] (d,s) -> (d) Subtract register 's' from 'd'.
  ScriptOp_Mul    = 9,   // [d,s    ] (d,s) -> (d) Multiply register 'd' by register 's'.
  ScriptOp_Div    = 10,  // [d,s    ] (d,s) -> (d) Divide register 'd' by register 's'.
  ScriptOp_Mod    = 11,  // [d,s    ] (d,s) -> (d) Modulo register 'd' by register 's'.
  ScriptOp_Negate = 12,  // [d      ] (d  ) -> (d) Negate register 'd'.
  ScriptOp_Invert = 13,  // [d      ] (d  ) -> (d) Invert register 'd'.
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
