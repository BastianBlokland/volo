#pragma once
#include "core_array.h"
#include "script.h"
#include "script_panic.h"

#define script_prog_regs 32

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
 * - boolean         1 byte(s).
 * - small-int       1 byte(s).
 * - memory-key:     4 byte(s).
 *
 * NOTE: Multi-byte operation data is encoded as little-endian.
 * NOTE: There is no alignment requirement for operation data.
 * NOTE: Instruction values are 2 byte offsets from the start of the code memory.
 */
typedef enum eScriptOp {
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
  ScriptOp_ValueNull         = 10, // [d      ] (       ) -> (d) Load null value into register 'd'.
  ScriptOp_ValueBool         = 11, // [d,b    ] (       ) -> (d) Load value boolean 'b' into register 'd'.
  ScriptOp_ValueSmallInt     = 12, // [d,i    ] (       ) -> (d) Load small integer value 'i' into register 'd'.
  ScriptOp_MemLoad           = 13, // [d,k    ] (       ) -> (d) Load from memory at key 'k' into register 'd'.
  ScriptOp_MemStore          = 14, // [s,k    ] (s      ) -> ( ) Store to memory at key 'k' from register 's'.
  ScriptOp_MemLoadDyn        = 15, // [d      ] (d      ) -> (d) Load from memory with a key from register 'd'.
  ScriptOp_MemStoreDyn       = 16, // [s,r    ] (s,r    ) -> ( ) Store a value from register 's' to memory with a key from register 'r'.
  ScriptOp_Extern            = 17, // [d,f,r,c] (r:c    ) -> (d) Invoke extern func 'f' using count 'c' registers starting from 'r' and store result in register 'd'.
  ScriptOp_Truthy            = 18, // [d      ] (d      ) -> (d) Check if register 'd' is truthy.
  ScriptOp_Falsy             = 19, // [d      ] (d      ) -> (d) Check if register 'd' is falsy.
  ScriptOp_NonNull           = 20, // [d      ] (d      ) -> (d) Check if register 'd' is non-null.
  ScriptOp_Type              = 21, // [d      ] (d      ) -> (d) Retrieve the type for register 'd'.
  ScriptOp_Hash              = 22, // [d      ] (d      ) -> (d) Retrieve the hash for register 'd'.
  ScriptOp_Equal             = 23, // [d,s    ] (d,s    ) -> (d) Compare 'd' and 's' and store result in register 'd'.
  ScriptOp_Less              = 24, // [d,s    ] (d,s    ) -> (d) Compare 'd' and 's' and store result in register 'd'.
  ScriptOp_Greater           = 25, // [d,s    ] (d,s    ) -> (d) Compare 'd' and 's' and store result in register 'd'.
  ScriptOp_Add               = 26, // [d,s    ] (d,s    ) -> (d) Add register 's' to 'd'.
  ScriptOp_Sub               = 27, // [d,s    ] (d,s    ) -> (d) Subtract register 's' from 'd'.
  ScriptOp_Mul               = 28, // [d,s    ] (d,s    ) -> (d) Multiply register 'd' by register 's'.
  ScriptOp_Div               = 29, // [d,s    ] (d,s    ) -> (d) Divide register 'd' by register 's'.
  ScriptOp_Mod               = 30, // [d,s    ] (d,s    ) -> (d) Modulo register 'd' by register 's'.
  ScriptOp_Negate            = 31, // [d      ] (d      ) -> (d) Negate register 'd'.
  ScriptOp_Invert            = 32, // [d      ] (d      ) -> (d) Invert register 'd'.
  ScriptOp_Distance          = 33, // [d,s    ] (d,s    ) -> (d) Compute the distance between 'd' and 's' and store result in register 'd'.
  ScriptOp_Angle             = 34, // [d,s    ] (d,s    ) -> (d) Compare the angle between 'd' and 's' and store result in register 'd'.
  ScriptOp_Sin               = 35, // [d      ] (d      ) -> (d) Evaluate the sine wave at 'd'.
  ScriptOp_Cos               = 36, // [d      ] (d      ) -> (d) Evaluate the cosine wave at 'd'.
  ScriptOp_Normalize         = 37, // [d      ] (d      ) -> (d) Normalize register 'd'.
  ScriptOp_Magnitude         = 38, // [d      ] (d      ) -> (d) Compute the magnitude of register 'd'.
  ScriptOp_Absolute          = 39, // [d      ] (d      ) -> (d) Normalize register 'd'.
  ScriptOp_VecX              = 40, // [d      ] (d      ) -> (d) Retrieve the x component of a vector in register 'd'.
  ScriptOp_VecY              = 41, // [d      ] (d      ) -> (d) Retrieve the y component of a vector in register 'd'.
  ScriptOp_VecZ              = 42, // [d      ] (d      ) -> (d) Retrieve the z component of a vector in register 'd'.
  ScriptOp_Vec3Compose       = 43, // [x,y,z  ] (x,y,z  ) -> (x) Compose a vector from 'x', 'y', 'z' and store in register 'x'.
  ScriptOp_QuatFromEuler     = 44, // [x,y,z  ] (x,y,z  ) -> (x) Compose a quaternion from 'x', 'y', 'z' angles and store in register 'x'.
  ScriptOp_QuatFromAngleAxis = 45, // [x,y    ] (x,y    ) -> (x) Compose a quaternion from angle 'x' and axis 'y' and store in register 'x'.
  ScriptOp_ColorR            = 46, // [d      ] (d      ) -> (d) Retrieve the r component of a color in register 'd'.
  ScriptOp_ColorG            = 47, // [d      ] (d      ) -> (d) Retrieve the g component of a color in register 'd'.
  ScriptOp_ColorB            = 48, // [d      ] (d      ) -> (d) Retrieve the b component of a color in register 'd'.
  ScriptOp_ColorA            = 49, // [d      ] (d      ) -> (d) Retrieve the a component of a color in register 'd'.
  ScriptOp_ColorCompose      = 50, // [x,y,z,w] (x,y,z,w) -> (x) Compose a color from 'x', 'y', 'z', 'w' and store in register 'x'.
  ScriptOp_ColorComposeHsv   = 51, // [x,y,z,w] (x,y,z,w) -> (x) Compose a hsv color from 'x', 'y', 'z', 'w' and store in register 'x'.
  ScriptOp_ColorFor          = 52, // [d      ] (d      ) -> (d) Compute a color for register 'd'.
  ScriptOp_Random            = 53, // [d      ] (       ) -> (d) Compute a random value (0 - 1) in register 'd'.
  ScriptOp_RandomSphere      = 54, // [d      ] (       ) -> (d) Compute a random vector on a unit sphere in register 'd'.
  ScriptOp_RandomCircleXZ    = 55, // [d      ] (       ) -> (d) Compute a random vector on a unit circle in register 'd'.
  ScriptOp_RandomBetween     = 56, // [x,y    ] (x,y    ) -> (x) Compute a random value between 'x' and 'y' and store in register 'x'.
  ScriptOp_RoundDown         = 57, // [d      ] (d      ) -> (d) Round register 'd' down.
  ScriptOp_RoundNearest      = 58, // [d      ] (d      ) -> (d) Round register 'd' to nearest.
  ScriptOp_RoundUp           = 59, // [d      ] (d      ) -> (d) Round register 'd' up.
  ScriptOp_Clamp             = 60, // [x,y,z  ] (x,y,z  ) -> (x) Clamp register 'x' between 'y' and 'z' and store in register 'x'.
  ScriptOp_Lerp              = 61, // [x,y,z  ] (x,y,z  ) -> (x) Compute a linearly interpolated value from 'x' to 'y' at time 'z' and store in register 'x'.
  ScriptOp_Min               = 62, // [x,y    ] (x,y    ) -> (x) Store the minimum value of 'x' and 'y' in register 'x'.
  ScriptOp_Max               = 63, // [x,y    ] (x,y    ) -> (x) Store the maximum value of 'x' and 'y' in register 'x'.
  ScriptOp_Perlin3           = 64, // [d      ] (       ) -> (d) Compute a 3d perlin noise in register 'd'.
} ScriptOp;

