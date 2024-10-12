#pragma once
#include "script_doc.h"
#include "script_panic.h"

// Forward declare from 'script_mem.h'.
typedef struct sScriptMem ScriptMem;

// Forward declare from 'core_binder.h'.
typedef struct sScriptBinder ScriptBinder;

#define script_vm_regs 32

// clang-format off

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
 * NOTE: Multi-byte operation data is encoded as little-endian.
 * NOTE: There is no alignment requirement for operation data.
 */
typedef enum {
  ScriptOp_Fail        = 0,  // [       ] (   ) -> ( ) Terminate the execution.
  ScriptOp_Assert      = 1,  // [s      ] (s  ) -> ( ) Terminate the execution if register 's' is falsy.
  ScriptOp_Return      = 2,  // [s      ] (s  ) -> ( ) Return register 's'.
  ScriptOp_Move        = 3,  // [d,s    ] (s  ) -> (d) Load value at register 's' into register 'd'.
  ScriptOp_Value       = 4,  // [d,v    ] (   ) -> (d) Load value with index 'v' into register 'd'.
  ScriptOp_MemLoad     = 5,  // [d,k    ] (   ) -> (d) Load from memory at key 'k' into register 'd'.
  ScriptOp_MemStore    = 6,  // [s,k    ] (s  ) -> ( ) Store to memory at key 'k' from register 's'.
  ScriptOp_MemLoadDyn  = 7,  // [d      ] (d  ) -> (d) Load from memory with a key from register 'd'.
  ScriptOp_MemStoreDyn = 8,  // [s,r    ] (s,r) -> ( ) Store a value from register 's' to memory with a key from register 'r'.
  ScriptOp_Extern      = 9,  // [d,f,r,c] (r:c) -> (d) Invoke extern func 'f' using count 'c' registers starting from 'r' and store result in register 'd'.
  ScriptOp_Type        = 10, // [d      ] (d  ) -> (d) Retrieve the type for register 'd'.
  ScriptOp_Hash        = 11, // [d      ] (d  ) -> (d) Retrieve the hash for register 'd'.
  ScriptOp_Equal       = 12, // [d,s    ] (d,s) -> (d) Compare 'd' and 's' and store result in register 'd'.
  ScriptOp_Less        = 13, // [d,s    ] (d,s) -> (d) Compare 'd' and 's' and store result in register 'd'.
  ScriptOp_Greater     = 14, // [d,s    ] (d,s) -> (d) Compare 'd' and 's' and store result in register 'd'.
  ScriptOp_Add         = 15, // [d,s    ] (d,s) -> (d) Add register 's' to 'd'.
  ScriptOp_Sub         = 16, // [d,s    ] (d,s) -> (d) Subtract register 's' from 'd'.
  ScriptOp_Mul         = 17, // [d,s    ] (d,s) -> (d) Multiply register 'd' by register 's'.
  ScriptOp_Div         = 18, // [d,s    ] (d,s) -> (d) Divide register 'd' by register 's'.
  ScriptOp_Mod         = 19, // [d,s    ] (d,s) -> (d) Modulo register 'd' by register 's'.
  ScriptOp_Negate      = 20, // [d      ] (d  ) -> (d) Negate register 'd'.
  ScriptOp_Invert      = 21, // [d      ] (d  ) -> (d) Invert register 'd'.
} ScriptOp;

// clang-format on

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
