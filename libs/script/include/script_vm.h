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
 * - instruction:    2 byte(s).
 * - register-id:    1 byte(s).
 * - register-count: 1 byte(s).
 * - extern-func:    2 byte(s).
 * - value-id:       1 byte(s).
 * - memory-key:     4 byte(s).
 *
 * NOTE: Multi-byte operation data is encoded as little-endian.
 * NOTE: There is no alignment requirement for operation data.
 * NOTE: Instruction values are 2 byte offsets from the start of the code memory.
 */
typedef enum {
  ScriptOp_Fail              = 0,  // [       ] (       ) -> ( ) Terminate the execution.
  ScriptOp_Assert            = 1,  // [s      ] (s      ) -> ( ) Terminate the execution if register 's' is falsy.
  ScriptOp_Return            = 2,  // [s      ] (s      ) -> ( ) Return register 's'.
  ScriptOp_ReturnNull        = 3,  // [       ] (       ) -> ( ) Return value null.
  ScriptOp_Move              = 4,  // [d,s    ] (s      ) -> (d) Load value at register 's' into register 'd'.
  ScriptOp_Jump              = 5,  // [i      ] (       ) -> ( ) Jump to instruction 'i'.
  ScriptOp_JumpIfTruthy      = 6,  // [r,i    ] (r      ) -> ( ) Jump to instruction 'i' if register 'r' is truthy.
  ScriptOp_JumpIfFalsy       = 7,  // [r,i    ] (r      ) -> ( ) Jump to instruction 'i' if register 'r' is falsy.
  ScriptOp_JumpIfNonNull     = 8,  // [r,i    ] (r      ) -> ( ) Jump to instruction 'i' if register 'r' is not null.
  ScriptOp_Value             = 9,  // [d,v    ] (       ) -> (d) Load value with index 'v' into register 'd'.
  ScriptOp_MemLoad           = 10, // [d,k    ] (       ) -> (d) Load from memory at key 'k' into register 'd'.
  ScriptOp_MemStore          = 11, // [s,k    ] (s      ) -> ( ) Store to memory at key 'k' from register 's'.
  ScriptOp_MemLoadDyn        = 12, // [d      ] (d      ) -> (d) Load from memory with a key from register 'd'.
  ScriptOp_MemStoreDyn       = 13, // [s,r    ] (s,r    ) -> ( ) Store a value from register 's' to memory with a key from register 'r'.
  ScriptOp_Extern            = 14, // [d,f,r,c] (r:c    ) -> (d) Invoke extern func 'f' using count 'c' registers starting from 'r' and store result in register 'd'.
  ScriptOp_Null              = 15, // [d      ] (       ) -> (d) Load null value into register 'd'.
  ScriptOp_Truthy            = 16, // [d      ] (d      ) -> (d) Check if register 'd' is truthy.
  ScriptOp_Falsy             = 17, // [d      ] (d      ) -> (d) Check if register 'd' is falsy.
  ScriptOp_Type              = 18, // [d      ] (d      ) -> (d) Retrieve the type for register 'd'.
  ScriptOp_Hash              = 19, // [d      ] (d      ) -> (d) Retrieve the hash for register 'd'.
  ScriptOp_Equal             = 20, // [d,s    ] (d,s    ) -> (d) Compare 'd' and 's' and store result in register 'd'.
  ScriptOp_Less              = 21, // [d,s    ] (d,s    ) -> (d) Compare 'd' and 's' and store result in register 'd'.
  ScriptOp_Greater           = 22, // [d,s    ] (d,s    ) -> (d) Compare 'd' and 's' and store result in register 'd'.
  ScriptOp_Add               = 23, // [d,s    ] (d,s    ) -> (d) Add register 's' to 'd'.
  ScriptOp_Sub               = 24, // [d,s    ] (d,s    ) -> (d) Subtract register 's' from 'd'.
  ScriptOp_Mul               = 25, // [d,s    ] (d,s    ) -> (d) Multiply register 'd' by register 's'.
  ScriptOp_Div               = 26, // [d,s    ] (d,s    ) -> (d) Divide register 'd' by register 's'.
  ScriptOp_Mod               = 27, // [d,s    ] (d,s    ) -> (d) Modulo register 'd' by register 's'.
  ScriptOp_Negate            = 28, // [d      ] (d      ) -> (d) Negate register 'd'.
  ScriptOp_Invert            = 29, // [d      ] (d      ) -> (d) Invert register 'd'.
  ScriptOp_Distance          = 30, // [d,s    ] (d,s    ) -> (d) Compute the distance between 'd' and 's' and store result in register 'd'.
  ScriptOp_Angle             = 31, // [d,s    ] (d,s    ) -> (d) Compare the angle between 'd' and 's' and store result in register 'd'.
  ScriptOp_Sin               = 32, // [d      ] (d      ) -> (d) Evaluate the sine wave at 'd'.
  ScriptOp_Cos               = 33, // [d      ] (d      ) -> (d) Evaluate the cosine wave at 'd'.
  ScriptOp_Normalize         = 34, // [d      ] (d      ) -> (d) Normalize register 'd'.
  ScriptOp_Magnitude         = 35, // [d      ] (d      ) -> (d) Compute the magnitude of register 'd'.
  ScriptOp_Absolute          = 36, // [d      ] (d      ) -> (d) Normalize register 'd'.
  ScriptOp_VecX              = 37, // [d      ] (d      ) -> (d) Retrieve the x component of a vector in register 'd'.
  ScriptOp_VecY              = 38, // [d      ] (d      ) -> (d) Retrieve the y component of a vector in register 'd'.
  ScriptOp_VecZ              = 39, // [d      ] (d      ) -> (d) Retrieve the z component of a vector in register 'd'.
  ScriptOp_Vec3Compose       = 40, // [x,y,z  ] (x,y,z  ) -> (x) Compose a vector from 'x', 'y', 'z' and store in register 'x'.
  ScriptOp_QuatFromEuler     = 41, // [x,y,z  ] (x,y,z  ) -> (x) Compose a quaternion from 'x', 'y', 'z' angles and store in register 'x'.
  ScriptOp_QuatFromAngleAxis = 42, // [x,y    ] (x,y    ) -> (x) Compose a quaternion from angle 'x' and axis 'y' and store in register 'x'.
  ScriptOp_ColorCompose      = 43, // [x,y,z,w] (x,y,z,w) -> (x) Compose a color from 'x', 'y', 'z', 'w' and store in register 'x'.
  ScriptOp_ColorComposeHsv   = 44, // [x,y,z,w] (x,y,z,w) -> (x) Compose a hsv color from 'x', 'y', 'z', 'w' and store in register 'x'.
  ScriptOp_ColorFor          = 45, // [d      ] (d      ) -> (d) Compute a color for register 'd'.
  ScriptOp_Random            = 46, // [d      ] (       ) -> (d) Compute a random value (0 - 1) in register 'd'.
  ScriptOp_RandomSphere      = 47, // [d      ] (       ) -> (d) Compute a random vector on a unit sphere in register 'd'.
  ScriptOp_RandomCircleXZ    = 48, // [d      ] (       ) -> (d) Compute a random vector on a unit circle in register 'd'.
  ScriptOp_RandomBetween     = 49, // [x,y    ] (x,y    ) -> (x) Compute a random value between 'x' and 'y' and store in register 'x'.
  ScriptOp_RoundDown         = 50, // [d      ] (d      ) -> (d) Round register 'd' down.
  ScriptOp_RoundNearest      = 51, // [d      ] (d      ) -> (d) Round register 'd' to nearest.
  ScriptOp_RoundUp           = 52, // [d      ] (d      ) -> (d) Round register 'd' up.
  ScriptOp_Clamp             = 53, // [x,y,z  ] (x,y,z  ) -> (x) Clamp register 'x' between 'y' and 'z' and store in register 'x'.
  ScriptOp_Lerp              = 54, // [x,y,z  ] (x,y,z  ) -> (x) Compute a linearly interpolated value from 'x' to 'y' at time 'z' and store in register 'x'.
  ScriptOp_Min               = 55, // [x,y    ] (x,y    ) -> (x) Store the minimum value of 'x' and 'y' in register 'x'.
  ScriptOp_Max               = 56, // [x,y    ] (x,y    ) -> (x) Store the maximum value of 'x' and 'y' in register 'x'.
  ScriptOp_Perlin3           = 57, // [d      ] (       ) -> (d) Compute a 3d perlin noise in register 'd'.
} ScriptOp;

// clang-format on

typedef struct {
  u32         executedOps;
  ScriptPanic panic;
  ScriptVal   val;
} ScriptVmResult;

/**
 * Evaluate the given byte-code.
 * NOTE: Maximum supported code size is u16_max.
 */
ScriptVmResult
script_vm_eval(const ScriptDoc*, String code, ScriptMem*, const ScriptBinder*, void* bindCtx);

/**
 * Disassemble the given byte-code.
 */
void   script_vm_disasm_write(const ScriptDoc*, String code, DynString* out);
String script_vm_disasm_scratch(const ScriptDoc*, String code);
