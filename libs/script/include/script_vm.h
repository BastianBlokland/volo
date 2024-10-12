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
  ScriptOp_Fail              = 0,  // [       ] (       ) -> ( ) Terminate the execution.
  ScriptOp_Assert            = 1,  // [s      ] (s      ) -> ( ) Terminate the execution if register 's' is falsy.
  ScriptOp_Return            = 2,  // [s      ] (s      ) -> ( ) Return register 's'.
  ScriptOp_Move              = 3,  // [d,s    ] (s      ) -> (d) Load value at register 's' into register 'd'.
  ScriptOp_Value             = 4,  // [d,v    ] (       ) -> (d) Load value with index 'v' into register 'd'.
  ScriptOp_MemLoad           = 5,  // [d,k    ] (       ) -> (d) Load from memory at key 'k' into register 'd'.
  ScriptOp_MemStore          = 6,  // [s,k    ] (s      ) -> ( ) Store to memory at key 'k' from register 's'.
  ScriptOp_MemLoadDyn        = 7,  // [d      ] (d      ) -> (d) Load from memory with a key from register 'd'.
  ScriptOp_MemStoreDyn       = 8,  // [s,r    ] (s,r    ) -> ( ) Store a value from register 's' to memory with a key from register 'r'.
  ScriptOp_Extern            = 9,  // [d,f,r,c] (r:c    ) -> (d) Invoke extern func 'f' using count 'c' registers starting from 'r' and store result in register 'd'.
  ScriptOp_Type              = 10, // [d      ] (d      ) -> (d) Retrieve the type for register 'd'.
  ScriptOp_Hash              = 11, // [d      ] (d      ) -> (d) Retrieve the hash for register 'd'.
  ScriptOp_Equal             = 12, // [d,s    ] (d,s    ) -> (d) Compare 'd' and 's' and store result in register 'd'.
  ScriptOp_Less              = 13, // [d,s    ] (d,s    ) -> (d) Compare 'd' and 's' and store result in register 'd'.
  ScriptOp_Greater           = 14, // [d,s    ] (d,s    ) -> (d) Compare 'd' and 's' and store result in register 'd'.
  ScriptOp_Add               = 15, // [d,s    ] (d,s    ) -> (d) Add register 's' to 'd'.
  ScriptOp_Sub               = 16, // [d,s    ] (d,s    ) -> (d) Subtract register 's' from 'd'.
  ScriptOp_Mul               = 17, // [d,s    ] (d,s    ) -> (d) Multiply register 'd' by register 's'.
  ScriptOp_Div               = 18, // [d,s    ] (d,s    ) -> (d) Divide register 'd' by register 's'.
  ScriptOp_Mod               = 19, // [d,s    ] (d,s    ) -> (d) Modulo register 'd' by register 's'.
  ScriptOp_Negate            = 20, // [d      ] (d      ) -> (d) Negate register 'd'.
  ScriptOp_Invert            = 21, // [d      ] (d      ) -> (d) Invert register 'd'.
  ScriptOp_Distance          = 22, // [d,s    ] (d,s    ) -> (d) Compute the distance between 'd' and 's' and store result in register 'd'.
  ScriptOp_Angle             = 23, // [d,s    ] (d,s    ) -> (d) Compare the angle between 'd' and 's' and store result in register 'd'.
  ScriptOp_Sin               = 24, // [d      ] (d      ) -> (d) Evaluate the sine wave at 'd'.
  ScriptOp_Cos               = 25, // [d      ] (d      ) -> (d) Evaluate the cosine wave at 'd'.
  ScriptOp_Normalize         = 26, // [d      ] (d      ) -> (d) Normalize register 'd'.
  ScriptOp_Magnitude         = 27, // [d      ] (d      ) -> (d) Compute the magnitude of register 'd'.
  ScriptOp_Absolute          = 28, // [d      ] (d      ) -> (d) Normalize register 'd'.
  ScriptOp_VecX              = 29, // [d      ] (d      ) -> (d) Retrieve the x component of a vector in register 'd'.
  ScriptOp_VecY              = 30, // [d      ] (d      ) -> (d) Retrieve the y component of a vector in register 'd'.
  ScriptOp_VecZ              = 31, // [d      ] (d      ) -> (d) Retrieve the z component of a vector in register 'd'.
  ScriptOp_Vec3Compose       = 32, // [x,y,z  ] (x,y,z  ) -> (x) Compose a vector from 'x', 'y', 'z' and store in register 'x'.
  ScriptOp_QuatFromEuler     = 33, // [x,y,z  ] (x,y,z  ) -> (x) Compose a quaternion from 'x', 'y', 'z' angles and store in register 'x'.
  ScriptOp_QuatFromAngleAxis = 34, // [x,y    ] (x,y    ) -> (x) Compose a quaternion from angle 'x' and axis 'y' and store in register 'x'.
  ScriptOp_ColorCompose      = 35, // [x,y,z,w] (x,y,z,w) -> (x) Compose a color from 'x', 'y', 'z', 'w' and store in register 'x'.
  ScriptOp_ColorComposeHsv   = 36, // [x,y,z,w] (x,y,z,w) -> (x) Compose a hsv color from 'x', 'y', 'z', 'w' and store in register 'x'.
  ScriptOp_ColorFor          = 37, // [d      ] (d      ) -> (d) Compute a color for register 'd'.
  ScriptOp_Random            = 38, // [d      ] (       ) -> (d) Compute a random value (0 - 1) in register 'd'.
  ScriptOp_RandomSphere      = 39, // [d      ] (       ) -> (d) Compute a random vector on a unit sphere in register 'd'.
  ScriptOp_RandomCircleXZ    = 40, // [d      ] (       ) -> (d) Compute a random vector on a unit circle in register 'd'.
  ScriptOp_RandomBetween     = 41, // [x,y    ] (x,y    ) -> (x) Compute a random value between 'x' and 'y' and store in register 'x'.
  ScriptOp_RoundDown         = 42, // [d      ] (d      ) -> (d) Round register 'd' down.
  ScriptOp_RoundNearest      = 43, // [d      ] (d      ) -> (d) Round register 'd' to nearest.
  ScriptOp_RoundUp           = 44, // [d      ] (d      ) -> (d) Round register 'd' up.
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