// clang-format on

typedef struct {
  u16                instruction; // Offset in the code stream.
  ScriptRangeLineCol range;
} ScriptProgramLoc;

typedef struct sScriptProgram {
  struct {
    bool  external;
    void* ptr;
    usize size;
  } code; // Instruction stream (struct layout compatible with DataMem).
  ScriptBinderHash binderHash;
  HeapArray_t(ScriptVal) literals;
  HeapArray_t(ScriptProgramLoc) locations; // Sorted on instruction.
} ScriptProgram;

typedef struct {
  u32         executedOps;
  ScriptPanic panic;
  ScriptVal   val;
} ScriptProgResult;

void script_prog_destroy(ScriptProgram*, Allocator*);
void script_prog_clear(ScriptProgram*, Allocator*);

/**
 * Evaluate the program.
 * Pre-condition: script_prog_validate(program, binder).
 */
ScriptProgResult
script_prog_eval(const ScriptProgram*, ScriptMem*, const ScriptBinder*, void* bindCtx);

/**
 * Validate the given program.
 */
bool script_prog_validate(const ScriptProgram*, const ScriptBinder*);

/**
 * Lookup the source range for the given call-identifier.
 */
ScriptRangeLineCol script_prog_location(const ScriptProgram*, u32 callId);

/**
 * Write the program disassembly for diagnostic purposes.
 */
void   script_prog_write(const ScriptProgram*, DynString* out);
String script_prog_write_scratch(const ScriptProgram*);
