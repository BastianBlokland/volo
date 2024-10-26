#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_thread.h"
#include "core_utf8.h"
#include "script_binder.h"
#include "script_diag.h"
#include "script_lex.h"
#include "script_read.h"
#include "script_sig.h"
#include "script_sym.h"

#include "doc_internal.h"
#include "val_internal.h"

#define script_depth_max 25
#define script_block_size_max 128
#define script_args_max 10
#define script_builtin_consts_max 32
#define script_builtin_funcs_max 48
#define script_tracked_mem_keys_max 32

typedef struct {
  StringHash idHash;
  ScriptVal  val;
  String     id;
} ScriptBuiltinConst;

static ScriptBuiltinConst g_scriptBuiltinConsts[script_builtin_consts_max];
static u32                g_scriptBuiltinConstCount;

static const ScriptBuiltinConst* script_builtin_const_lookup(const StringHash id) {
  for (u32 i = 0; i != g_scriptBuiltinConstCount; ++i) {
    if (g_scriptBuiltinConsts[i].idHash == id) {
      return &g_scriptBuiltinConsts[i];
    }
  }
  return null;
}

static void script_builtin_const_add(const String id, const ScriptVal val) {
  diag_assert(g_scriptBuiltinConstCount != script_builtin_consts_max);
  diag_assert(!script_builtin_const_lookup(string_hash(id)));
  g_scriptBuiltinConsts[g_scriptBuiltinConstCount++] = (ScriptBuiltinConst){
      .id     = id,
      .idHash = string_hash(id),
      .val    = val,
  };
}

typedef struct {
  StringHash      idHash;
  ScriptSig*      sig;
  ScriptIntrinsic intr;
  String          id;
  String          doc;
} ScriptBuiltinFunc;

static ScriptBuiltinFunc g_scriptBuiltinFuncs[script_builtin_funcs_max];
static u32               g_scriptBuiltinFuncCount;

static const ScriptBuiltinFunc* script_builtin_func_lookup(const StringHash id) {
  for (u32 i = 0; i != g_scriptBuiltinFuncCount; ++i) {
    if (g_scriptBuiltinFuncs[i].idHash == id) {
      return &g_scriptBuiltinFuncs[i];
    }
  }
  return null;
}

static void script_builtin_func_add(
    const String          id,
    const ScriptIntrinsic intr,
    const String          doc,
    const ScriptMask      retMask,
    const ScriptSigArg    args[],
    const u8              argCount) {
  diag_assert(g_scriptBuiltinFuncCount != script_builtin_funcs_max);
  diag_assert(script_intrinsic_arg_count(intr) == argCount);
  diag_assert(argCount < script_args_max);
  diag_assert(!script_builtin_func_lookup(string_hash(id)));
  g_scriptBuiltinFuncs[g_scriptBuiltinFuncCount++] = (ScriptBuiltinFunc){
      .idHash = string_hash(id),
      .sig    = script_sig_create(g_allocPersist, retMask, args, argCount),
      .intr   = intr,
      .id     = id,
      .doc    = doc,
  };
}

static void script_builtin_init(void) {
  diag_assert(g_scriptBuiltinConstCount == 0);
  diag_assert(g_scriptBuiltinFuncCount == 0);

  // clang-format off

  // Builtin constants.
  script_builtin_const_add(string_lit("null"),        script_null());
  script_builtin_const_add(string_lit("true"),        script_bool(true));
  script_builtin_const_add(string_lit("false"),       script_bool(false));
  script_builtin_const_add(string_lit("pi"),          script_num(math_pi_f64));
  script_builtin_const_add(string_lit("deg_to_rad"),  script_num(math_deg_to_rad));
  script_builtin_const_add(string_lit("rad_to_deg"),  script_num(math_rad_to_deg));
  script_builtin_const_add(string_lit("up"),          script_vec3(geo_up));
  script_builtin_const_add(string_lit("down"),        script_vec3(geo_down));
  script_builtin_const_add(string_lit("left"),        script_vec3(geo_left));
  script_builtin_const_add(string_lit("right"),       script_vec3(geo_right));
  script_builtin_const_add(string_lit("forward"),     script_vec3(geo_forward));
  script_builtin_const_add(string_lit("backward"),    script_vec3(geo_backward));
  script_builtin_const_add(string_lit("quat_ident"),  script_quat(geo_quat_ident));
  script_builtin_const_add(string_lit("white"),       script_color(geo_color_white));
  script_builtin_const_add(string_lit("black"),       script_color(geo_color_black));
  script_builtin_const_add(string_lit("red"),         script_color(geo_color_red));
  script_builtin_const_add(string_lit("green"),       script_color(geo_color_green));
  script_builtin_const_add(string_lit("blue"),        script_color(geo_color_blue));

  // Builtin functions.
  {
    const String       name   = string_lit("type");
    const String       doc    = string_lit("Retrieve the type of the given value.");
    const ScriptMask   ret    = script_mask_str;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_any},
    };
    script_builtin_func_add(name, ScriptIntrinsic_Type, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("hash");
    const String       doc    = string_lit("Compute the hash for the given value.");
    const ScriptMask   ret    = script_mask_num;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_any},
    };
    script_builtin_func_add(name, ScriptIntrinsic_Hash, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("mem_load");
    const String       doc    = string_lit("Load a value from memory.\n\n*Note*: Identical to using `$myKey` but can be used with a dynamic key.");
    const ScriptMask   ret    = script_mask_any;
    const ScriptSigArg args[] = {
        {string_lit("key"), script_mask_str},
    };
    script_builtin_func_add(name, ScriptIntrinsic_MemLoadDynamic, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("mem_store");
    const String       doc    = string_lit("Store a memory value.\n\n*Note*: Identical to using `$myKey = value` but can be used with a dynamic key.");
    const ScriptMask   ret    = script_mask_any;
    const ScriptSigArg args[] = {
        {string_lit("key"), script_mask_str},
        {string_lit("value"), script_mask_any},
    };
    script_builtin_func_add(name, ScriptIntrinsic_MemStoreDynamic, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("vec3");
    const String       doc    = string_lit("Construct a new vector.");
    const ScriptMask   ret    = script_mask_vec3;
    const ScriptSigArg args[] = {
        {string_lit("x"), script_mask_num},
        {string_lit("y"), script_mask_num},
        {string_lit("z"), script_mask_num},
    };
    script_builtin_func_add(name, ScriptIntrinsic_Vec3Compose, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("vec_x");
    const String       doc    = string_lit("Retrieve the x component of a vector.");
    const ScriptMask   ret    = script_mask_num;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_vec3},
    };
    script_builtin_func_add(name, ScriptIntrinsic_VecX, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("vec_y");
    const String       doc    = string_lit("Retrieve the y component of a vector.");
    const ScriptMask   ret    = script_mask_num;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_vec3},
    };
    script_builtin_func_add(name, ScriptIntrinsic_VecY, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("vec_z");
    const String       doc    = string_lit("Retrieve the z component of a vector.");
    const ScriptMask   ret    = script_mask_num;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_vec3},
    };
    script_builtin_func_add(name, ScriptIntrinsic_VecZ, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("euler");
    const String       doc    = string_lit("Construct a quaternion from the given euler angles (in radians).");
    const ScriptMask   ret    = script_mask_quat;
    const ScriptSigArg args[] = {
        {string_lit("x"), script_mask_num},
        {string_lit("y"), script_mask_num},
        {string_lit("z"), script_mask_num},
    };
    script_builtin_func_add(name, ScriptIntrinsic_QuatFromEuler, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("angle_axis");
    const String       doc    = string_lit("Construct a quaternion from an angle (in radians) and an axis.");
    const ScriptMask   ret    = script_mask_quat;
    const ScriptSigArg args[] = {
        {string_lit("angle"), script_mask_num},
        {string_lit("axis"), script_mask_vec3},
    };
    script_builtin_func_add(name, ScriptIntrinsic_QuatFromAngleAxis, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("color");
    const String       doc    = string_lit("Construct a new color.");
    const ScriptMask   ret    = script_mask_color;
    const ScriptSigArg args[] = {
        {string_lit("r"), script_mask_num},
        {string_lit("g"), script_mask_num},
        {string_lit("b"), script_mask_num},
        {string_lit("a"), script_mask_num},
    };
    script_builtin_func_add(name, ScriptIntrinsic_ColorCompose, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("color_hsv");
    const String       doc    = string_lit("Construct a new color from hue-saturation-value numbers.");
    const ScriptMask   ret    = script_mask_color;
    const ScriptSigArg args[] = {
        {string_lit("h"), script_mask_num},
        {string_lit("s"), script_mask_num},
        {string_lit("v"), script_mask_num},
        {string_lit("a"), script_mask_num},
    };
    script_builtin_func_add(name, ScriptIntrinsic_ColorComposeHsv, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("color_for");
    const String       doc    = string_lit("Retrieve a color for the given value.\n\n*Note*: Returns identical colors for identical values, useful for debug purposes.");
    const ScriptMask   ret    = script_mask_color;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_any},
    };
    script_builtin_func_add(name, ScriptIntrinsic_ColorFor, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("distance");
    const String       doc    = string_lit("Compute the distance between two values.");
    const ScriptMask   ret    = script_mask_num;
    const ScriptSigArg args[] = {
        {string_lit("a"), script_mask_num | script_mask_vec3},
        {string_lit("b"), script_mask_num | script_mask_vec3},
    };
    script_builtin_func_add(name, ScriptIntrinsic_Distance, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("magnitude");
    const String       doc    = string_lit("Compute the magnitude of the given value.");
    const ScriptMask   ret    = script_mask_num;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_num | script_mask_vec3 | script_mask_color},
    };
    script_builtin_func_add(name, ScriptIntrinsic_Magnitude, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("abs");
    const String       doc    = string_lit("Compute the absolute of the given value.");
    const ScriptMask   ret    = script_mask_any;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_num | script_mask_vec3 | script_mask_color},
    };
    script_builtin_func_add(name, ScriptIntrinsic_Absolute, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("sin");
    const String       doc    = string_lit("Evaluate the sine function.");
    const ScriptMask   ret    = script_mask_num;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_num},
    };
    script_builtin_func_add(name, ScriptIntrinsic_Sin, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("cos");
    const String       doc    = string_lit("Evaluate the cosine function.");
    const ScriptMask   ret    = script_mask_num;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_num},
    };
    script_builtin_func_add(name, ScriptIntrinsic_Cos, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("normalize");
    const String       doc    = string_lit("Normalize the given value.");
    const ScriptMask   ret    = script_mask_vec3 | script_mask_quat;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_vec3 | script_mask_quat},
    };
    script_builtin_func_add(name, ScriptIntrinsic_Normalize, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("angle");
    const String       doc    = string_lit("Compute the angle (in radians) between two directions or two quaternions.");
    const ScriptMask   ret    = script_mask_num;
    const ScriptSigArg args[] = {
        {string_lit("a"), script_mask_vec3 | script_mask_quat},
        {string_lit("b"), script_mask_vec3 | script_mask_quat},
    };
    script_builtin_func_add(name, ScriptIntrinsic_Angle, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("random");
    const String       doc    = string_lit("Compute a random value between 0.0 (inclusive) and 1.0 (exclusive) with a uniform distribution.");
    const ScriptMask   ret    = script_mask_num;
    script_builtin_func_add(name, ScriptIntrinsic_Random, doc, ret, null, 0);
  }
  {
    const String       name   = string_lit("random_between");
    const String       doc    = string_lit("Compute a random value between the given min (inclusive) and max (exclusive) values with a uniform distribution.");
    const ScriptMask   ret    = script_mask_num | script_mask_vec3;
    const ScriptSigArg args[] = {
        {string_lit("min"), script_mask_num | script_mask_vec3},
        {string_lit("max"), script_mask_num | script_mask_vec3},
    };
    script_builtin_func_add(name, ScriptIntrinsic_RandomBetween, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("random_sphere");
    const String       doc    = string_lit("Compute a random vector inside a unit sphere with a uniform distribution.");
    const ScriptMask   ret    = script_mask_vec3;
    script_builtin_func_add(name, ScriptIntrinsic_RandomSphere, doc, ret, null, 0);
  }
  {
    const String       name   = string_lit("random_circle_xz");
    const String       doc    = string_lit("Compute a random vector inside a xz unit circle with a uniform distribution.");
    const ScriptMask   ret    = script_mask_vec3;
    script_builtin_func_add(name, ScriptIntrinsic_RandomCircleXZ, doc, ret, null, 0);
  }
  {
    const String       name   = string_lit("round_down");
    const String       doc    = string_lit("Round the given value down to an integer.");
    const ScriptMask   ret    = script_mask_num | script_mask_vec3;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_num | script_mask_vec3},
    };
    script_builtin_func_add(name, ScriptIntrinsic_RoundDown, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("round_nearest");
    const String       doc    = string_lit("Round the given value to the nearest integer.");
    const ScriptMask   ret    = script_mask_num | script_mask_vec3;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_num | script_mask_vec3},
    };
    script_builtin_func_add(name, ScriptIntrinsic_RoundNearest, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("round_up");
    const String       doc    = string_lit("Round the given value up to an integer.");
    const ScriptMask   ret    = script_mask_num | script_mask_vec3;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_num | script_mask_vec3},
    };
    script_builtin_func_add(name, ScriptIntrinsic_RoundUp, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("clamp");
    const String       doc    = string_lit("Clamp given value between a minimum and a maximum.");
    const ScriptMask   ret    = script_mask_any;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_any},
        {string_lit("min"), script_mask_any},
        {string_lit("max"), script_mask_any},
    };
    script_builtin_func_add(name, ScriptIntrinsic_Clamp, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("lerp");
    const String       doc    = string_lit("Compute the linearly interpolated value from x to y at time t.");
    const ScriptMask   ret    = script_mask_any;
    const ScriptSigArg args[] = {
        {string_lit("x"), script_mask_any},
        {string_lit("y"), script_mask_any},
        {string_lit("t"), script_mask_num},
    };
    script_builtin_func_add(name, ScriptIntrinsic_Lerp, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("min");
    const String       doc    = string_lit("Return the minimum value.");
    const ScriptMask   ret    = script_mask_any;
    const ScriptSigArg args[] = {
        {string_lit("x"), script_mask_any},
        {string_lit("y"), script_mask_any},
    };
    script_builtin_func_add(name, ScriptIntrinsic_Min, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("max");
    const String       doc    = string_lit("Return the maximum value.");
    const ScriptMask   ret    = script_mask_any;
    const ScriptSigArg args[] = {
        {string_lit("x"), script_mask_any},
        {string_lit("y"), script_mask_any},
    };
    script_builtin_func_add(name, ScriptIntrinsic_Max, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("perlin3");
    const String       doc    = string_lit("Evaluate the perlin gradient noise at the given position.");
    const ScriptMask   ret    = script_mask_num;
    const ScriptSigArg args[] = {
        {string_lit("pos"), script_mask_vec3},
    };
    script_builtin_func_add(name, ScriptIntrinsic_Perlin3, doc, ret, args, array_elems(args));
  }
  {
    const String       name   = string_lit("assert");
    const String       doc    = string_lit("Assert that the given value is truthy.");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("v"), script_mask_any},
    };
    script_builtin_func_add(name, ScriptIntrinsic_Assert, doc, ret, args, array_elems(args));
  }

  // clang-format on
}

typedef enum {
  OpPrecedence_None,
  OpPrecedence_Assignment,
  OpPrecedence_Conditional,
  OpPrecedence_Logical,
  OpPrecedence_Equality,
  OpPrecedence_Relational,
  OpPrecedence_Additive,
  OpPrecedence_Multiplicative,
  OpPrecedence_Unary,
} OpPrecedence;

static OpPrecedence op_precedence(const ScriptTokenKind kind) {
  switch (kind) {
  case ScriptTokenKind_EqEq:
  case ScriptTokenKind_BangEq:
    return OpPrecedence_Equality;
  case ScriptTokenKind_Le:
  case ScriptTokenKind_LeEq:
  case ScriptTokenKind_Gt:
  case ScriptTokenKind_GtEq:
    return OpPrecedence_Relational;
  case ScriptTokenKind_Plus:
  case ScriptTokenKind_Minus:
    return OpPrecedence_Additive;
  case ScriptTokenKind_Star:
  case ScriptTokenKind_Slash:
  case ScriptTokenKind_Percent:
    return OpPrecedence_Multiplicative;
  case ScriptTokenKind_AmpAmp:
  case ScriptTokenKind_PipePipe:
    return OpPrecedence_Logical;
  case ScriptTokenKind_QMark:
  case ScriptTokenKind_QMarkQMark:
    return OpPrecedence_Conditional;
  default:
    return OpPrecedence_None;
  }
}

static ScriptIntrinsic token_op_unary(const ScriptTokenKind kind) {
  switch (kind) {
  case ScriptTokenKind_Minus:
    return ScriptIntrinsic_Negate;
  case ScriptTokenKind_Bang:
    return ScriptIntrinsic_Invert;
  default:
    diag_assert_fail("Invalid unary operation token");
    UNREACHABLE
  }
}

static ScriptIntrinsic token_op_binary(const ScriptTokenKind kind) {
  switch (kind) {
  case ScriptTokenKind_EqEq:
    return ScriptIntrinsic_Equal;
  case ScriptTokenKind_BangEq:
    return ScriptIntrinsic_NotEqual;
  case ScriptTokenKind_Le:
    return ScriptIntrinsic_Less;
  case ScriptTokenKind_LeEq:
    return ScriptIntrinsic_LessOrEqual;
  case ScriptTokenKind_Gt:
    return ScriptIntrinsic_Greater;
  case ScriptTokenKind_GtEq:
    return ScriptIntrinsic_GreaterOrEqual;
  case ScriptTokenKind_Plus:
    return ScriptIntrinsic_Add;
  case ScriptTokenKind_Minus:
    return ScriptIntrinsic_Sub;
  case ScriptTokenKind_Star:
    return ScriptIntrinsic_Mul;
  case ScriptTokenKind_Slash:
    return ScriptIntrinsic_Div;
  case ScriptTokenKind_Percent:
    return ScriptIntrinsic_Mod;
  case ScriptTokenKind_AmpAmp:
    return ScriptIntrinsic_LogicAnd;
  case ScriptTokenKind_PipePipe:
    return ScriptIntrinsic_LogicOr;
  case ScriptTokenKind_QMarkQMark:
    return ScriptIntrinsic_NullCoalescing;
  default:
    diag_assert_fail("Invalid binary operation token");
    UNREACHABLE
  }
}

static ScriptIntrinsic token_op_binary_modify(const ScriptTokenKind kind) {
  switch (kind) {
  case ScriptTokenKind_PlusEq:
    return ScriptIntrinsic_Add;
  case ScriptTokenKind_MinusEq:
    return ScriptIntrinsic_Sub;
  case ScriptTokenKind_StarEq:
    return ScriptIntrinsic_Mul;
  case ScriptTokenKind_SlashEq:
    return ScriptIntrinsic_Div;
  case ScriptTokenKind_PercentEq:
    return ScriptIntrinsic_Mod;
  case ScriptTokenKind_QMarkQMarkEq:
    return ScriptIntrinsic_NullCoalescing;
  default:
    diag_assert_fail("Invalid binary modify operation token");
    UNREACHABLE
  }
}

static bool token_intr_rhs_scope(const ScriptIntrinsic intr) {
  switch (intr) {
  case ScriptIntrinsic_LogicAnd:
  case ScriptIntrinsic_LogicOr:
  case ScriptIntrinsic_NullCoalescing:
    return true;
  default:
    return false;
  }
}

typedef struct {
  StringHash    id;
  ScriptScopeId scopeId;
  ScriptVarId   varSlot;
  bool          used;
  ScriptSym     sym; // Only set when a ScriptSymBag is provided.
  ScriptRange   declRange;
  ScriptPos     validRangeStart;
} ScriptVarMeta;

typedef struct sScriptScope {
  ScriptScopeId        id;
  ScriptVarMeta        vars[script_var_count];
  struct sScriptScope* next;
} ScriptScope;

typedef enum {
  ScriptReadFlags_ProgramInvalid = 1 << 0,
} ScriptReadFlags;

// clang-format off

typedef enum {
  ScriptSection_InsideLoop           = 1 << 0,
  ScriptSection_InsideArg            = 1 << 1,
  ScriptSection_DisallowVarDeclare   = 1 << 2,
  ScriptSection_DisallowLoop         = 1 << 3,
  ScriptSection_DisallowIf           = 1 << 4,
  ScriptSection_DisallowReturn       = 1 << 5,
  ScriptSection_DisallowStatement    = 0 | ScriptSection_DisallowVarDeclare
                                         | ScriptSection_DisallowLoop
                                         | ScriptSection_DisallowIf
                                         | ScriptSection_DisallowReturn
                                       ,
  ScriptSection_ResetOnExplicitScope = 0 | ScriptSection_DisallowStatement
                                       ,
} ScriptSection;

// clang-format on

typedef struct {
  ScriptDoc*          doc;
  const ScriptBinder* binder;
  StringTable*        stringtable;
  ScriptDiagBag*      diags;
  ScriptSymBag*       syms;
  String              input, inputTotal;
  ScriptScope*        scopeRoot;
  ScriptReadFlags     flags : 8;
  ScriptSection       section : 8;
  u16                 recursionDepth;
  u32                 scopeCounter;
  u8                  varAvailability[bits_to_bytes(script_var_count) + 1]; // Bitmask of free vars.
  StringHash          trackedMemKeys[script_tracked_mem_keys_max];
} ScriptReadContext;

static ScriptSection read_section_add(ScriptReadContext* ctx, const ScriptSection flags) {
  const ScriptSection oldSection = ctx->section;
  ctx->section |= flags;
  return oldSection;
}

static ScriptSection read_section_reset(ScriptReadContext* ctx, const ScriptSection flags) {
  const ScriptSection oldSection = ctx->section;
  ctx->section &= ~flags;
  return oldSection;
}

static ScriptPos read_pos_current(ScriptReadContext* ctx) {
  return (ScriptPos)(ctx->inputTotal.size - ctx->input.size);
}

static ScriptPos read_pos_next(ScriptReadContext* ctx) {
  return script_pos_trim(ctx->inputTotal, read_pos_current(ctx));
}

static ScriptRange read_range_dummy(ScriptReadContext* ctx) {
  return script_range(read_pos_current(ctx), read_pos_current(ctx));
}

static ScriptRange read_range_to_current(ScriptReadContext* ctx, const ScriptPos start) {
  const ScriptPos cur = read_pos_current(ctx);
  return script_range(start, math_max(cur, start));
}

static ScriptRange read_range_to_next(ScriptReadContext* ctx, const ScriptPos start) {
  return script_range(start, read_pos_next(ctx) + 1);
}

static void read_emit_err(ScriptReadContext* ctx, const ScriptDiagKind kind, const ScriptRange r) {
  if (ctx->diags) {
    const ScriptDiag diag = {
        .severity = ScriptDiagSeverity_Error,
        .kind     = kind,
        .range    = r,
    };
    script_diag_push(ctx->diags, &diag);
  }
}

static void read_emit_unused_vars(ScriptReadContext* ctx, const ScriptScope* scope) {
  if (!ctx->diags || !script_diag_active(ctx->diags, ScriptDiagSeverity_Warning)) {
    return;
  }
  for (u32 i = 0; i != script_var_count; ++i) {
    if (!scope->vars[i].id) {
      break;
    }
    if (!scope->vars[i].used) {
      const ScriptDiag unusedDiag = {
          .severity = ScriptDiagSeverity_Warning,
          .kind     = ScriptDiag_VarUnused,
          .range    = scope->vars[i].declRange,
      };
      script_diag_push(ctx->diags, &unusedDiag);
    }
  }
}

static void read_sym_set_var_valid_ranges(ScriptReadContext* ctx, const ScriptScope* scope) {
  if (!ctx->syms) {
    return;
  }
  for (u32 i = 0; i != script_var_count; ++i) {
    if (scope->vars[i].id) {
      diag_assert(!sentinel_check(scope->vars[i].sym));

      const ScriptRange validRange = read_range_to_next(ctx, scope->vars[i].validRangeStart);
      script_sym_set_valid_range(ctx->syms, scope->vars[i].sym, validRange);
    }
  }
}

static bool read_var_alloc(ScriptReadContext* ctx, ScriptVarId* out) {
  const usize index = bitset_next(mem_var(ctx->varAvailability), 0);
  if (UNLIKELY(sentinel_check(index))) {
    return false;
  }
  bitset_clear(mem_var(ctx->varAvailability), index);
  *out = (ScriptVarId)index;
  return true;
}

static void read_var_free(ScriptReadContext* ctx, const ScriptVarId var) {
  diag_assert(!bitset_test(mem_var(ctx->varAvailability), var));
  bitset_set(mem_var(ctx->varAvailability), var);
}

static void read_var_free_all(ScriptReadContext* ctx) {
  bitset_set_all(mem_var(ctx->varAvailability), script_var_count);
}

static ScriptScope* read_scope_head(ScriptReadContext* ctx) { return ctx->scopeRoot; }
static ScriptScope* read_scope_tail(ScriptReadContext* ctx) {
  ScriptScope* scope = ctx->scopeRoot;
  for (; scope->next; scope = scope->next)
    ;
  return scope;
}

static void read_scope_push(ScriptReadContext* ctx, ScriptScope* scope) {
  scope->id                  = (ScriptScopeId)ctx->scopeCounter++;
  read_scope_tail(ctx)->next = scope;
}

static void read_scope_pop(ScriptReadContext* ctx) {
  ScriptScope* scope = read_scope_tail(ctx);
  diag_assert(scope && scope != ctx->scopeRoot);

  // Remove the scope from the scope list.
  ScriptScope* newTail = ctx->scopeRoot;
  for (; newTail->next != scope; newTail = newTail->next)
    ;
  newTail->next = null;

  read_sym_set_var_valid_ranges(ctx, scope);
  read_emit_unused_vars(ctx, scope);

  // Free all the variables that the scope declared.
  for (u32 i = 0; i != script_var_count; ++i) {
    if (scope->vars[i].id) {
      read_var_free(ctx, scope->vars[i].varSlot);
    }
  }
}

static ScriptVarMeta*
read_var_declare(ScriptReadContext* ctx, const StringHash id, const ScriptRange declRange) {
  ScriptScope* scope = read_scope_tail(ctx);
  diag_assert(scope);

  for (u32 i = 0; i != script_var_count; ++i) {
    if (scope->vars[i].id) {
      continue; // Var already in use.
    }
    ScriptVarId varId;
    if (!read_var_alloc(ctx, &varId)) {
      return null;
    }
    ScriptSym sym = script_sym_sentinel;
    if (ctx->syms) {
      const String label = script_range_text(ctx->inputTotal, declRange);
      sym                = script_sym_push_var(ctx->syms, label, varId, scope->id, declRange);
    }
    scope->vars[i] = (ScriptVarMeta){
        .id              = id,
        .scopeId         = scope->id,
        .varSlot         = varId,
        .sym             = sym,
        .declRange       = declRange,
        .validRangeStart = read_pos_next(ctx) + 1,
    };
    return &scope->vars[i];
  }
  return null;
}

static ScriptVarMeta* read_var_lookup(ScriptReadContext* ctx, const StringHash id) {
  for (ScriptScope* scope = read_scope_head(ctx); scope; scope = scope->next) {
    for (u32 i = 0; i != script_var_count; ++i) {
      if (scope->vars[i].id == id) {
        return &scope->vars[i];
      }
      if (!scope->vars[i].id) {
        break;
      }
    }
  }
  return null;
}

static bool read_track_mem_key(ScriptReadContext* ctx, const StringHash key) {
  for (u32 i = 0; i != script_tracked_mem_keys_max; ++i) {
    if (ctx->trackedMemKeys[i] == key) {
      return true;
    }
    if (!ctx->trackedMemKeys[i]) {
      ctx->trackedMemKeys[i] = key;
      return true;
    }
  }
  return false;
}

static ScriptToken read_peek(ScriptReadContext* ctx) {
  ScriptToken token;
  script_lex(ctx->input, null, &token, ScriptLexFlags_None);
  return token;
}

static ScriptToken read_consume(ScriptReadContext* ctx) {
  ScriptToken token;
  ctx->input = script_lex(ctx->input, ctx->stringtable, &token, ScriptLexFlags_None);
  return token;
}

static bool read_consume_if(ScriptReadContext* ctx, const ScriptTokenKind kind) {
  ScriptToken  token;
  const String rem = script_lex(ctx->input, ctx->stringtable, &token, ScriptLexFlags_None);
  if (token.kind == kind) {
    ctx->input = rem;
    return true;
  }
  return false;
}

/**
 * We differentiate between two different kinds of failure while parsing:
 *
 * 1) Structural failure (for example a missing expression):
 *      At this point we do not know how to interpret the following tokens and thus we produce an
 *      invalid token (and generally stop parsing at that point).
 *
 * 2) Semantic failure (for example an unknown variable identifier):
 *      An dummy token is returned (null) and parsing continues.
 *
 * In both cases however we mark that the program is invalid and wont produce a valid output.
 */

static ScriptExpr read_fail_structural(ScriptReadContext* ctx) {
  ctx->flags |= ScriptReadFlags_ProgramInvalid;
  return script_expr_sentinel;
}

static ScriptExpr read_fail_semantic(ScriptReadContext* ctx, const ScriptRange range) {
  ctx->flags |= ScriptReadFlags_ProgramInvalid;
  return script_add_value(ctx->doc, range, script_null());
}

static ScriptExpr read_expr(ScriptReadContext*, OpPrecedence minPrecedence);

static void read_emit_unnecessary_semicolon(ScriptReadContext* ctx, const ScriptRange sepRange) {
  if (!ctx->diags || !script_diag_active(ctx->diags, ScriptDiagSeverity_Warning)) {
    return;
  }
  ScriptToken nextToken;
  script_lex(ctx->input, null, &nextToken, ScriptLexFlags_IncludeNewlines);
  if (UNLIKELY(nextToken.kind == ScriptTokenKind_Newline)) {
    const ScriptDiag unnecessaryDiag = {
        .severity = ScriptDiagSeverity_Warning,
        .kind     = ScriptDiag_UnnecessarySemicolon,
        .range    = sepRange,
    };
    script_diag_push(ctx->diags, &unnecessaryDiag);
  }
}

static void read_visitor_has_side_effect(void* ctx, const ScriptDoc* doc, const ScriptExpr expr) {
  bool* hasSideEffect = ctx;
  switch (expr_kind(doc, expr)) {
  case ScriptExprKind_MemStore:
  case ScriptExprKind_VarStore:
  case ScriptExprKind_Extern:
    *hasSideEffect = true;
    return;
  case ScriptExprKind_Value:
  case ScriptExprKind_VarLoad:
  case ScriptExprKind_MemLoad:
  case ScriptExprKind_Block:
    return;
  case ScriptExprKind_Intrinsic: {
    switch (expr_data(doc, expr)->intrinsic.intrinsic) {
    case ScriptIntrinsic_MemStoreDynamic:
    case ScriptIntrinsic_Continue:
    case ScriptIntrinsic_Break:
    case ScriptIntrinsic_Return:
    case ScriptIntrinsic_Assert:
      *hasSideEffect = true;
      // Fallthrough.
    default:
      return;
    }
  }
  case ScriptExprKind_Count:
    break;
  }
  diag_assert_fail("Unknown expression kind");
  UNREACHABLE
}

static void
read_emit_no_effect(ScriptReadContext* ctx, const ScriptExpr exprs[], const u32 exprCount) {
  if (!ctx->diags || !script_diag_active(ctx->diags, ScriptDiagSeverity_Warning)) {
    return;
  }
  for (u32 i = 0; i != (exprCount - 1); ++i) {
    bool hasSideEffect = false;
    script_expr_visit(ctx->doc, exprs[i], &hasSideEffect, read_visitor_has_side_effect);
    if (!hasSideEffect) {
      const ScriptDiag noEffectDiag = {
          .severity = ScriptDiagSeverity_Warning,
          .kind     = ScriptDiag_ExprHasNoEffect,
          .range    = expr_range(ctx->doc, exprs[i]),
      };
      script_diag_push(ctx->diags, &noEffectDiag);
    }
  }
}

static void
read_emit_unreachable(ScriptReadContext* ctx, const ScriptExpr exprs[], const u32 exprCount) {
  if (!ctx->diags || !script_diag_active(ctx->diags, ScriptDiagSeverity_Warning)) {
    return;
  }
  for (u32 i = 0; i != (exprCount - 1); ++i) {
    const ScriptDocSignal uncaughtSignal = script_expr_always_uncaught_signal(ctx->doc, exprs[i]);
    if (uncaughtSignal) {
      const ScriptPos  unreachableStart = expr_range(ctx->doc, exprs[i + 1]).start;
      const ScriptPos  unreachableEnd   = expr_range(ctx->doc, exprs[exprCount - 1]).end;
      const ScriptDiag unreachableDiag  = {
          .severity = ScriptDiagSeverity_Warning,
          .kind     = ScriptDiag_ExprUnreachable,
          .range    = script_range(unreachableStart, unreachableEnd),
      };
      script_diag_push(ctx->diags, &unreachableDiag);
      break;
    }
  }
}

typedef enum {
  ScriptBlockType_Implicit,
  ScriptBlockType_Explicit,
} ScriptBlockType;

static bool read_is_block_end(const ScriptTokenKind tokenKind, const ScriptBlockType blockType) {
  if (blockType == ScriptBlockType_Explicit && tokenKind == ScriptTokenKind_CurlyClose) {
    return true;
  }
  return tokenKind == ScriptTokenKind_End;
}

static bool read_is_block_separator(const ScriptTokenKind tokenKind) {
  return tokenKind == ScriptTokenKind_Newline || tokenKind == ScriptTokenKind_Semicolon;
}

static ScriptExpr read_expr_block(ScriptReadContext* ctx, const ScriptBlockType blockType) {
  ScriptExpr exprs[script_block_size_max];
  u32        exprCount = 0;

  if (read_is_block_end(read_peek(ctx).kind, blockType)) {
    goto BlockEnd; // Empty block.
  }

BlockNext:
  if (UNLIKELY(exprCount == script_block_size_max)) {
    const ScriptPos   blockStart = expr_range(ctx->doc, exprs[0]).start;
    const ScriptRange blockRange = read_range_to_current(ctx, blockStart);
    return read_emit_err(ctx, ScriptDiag_BlockTooBig, blockRange), read_fail_structural(ctx);
  }
  const ScriptExpr exprNew = read_expr(ctx, OpPrecedence_None);
  if (UNLIKELY(sentinel_check(exprNew))) {
    return read_fail_structural(ctx);
  }
  exprs[exprCount++] = exprNew;

  if (read_is_block_end(read_peek(ctx).kind, blockType)) {
    goto BlockEnd;
  }

  const ScriptPos sepStart = read_pos_next(ctx);
  ScriptToken     sepToken;
  ctx->input = script_lex(ctx->input, ctx->stringtable, &sepToken, ScriptLexFlags_IncludeNewlines);

  if (!read_is_block_separator(sepToken.kind)) {
    read_emit_err(ctx, ScriptDiag_MissingSemicolon, expr_range(ctx->doc, exprNew));
    return read_fail_structural(ctx);
  }
  if (sepToken.kind == ScriptTokenKind_Semicolon) {
    const ScriptRange sepRange = read_range_to_current(ctx, sepStart);
    read_emit_unnecessary_semicolon(ctx, sepRange);
  }
  if (!read_is_block_end(read_peek(ctx).kind, blockType)) {
    goto BlockNext;
  }

BlockEnd:;
  switch (exprCount) {
  case 0: {
    return script_add_value(ctx->doc, read_range_dummy(ctx), script_null());
  }
  case 1:
    return exprs[0];
  default:
    read_emit_no_effect(ctx, exprs, exprCount);
    read_emit_unreachable(ctx, exprs, exprCount);

    const ScriptRange blockRange = {
        .start = expr_range(ctx->doc, exprs[0]).start,
        .end   = expr_range(ctx->doc, exprs[exprCount - 1]).end,
    };
    return script_add_block(ctx->doc, blockRange, exprs, exprCount);
  }
}

/**
 * NOTE: Caller is expected to consume the opening curly-brace.
 */
static ScriptExpr read_expr_scope_block(ScriptReadContext* ctx, const ScriptPos start) {
  ScriptScope scope = {0};
  read_scope_push(ctx, &scope);

  const ScriptSection prevSection = read_section_reset(ctx, ScriptSection_ResetOnExplicitScope);
  const ScriptExpr    expr        = read_expr_block(ctx, ScriptBlockType_Explicit);
  ctx->section                    = prevSection;

  diag_assert(&scope == read_scope_tail(ctx));
  read_scope_pop(ctx);

  if (UNLIKELY(sentinel_check(expr))) {
    return read_fail_structural(ctx);
  }

  if (UNLIKELY(read_consume(ctx).kind != ScriptTokenKind_CurlyClose)) {
    const ScriptRange range = script_range(start, expr_range(ctx->doc, expr).end);
    return read_emit_err(ctx, ScriptDiag_UnterminatedBlock, range), read_fail_structural(ctx);
  }

  return expr;
}

static ScriptExpr read_expr_scope_single(ScriptReadContext* ctx, const OpPrecedence prec) {
  ScriptScope scope = {0};
  read_scope_push(ctx, &scope);

  const ScriptExpr expr = read_expr(ctx, prec);

  diag_assert(&scope == read_scope_tail(ctx));
  read_scope_pop(ctx);

  return expr;
}

/**
 * NOTE: Caller is expected to consume the opening parenthesis.
 */
static ScriptExpr read_expr_paren(ScriptReadContext* ctx, const ScriptPos start) {
  const ScriptExpr expr = read_expr(ctx, OpPrecedence_None);
  if (UNLIKELY(sentinel_check(expr))) {
    return read_fail_structural(ctx);
  }
  const ScriptToken closeToken = read_consume(ctx);
  if (UNLIKELY(closeToken.kind != ScriptTokenKind_ParenClose)) {
    const ScriptRange range = read_range_to_current(ctx, start);
    read_emit_err(ctx, ScriptDiag_UnclosedParenthesizedExpr, range);
    return read_fail_structural(ctx);
  }
  return expr;
}

static bool read_is_arg_end(const ScriptTokenKind tokenKind) {
  return tokenKind == ScriptTokenKind_Comma || tokenKind == ScriptTokenKind_ParenClose;
}

static bool read_is_args_end(const ScriptTokenKind tokenKind) {
  return tokenKind == ScriptTokenKind_End || tokenKind == ScriptTokenKind_ParenClose;
}

/**
 * NOTE: Caller is expected to consume the opening parenthesis.
 */
static i32 read_args(ScriptReadContext* ctx, ScriptExpr outExprs[script_args_max]) {
  i32 count = 0;

  if (read_is_args_end(read_peek(ctx).kind)) {
    goto ArgEnd; // Empty argument list.
  }

ArgNext:;
  if (UNLIKELY(count == script_args_max)) {
    const ScriptRange wholeArgsRange = {
        .start = expr_range(ctx->doc, outExprs[0]).start,
        .end   = expr_range(ctx->doc, outExprs[count - 1]).end,
    };
    return read_emit_err(ctx, ScriptDiag_ArgumentCountExceedsMaximum, wholeArgsRange), -1;
  }
  const ScriptSection section = ScriptSection_InsideArg | ScriptSection_DisallowLoop |
                                ScriptSection_DisallowIf | ScriptSection_DisallowReturn;
  const ScriptSection prevSection = read_section_add(ctx, section);
  const ScriptExpr    arg         = read_expr(ctx, OpPrecedence_None);
  ctx->section                    = prevSection;
  if (UNLIKELY(sentinel_check(arg))) {
    return -1;
  }
  outExprs[count++] = arg;

  if (read_consume_if(ctx, ScriptTokenKind_Comma)) {
    goto ArgNext;
  }

ArgEnd:
  if (UNLIKELY(read_consume(ctx).kind != ScriptTokenKind_ParenClose)) {
    ScriptRange range;
    if (count == 0) {
      range = read_range_dummy(ctx);
    } else {
      range = expr_range(ctx->doc, outExprs[count - 1]);
    }
    return read_emit_err(ctx, ScriptDiag_UnterminatedArgumentList, range), -1;
  }
  return count;
}

static ScriptExpr read_expr_var_declare(ScriptReadContext* ctx, const ScriptPos start) {
  const ScriptPos   idStart = read_pos_next(ctx);
  const ScriptToken token   = read_consume(ctx);
  const ScriptRange idRange = read_range_to_current(ctx, idStart);
  if (UNLIKELY(token.kind != ScriptTokenKind_Identifier)) {
    return read_emit_err(ctx, ScriptDiag_VarIdInvalid, idRange), read_fail_semantic(ctx, idRange);
  }
  if (script_builtin_const_lookup(token.val_identifier)) {
    return read_emit_err(ctx, ScriptDiag_VarIdConflicts, idRange), read_fail_semantic(ctx, idRange);
  }
  if (read_var_lookup(ctx, token.val_identifier)) {
    return read_emit_err(ctx, ScriptDiag_VarIdConflicts, idRange), read_fail_semantic(ctx, idRange);
  }

  ScriptExpr valExpr;
  if (read_consume_if(ctx, ScriptTokenKind_Eq)) {
    const ScriptSection prevSection = read_section_add(ctx, ScriptSection_DisallowStatement);
    valExpr                         = read_expr(ctx, OpPrecedence_Assignment);
    ctx->section                    = prevSection;
    if (UNLIKELY(sentinel_check(valExpr))) {
      return read_fail_structural(ctx);
    }
  } else {
    valExpr = script_add_value(ctx->doc, read_range_dummy(ctx), script_null());
  }

  const ScriptRange range = script_range(start, script_expr_range(ctx->doc, valExpr).end);

  const ScriptVarMeta* var = read_var_declare(ctx, token.val_identifier, idRange);
  if (!var) {
    return read_emit_err(ctx, ScriptDiag_VarLimitExceeded, range), read_fail_semantic(ctx, range);
  }
  return script_add_var_store(ctx->doc, range, var->scopeId, var->varSlot, valExpr);
}

static ScriptExpr
read_expr_var_lookup(ScriptReadContext* ctx, const StringHash id, const ScriptPos start) {
  const ScriptRange         range   = read_range_to_current(ctx, start);
  const ScriptBuiltinConst* builtin = script_builtin_const_lookup(id);
  if (builtin) {
    return script_add_value(ctx->doc, range, builtin->val);
  }
  ScriptVarMeta* var = read_var_lookup(ctx, id);
  if (var) {
    var->used = true;
    return script_add_var_load(ctx->doc, range, var->scopeId, var->varSlot);
  }
  return read_emit_err(ctx, ScriptDiag_NoVarFoundForId, range), read_fail_semantic(ctx, range);
}

static ScriptExpr
read_expr_var_assign(ScriptReadContext* ctx, const StringHash id, const ScriptPos start) {
  const ScriptSection prevSection = read_section_add(ctx, ScriptSection_DisallowStatement);
  const ScriptExpr    expr        = read_expr(ctx, OpPrecedence_Assignment);
  ctx->section                    = prevSection;
  if (UNLIKELY(sentinel_check(expr))) {
    return read_fail_structural(ctx);
  }
  const ScriptRange range = script_range(start, script_expr_range(ctx->doc, expr).end);

  const ScriptVarMeta* var = read_var_lookup(ctx, id);
  if (UNLIKELY(!var)) {
    return read_emit_err(ctx, ScriptDiag_NoVarFoundForId, range), read_fail_semantic(ctx, range);
  }

  return script_add_var_store(ctx->doc, range, var->scopeId, var->varSlot, expr);
}

static ScriptExpr read_expr_var_modify(
    ScriptReadContext*    ctx,
    const StringHash      id,
    const ScriptTokenKind tokenKind,
    const ScriptRange     varRange) {
  const ScriptSection   prevSection = read_section_add(ctx, ScriptSection_DisallowStatement);
  const ScriptIntrinsic intr        = token_op_binary_modify(tokenKind);
  const ScriptExpr      val         = token_intr_rhs_scope(intr)
                                          ? read_expr_scope_single(ctx, OpPrecedence_Assignment)
                                          : read_expr(ctx, OpPrecedence_Assignment);
  ctx->section                      = prevSection;
  if (UNLIKELY(sentinel_check(val))) {
    return read_fail_structural(ctx);
  }
  const ScriptRange range = script_range(varRange.start, script_expr_range(ctx->doc, val).end);

  ScriptVarMeta* var = read_var_lookup(ctx, id);
  if (UNLIKELY(!var)) {
    return read_emit_err(ctx, ScriptDiag_NoVarFoundForId, range), read_fail_semantic(ctx, range);
  }

  var->used = true;

  const ScriptExpr loadExpr   = script_add_var_load(ctx->doc, varRange, var->scopeId, var->varSlot);
  const ScriptExpr intrArgs[] = {loadExpr, val};
  const ScriptExpr intrExpr   = script_add_intrinsic(ctx->doc, range, intr, intrArgs);
  return script_add_var_store(ctx->doc, range, var->scopeId, var->varSlot, intrExpr);
}

static ScriptExpr
read_expr_mem_store(ScriptReadContext* ctx, const StringHash key, const ScriptPos start) {
  const ScriptSection prevSection = read_section_add(ctx, ScriptSection_DisallowStatement);
  const ScriptExpr    val         = read_expr(ctx, OpPrecedence_Assignment);
  ctx->section                    = prevSection;

  if (UNLIKELY(sentinel_check(val))) {
    return read_fail_structural(ctx);
  }
  const ScriptRange range = script_range(start, script_expr_range(ctx->doc, val).end);
  return script_add_mem_store(ctx->doc, range, key, val);
}

static ScriptExpr read_expr_mem_modify(
    ScriptReadContext*    ctx,
    const StringHash      key,
    const ScriptTokenKind tokenKind,
    const ScriptRange     keyRange) {
  const ScriptSection   prevSection = read_section_add(ctx, ScriptSection_DisallowStatement);
  const ScriptIntrinsic intr        = token_op_binary_modify(tokenKind);
  const ScriptExpr      val         = token_intr_rhs_scope(intr)
                                          ? read_expr_scope_single(ctx, OpPrecedence_Assignment)
                                          : read_expr(ctx, OpPrecedence_Assignment);
  ctx->section                      = prevSection;
  if (UNLIKELY(sentinel_check(val))) {
    return read_fail_structural(ctx);
  }
  const ScriptRange range      = script_range(keyRange.start, script_expr_range(ctx->doc, val).end);
  const ScriptExpr  loadExpr   = script_add_mem_load(ctx->doc, keyRange, key);
  const ScriptExpr  intrArgs[] = {loadExpr, val};
  const ScriptExpr  intrExpr   = script_add_intrinsic(ctx->doc, range, intr, intrArgs);
  return script_add_mem_store(ctx->doc, range, key, intrExpr);
}

static void read_emit_invalid_args(
    ScriptReadContext* ctx,
    const ScriptExpr   args[],
    const u8           argCount,
    const ScriptSig*   sig,
    const ScriptRange  range) {
  if (!ctx->diags || !script_diag_active(ctx->diags, ScriptDiagSeverity_Warning)) {
    return;
  }

  if (argCount < script_sig_arg_min_count(sig)) {
    const ScriptDiag tooFewArgsDiag = {
        .severity = ScriptDiagSeverity_Warning,
        .kind     = ScriptDiag_TooFewArguments,
        .range    = range,
    };
    script_diag_push(ctx->diags, &tooFewArgsDiag);
    return;
  }

  if (argCount > script_sig_arg_max_count(sig)) {
    const ScriptDiag tooManyArgsDiag = {
        .severity = ScriptDiagSeverity_Warning,
        .kind     = ScriptDiag_TooManyArguments,
        .range    = range,
    };
    script_diag_push(ctx->diags, &tooManyArgsDiag);
    return;
  }

  const u8 argsToValidate = math_min(argCount, script_sig_arg_count(sig));
  for (u8 i = 0; i != argsToValidate; ++i) {
    const ScriptSigArg arg = script_sig_arg(sig, i);
    if (arg.mask == script_mask_any) {
      continue; // Any value is valid; no need to validate.
    }
    if (!script_expr_static(ctx->doc, args[i])) {
      continue; // Non-static argument; cannot validate as the value is only known at runtime.
    }
    const ScriptVal argVal = script_expr_static_val(ctx->doc, args[i]);
    if (!val_type_check(argVal, arg.mask)) {
      const ScriptDiag invalidArgValue = {
          .severity = ScriptDiagSeverity_Warning,
          .kind     = ScriptDiag_InvalidArgumentValue,
          .range    = script_expr_range(ctx->doc, args[i]),
      };
      script_diag_push(ctx->diags, &invalidArgValue);
    }
  }
}

/**
 * NOTE: Caller is expected to consume the opening parenthesis.
 */
static ScriptExpr
read_expr_call(ScriptReadContext* ctx, const StringHash id, const ScriptRange idRange) {
  ScriptExpr args[script_args_max];
  const i32  argCount = read_args(ctx, args);
  if (UNLIKELY(argCount < 0)) {
    return read_fail_structural(ctx);
  }
  diag_assert((u32)argCount < u8_max);

  const ScriptRange callRange = read_range_to_current(ctx, idRange.start);

  const ScriptBuiltinFunc* builtin = script_builtin_func_lookup(id);
  if (builtin) {
    const u8 expectedArgCount = script_sig_arg_count(builtin->sig);
    if (UNLIKELY(expectedArgCount != argCount)) {
      read_emit_err(ctx, ScriptDiag_IncorrectArgCountForBuiltinFunc, callRange);

      /**
       * Mark the program as invalid but still emit a correct intrinsic, this helps the
       * language-server know what intrinsic the user tried to call.
       *
       * NOTE: Incase of too little arguments we will have to insert null padding values to make
       * sure the program is well formed.
       */
      const ScriptPos lastPos = callRange.end - 1;
      for (u8 i = (u8)argCount; i < expectedArgCount; ++i) {
        args[i] = script_add_value(ctx->doc, script_range(lastPos, lastPos), script_null());
      }
      ctx->flags |= ScriptReadFlags_ProgramInvalid;
    } else {
      // Correct number of arguments; validate value types and emit warnings if needed.
      read_emit_invalid_args(ctx, args, (u8)argCount, builtin->sig, callRange);
    }
    return script_add_intrinsic(ctx->doc, callRange, builtin->intr, args);
  }

  if (ctx->binder) {
    const ScriptBinderSlot externFunc = script_binder_lookup(ctx->binder, id);
    if (!sentinel_check(externFunc)) {

      const ScriptSig* sig = script_binder_sig(ctx->binder, externFunc);
      if (sig) {
        read_emit_invalid_args(ctx, args, (u8)argCount, sig, callRange);
      }

      return script_add_extern(ctx->doc, callRange, externFunc, args, (u16)argCount);
    }
  }

  return read_emit_err(ctx, ScriptDiag_NoFuncFoundForId, idRange), read_fail_semantic(ctx, idRange);
}

static void read_emit_static_condition(ScriptReadContext* ctx, const ScriptExpr expr) {
  if (!ctx->diags || !script_diag_active(ctx->diags, ScriptDiagSeverity_Warning)) {
    return;
  }
  if (script_expr_static(ctx->doc, expr)) {
    const ScriptDiag staticConditionDiag = {
        .severity = ScriptDiagSeverity_Warning,
        .kind     = ScriptDiag_ConditionExprStatic,
        .range    = expr_range(ctx->doc, expr),
    };
    script_diag_push(ctx->diags, &staticConditionDiag);
  }
}

static ScriptExpr read_expr_if(ScriptReadContext* ctx, const ScriptPos start) {
  const ScriptToken token = read_consume(ctx);
  if (UNLIKELY(token.kind != ScriptTokenKind_ParenOpen)) {
    const ScriptRange range = read_range_to_current(ctx, start);
    return read_emit_err(ctx, ScriptDiag_InvalidIf, range), read_fail_semantic(ctx, range);
  }

  ScriptScope scope = {0};
  read_scope_push(ctx, &scope);

  ScriptExpr conditions[script_args_max];
  const i32  conditionCount = read_args(ctx, conditions);
  if (UNLIKELY(conditionCount < 0)) {
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }
  if (UNLIKELY(conditionCount != 1)) {
    ctx->flags |= ScriptReadFlags_ProgramInvalid;
    read_emit_err(ctx, ScriptDiag_InvalidConditionCount, read_range_to_current(ctx, start));
    if (!conditionCount) {
      conditions[0] = read_fail_semantic(ctx, read_range_dummy(ctx));
    }
  } else {
    read_emit_static_condition(ctx, conditions[0]);
  }

  ScriptExpr b1, b2;

  if (UNLIKELY(read_peek(ctx).kind != ScriptTokenKind_CurlyOpen)) {
    read_emit_err(ctx, ScriptDiag_BlockExpected, read_range_dummy(ctx));
    b1 = read_fail_semantic(ctx, read_range_dummy(ctx));
    b2 = read_fail_semantic(ctx, read_range_dummy(ctx));
    goto RetIfExpr;
  }

  const ScriptPos b1BlockStart = read_pos_next(ctx);
  read_consume(ctx); // Consume the opening curly.

  b1 = read_expr_scope_block(ctx, b1BlockStart);
  if (UNLIKELY(sentinel_check(b1))) {
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }

  if (read_consume_if(ctx, ScriptTokenKind_Else)) {
    const ScriptPos elseBlockStart = read_pos_next(ctx);
    if (read_consume_if(ctx, ScriptTokenKind_CurlyOpen)) {
      b2 = read_expr_scope_block(ctx, elseBlockStart);
      if (UNLIKELY(sentinel_check(b2))) {
        return read_scope_pop(ctx), read_fail_structural(ctx);
      }
    } else if (read_consume_if(ctx, ScriptTokenKind_If)) {
      b2 = read_expr_if(ctx, elseBlockStart);
      if (UNLIKELY(sentinel_check(b2))) {
        return read_scope_pop(ctx), read_fail_structural(ctx);
      }
    } else {
      read_emit_err(ctx, ScriptDiag_BlockOrIfExpected, read_range_dummy(ctx));
      b2 = read_fail_semantic(ctx, read_range_dummy(ctx));
    }
  } else {
    b2 = script_add_value(ctx->doc, read_range_dummy(ctx), script_null());
  }

RetIfExpr:
  diag_assert(&scope == read_scope_tail(ctx));
  read_scope_pop(ctx);

  const ScriptRange range      = read_range_to_current(ctx, start);
  const ScriptExpr  intrArgs[] = {conditions[0], b1, b2};
  return script_add_intrinsic(ctx->doc, range, ScriptIntrinsic_Select, intrArgs);
}

static ScriptExpr read_expr_while(ScriptReadContext* ctx, const ScriptPos start) {
  const ScriptToken token = read_consume(ctx);
  if (UNLIKELY(token.kind != ScriptTokenKind_ParenOpen)) {
    const ScriptRange range = read_range_to_current(ctx, start);
    return read_emit_err(ctx, ScriptDiag_InvalidWhileLoop, range), read_fail_semantic(ctx, range);
  }

  ScriptScope scope = {0};
  read_scope_push(ctx, &scope);

  ScriptExpr conditions[script_args_max];
  const i32  conditionCount = read_args(ctx, conditions);
  if (UNLIKELY(conditionCount < 0)) {
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }
  if (UNLIKELY(conditionCount != 1)) {
    ctx->flags |= ScriptReadFlags_ProgramInvalid;
    read_emit_err(ctx, ScriptDiag_InvalidConditionCount, read_range_to_current(ctx, start));
    if (!conditionCount) {
      conditions[0] = read_fail_semantic(ctx, read_range_dummy(ctx));
    }
  } else {
    read_emit_static_condition(ctx, conditions[0]);
  }

  ScriptExpr body;

  if (UNLIKELY(read_peek(ctx).kind != ScriptTokenKind_CurlyOpen)) {
    read_emit_err(ctx, ScriptDiag_BlockExpected, read_range_dummy(ctx));
    body = read_fail_semantic(ctx, read_range_dummy(ctx));
    goto RetWhileExpr;
  }

  const ScriptPos blockStart = read_pos_next(ctx);
  read_consume(ctx); // Consume the opening curly.

  const ScriptSection prevSection = read_section_add(ctx, ScriptSection_InsideLoop);
  body                            = read_expr_scope_block(ctx, blockStart);
  ctx->section                    = prevSection;

  if (UNLIKELY(sentinel_check(body))) {
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }

RetWhileExpr:
  diag_assert(&scope == read_scope_tail(ctx));
  read_scope_pop(ctx);

  const ScriptRange range = read_range_to_current(ctx, start);
  // NOTE: Setup and Increment loop parts are not used in while loops.
  const ScriptExpr setupExpr  = script_add_value(ctx->doc, read_range_dummy(ctx), script_null());
  const ScriptExpr incrExpr   = script_add_value(ctx->doc, read_range_dummy(ctx), script_null());
  const ScriptExpr intrArgs[] = {setupExpr, conditions[0], incrExpr, body};
  return script_add_intrinsic(ctx->doc, range, ScriptIntrinsic_Loop, intrArgs);
}

static void read_emit_static_for_comp(
    ScriptReadContext* ctx, const ScriptExpr expr, const ScriptRange exprRange) {
  if (!ctx->diags || !script_diag_active(ctx->diags, ScriptDiagSeverity_Warning)) {
    return;
  }
  if (script_expr_static(ctx->doc, expr)) {
    const ScriptDiag staticForCompDiag = {
        .severity = ScriptDiagSeverity_Warning,
        .kind     = ScriptDiag_ForLoopCompStatic,
        .range    = exprRange,
    };
    script_diag_push(ctx->diags, &staticForCompDiag);
  }
}

typedef enum {
  ReadForComp_Setup,
  ReadForComp_Condition,
  ReadForComp_Increment,
} ReadForComp;

static ScriptExpr read_expr_for_comp(ScriptReadContext* ctx, const ReadForComp comp) {
  static const ScriptTokenKind g_endTokens[] = {
      [ReadForComp_Setup]     = ScriptTokenKind_Semicolon,
      [ReadForComp_Condition] = ScriptTokenKind_Semicolon,
      [ReadForComp_Increment] = ScriptTokenKind_ParenClose,
  };
  const ScriptPos start = read_pos_next(ctx);
  ScriptExpr      res;
  if (read_peek(ctx).kind == g_endTokens[comp]) {
    const ScriptRange range   = read_range_to_current(ctx, start);
    const ScriptVal   skipVal = comp == ReadForComp_Condition ? script_bool(true) : script_null();
    res                       = script_add_value(ctx->doc, range, skipVal);
  } else if (UNLIKELY(read_peek(ctx).kind == ScriptTokenKind_ParenClose)) {
    const ScriptRange range = read_range_to_current(ctx, start);
    return read_emit_err(ctx, ScriptDiag_ForLoopCompMissing, range), read_fail_semantic(ctx, range);
  } else {
    res = read_expr(ctx, OpPrecedence_None);
    if (UNLIKELY(sentinel_check(res))) {
      return read_fail_structural(ctx);
    }
    read_emit_static_for_comp(ctx, res, read_range_to_current(ctx, start));
  }
  if (UNLIKELY(read_consume(ctx).kind != g_endTokens[comp])) {
    const ScriptRange    range = read_range_to_current(ctx, start);
    const ScriptDiagKind err   = comp == ReadForComp_Increment ? ScriptDiag_InvalidForLoop
                                                               : ScriptDiag_ForLoopSeparatorMissing;
    return read_emit_err(ctx, err, range), read_fail_structural(ctx);
  }
  return res;
}

static ScriptExpr read_expr_for(ScriptReadContext* ctx, const ScriptPos start) {
  const ScriptToken token = read_consume(ctx);
  if (UNLIKELY(token.kind != ScriptTokenKind_ParenOpen)) {
    const ScriptRange range = read_range_to_current(ctx, start);
    return read_emit_err(ctx, ScriptDiag_InvalidForLoop, range), read_fail_semantic(ctx, range);
  }

  ScriptScope scope = {0};
  read_scope_push(ctx, &scope);

  const ScriptExpr setupExpr = read_expr_for_comp(ctx, ReadForComp_Setup);
  if (UNLIKELY(sentinel_check(setupExpr))) {
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }
  const ScriptExpr condExpr = read_expr_for_comp(ctx, ReadForComp_Condition);
  if (UNLIKELY(sentinel_check(condExpr))) {
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }
  const ScriptExpr incrExpr = read_expr_for_comp(ctx, ReadForComp_Increment);
  if (UNLIKELY(sentinel_check(incrExpr))) {
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }

  ScriptExpr body;

  if (UNLIKELY(read_peek(ctx).kind != ScriptTokenKind_CurlyOpen)) {
    read_emit_err(ctx, ScriptDiag_BlockExpected, read_range_dummy(ctx));
    body = read_fail_semantic(ctx, read_range_dummy(ctx));
    goto RetForExpr;
  }

  const ScriptPos blockStart = read_pos_next(ctx);
  read_consume(ctx); // Consume the opening curly.

  const ScriptSection prevSection = read_section_add(ctx, ScriptSection_InsideLoop);
  body                            = read_expr_scope_block(ctx, blockStart);
  ctx->section                    = prevSection;

  if (UNLIKELY(sentinel_check(body))) {
    return read_scope_pop(ctx), read_fail_structural(ctx);
  }

RetForExpr:
  diag_assert(&scope == read_scope_tail(ctx));
  read_scope_pop(ctx);

  const ScriptRange range      = read_range_to_current(ctx, start);
  const ScriptExpr  intrArgs[] = {setupExpr, condExpr, incrExpr, body};
  return script_add_intrinsic(ctx->doc, range, ScriptIntrinsic_Loop, intrArgs);
}

static ScriptExpr read_expr_select(ScriptReadContext* ctx, const ScriptExpr condition) {
  const ScriptPos start = expr_range(ctx->doc, condition).start;

  const ScriptExpr b1 = read_expr_scope_single(ctx, OpPrecedence_Conditional);
  if (UNLIKELY(sentinel_check(b1))) {
    return read_fail_structural(ctx);
  }

  ScriptExpr b2;
  if (UNLIKELY(read_peek(ctx).kind != ScriptTokenKind_Colon)) {
    const ScriptRange range = read_range_to_current(ctx, start);
    read_emit_err(ctx, ScriptDiag_MissingColonInSelectExpr, range);
    b2 = read_fail_semantic(ctx, read_range_dummy(ctx));
    goto RetSelectExpr;
  }
  read_consume(ctx); // Consume the colon.

  b2 = read_expr_scope_single(ctx, OpPrecedence_Conditional);
  if (UNLIKELY(sentinel_check(b2))) {
    return read_fail_structural(ctx);
  }

RetSelectExpr:;
  const ScriptRange range      = script_range(start, script_expr_range(ctx->doc, b2).end);
  const ScriptExpr  intrArgs[] = {condition, b1, b2};
  return script_add_intrinsic(ctx->doc, range, ScriptIntrinsic_Select, intrArgs);
}

static bool read_is_return_separator(const ScriptTokenKind tokenKind) {
  switch (tokenKind) {
  case ScriptTokenKind_Newline:
  case ScriptTokenKind_Semicolon:
  case ScriptTokenKind_CurlyClose:
  case ScriptTokenKind_End:
    return true;
  default:
    return false;
  }
}

static ScriptExpr read_expr_return(ScriptReadContext* ctx, const ScriptPos start) {
  ScriptToken nextToken;
  script_lex(ctx->input, null, &nextToken, ScriptLexFlags_IncludeNewlines);

  ScriptExpr retExpr;
  if (read_is_return_separator(nextToken.kind)) {
    retExpr = script_add_value(ctx->doc, read_range_dummy(ctx), script_null());
  } else {
    const ScriptSection prevSection = read_section_add(ctx, ScriptSection_DisallowStatement);
    retExpr                         = read_expr(ctx, OpPrecedence_Assignment);
    ctx->section                    = prevSection;
    if (UNLIKELY(sentinel_check(retExpr))) {
      return read_fail_structural(ctx);
    }
  }

  const ScriptRange range      = script_range(start, script_expr_range(ctx->doc, retExpr).end);
  const ScriptExpr  intrArgs[] = {retExpr};
  return script_add_intrinsic(ctx->doc, range, ScriptIntrinsic_Return, intrArgs);
}

static ScriptExpr read_expr_primary(ScriptReadContext* ctx) {
  const ScriptPos start = read_pos_next(ctx);

  ScriptToken  token;
  const String prevInput = ctx->input;
  ctx->input             = script_lex(prevInput, ctx->stringtable, &token, ScriptLexFlags_None);

  const ScriptRange range = read_range_to_current(ctx, start);

  switch (token.kind) {
  /**
   * Parenthesized expression.
   */
  case ScriptTokenKind_ParenOpen:
    return read_expr_paren(ctx, start);
  /**
   * Scope.
   */
  case ScriptTokenKind_CurlyOpen:
    return read_expr_scope_block(ctx, start);
  /**
   * Keywords.
   */
  case ScriptTokenKind_If:
    if (UNLIKELY(ctx->section & ScriptSection_DisallowIf)) {
      goto MissingPrimaryExpr;
    }
    return read_expr_if(ctx, start);
  case ScriptTokenKind_While:
    if (UNLIKELY(ctx->section & ScriptSection_DisallowLoop)) {
      goto MissingPrimaryExpr;
    }
    return read_expr_while(ctx, start);
  case ScriptTokenKind_For:
    if (UNLIKELY(ctx->section & ScriptSection_DisallowLoop)) {
      goto MissingPrimaryExpr;
    }
    return read_expr_for(ctx, start);
  case ScriptTokenKind_Var:
    if (UNLIKELY(ctx->section & ScriptSection_DisallowVarDeclare)) {
      goto MissingPrimaryExpr;
    }
    return read_expr_var_declare(ctx, start);
  case ScriptTokenKind_Continue:
    if (UNLIKELY(!(ctx->section & ScriptSection_InsideLoop))) {
      return read_emit_err(ctx, ScriptDiag_OnlyValidInLoop, range), read_fail_semantic(ctx, range);
    }
    return script_add_intrinsic(ctx->doc, range, ScriptIntrinsic_Continue, null);
  case ScriptTokenKind_Break:
    if (UNLIKELY(!(ctx->section & ScriptSection_InsideLoop))) {
      return read_emit_err(ctx, ScriptDiag_OnlyValidInLoop, range), read_fail_semantic(ctx, range);
    }
    return script_add_intrinsic(ctx->doc, range, ScriptIntrinsic_Break, null);
  case ScriptTokenKind_Return:
    if (UNLIKELY(ctx->section & ScriptSection_DisallowReturn)) {
      goto MissingPrimaryExpr;
    }
    return read_expr_return(ctx, start);
  /**
   * Identifiers.
   */
  case ScriptTokenKind_Identifier: {
    const ScriptRange idRange = read_range_to_current(ctx, start);
    ScriptToken       nextToken;
    const String      remInput = script_lex(ctx->input, null, &nextToken, ScriptLexFlags_None);
    switch (nextToken.kind) {
    case ScriptTokenKind_ParenOpen:
      ctx->input = remInput; // Consume the 'nextToken'.
      return read_expr_call(ctx, token.val_identifier, idRange);
    case ScriptTokenKind_Eq:
      ctx->input = remInput; // Consume the 'nextToken'.
      return read_expr_var_assign(ctx, token.val_identifier, start);
    case ScriptTokenKind_PlusEq:
    case ScriptTokenKind_MinusEq:
    case ScriptTokenKind_StarEq:
    case ScriptTokenKind_SlashEq:
    case ScriptTokenKind_PercentEq:
    case ScriptTokenKind_QMarkQMarkEq:
      ctx->input = remInput; // Consume the 'nextToken'.
      return read_expr_var_modify(ctx, token.val_key, nextToken.kind, range);
    default:
      return read_expr_var_lookup(ctx, token.val_identifier, start);
    }
  }
  /**
   * Unary operators.
   */
  case ScriptTokenKind_Minus:
  case ScriptTokenKind_Bang: {
    const ScriptExpr val = read_expr(ctx, OpPrecedence_Unary);
    if (UNLIKELY(sentinel_check(val))) {
      return read_fail_structural(ctx);
    }
    const ScriptRange     rangeInclExpr = read_range_to_current(ctx, start);
    const ScriptIntrinsic intr          = token_op_unary(token.kind);
    const ScriptExpr      intrArgs[]    = {val};
    return script_add_intrinsic(ctx->doc, rangeInclExpr, intr, intrArgs);
  }
  /**
   * Literals.
   */
  case ScriptTokenKind_Number:
    return script_add_value(ctx->doc, range, script_num(token.val_number));
  case ScriptTokenKind_String:
    return script_add_value(ctx->doc, range, script_str(token.val_string));
  /**
   * Memory access.
   */
  case ScriptTokenKind_Key: {
    // TODO: Should failing to track be an error? Currently these are only used to report symbols.
    read_track_mem_key(ctx, token.val_key);

    ScriptToken  nextToken;
    const String remInput = script_lex(ctx->input, null, &nextToken, ScriptLexFlags_None);
    switch (nextToken.kind) {
    case ScriptTokenKind_Eq:
      ctx->input = remInput; // Consume the 'nextToken'.
      return read_expr_mem_store(ctx, token.val_key, start);
    case ScriptTokenKind_PlusEq:
    case ScriptTokenKind_MinusEq:
    case ScriptTokenKind_StarEq:
    case ScriptTokenKind_SlashEq:
    case ScriptTokenKind_PercentEq:
    case ScriptTokenKind_QMarkQMarkEq:
      ctx->input = remInput; // Consume the 'nextToken'.
      return read_expr_mem_modify(ctx, token.val_key, nextToken.kind, range);
    default:
      return script_add_mem_load(ctx->doc, range, token.val_key);
    }
  }
  /**
   * Lex errors.
   */
  case ScriptTokenKind_Semicolon:
    if (ctx->section & ScriptSection_DisallowStatement) {
      goto MissingPrimaryExpr;
    }
    return read_emit_err(ctx, ScriptDiag_UnexpectedSemicolon, range), read_fail_structural(ctx);
  case ScriptTokenKind_Diag:
    return read_emit_err(ctx, token.val_diag, range), read_fail_semantic(ctx, range);
  case ScriptTokenKind_End:
    return read_emit_err(ctx, ScriptDiag_MissingPrimaryExpr, range), read_fail_structural(ctx);
  default:
    /**
     * If we encounter an argument end token (comma or close-paren) inside an arg expression we can
     * reasonably assume that this was meant to end the argument and the actual expression is
     * missing. This has the advantage of turning it into a semantic error where we can keep parsing
     * the rest of the script.
     */
    if (ctx->section & ScriptSection_InsideArg && read_is_arg_end(token.kind)) {
      goto MissingPrimaryExpr;
    }

    // Unexpected token; treat it as a structural failure.
    return read_emit_err(ctx, ScriptDiag_InvalidPrimaryExpr, range), read_fail_structural(ctx);
  }

MissingPrimaryExpr:
  ctx->input = prevInput; // Un-consume the token.
  read_emit_err(ctx, ScriptDiag_MissingPrimaryExpr, read_range_dummy(ctx));
  return read_fail_semantic(ctx, read_range_dummy(ctx));
}

static ScriptExpr read_expr(ScriptReadContext* ctx, const OpPrecedence minPrecedence) {
  ++ctx->recursionDepth;
  if (UNLIKELY(ctx->recursionDepth >= script_depth_max)) {
    read_emit_err(ctx, ScriptDiag_RecursionLimitExceeded, read_range_dummy(ctx));
    return read_fail_structural(ctx);
  }

  const ScriptPos start = read_pos_next(ctx);
  ScriptExpr      res   = read_expr_primary(ctx);
  if (UNLIKELY(sentinel_check(res))) {
    return read_fail_structural(ctx);
  }

  /**
   * Test if the next token is an operator with higher precedence.
   */
  while (true) {
    ScriptToken  nextToken;
    const String remInput =
        script_lex(ctx->input, ctx->stringtable, &nextToken, ScriptLexFlags_None);

    const OpPrecedence opPrecedence = op_precedence(nextToken.kind);
    if (!opPrecedence || opPrecedence <= minPrecedence) {
      break;
    }
    /**
     * Next token is an operator with a high enough precedence.
     * Consume the token and recurse down the right hand side.
     */
    ctx->input = remInput; // Consume the 'nextToken'.

    /**
     * Binary / Ternary expressions.
     */
    switch (nextToken.kind) {
    case ScriptTokenKind_QMark: {
      read_emit_static_condition(ctx, res);

      res = read_expr_select(ctx, res);
      if (UNLIKELY(sentinel_check(res))) {
        return read_fail_structural(ctx);
      }
    } break;
    case ScriptTokenKind_EqEq:
    case ScriptTokenKind_BangEq:
    case ScriptTokenKind_Le:
    case ScriptTokenKind_LeEq:
    case ScriptTokenKind_Gt:
    case ScriptTokenKind_GtEq:
    case ScriptTokenKind_Plus:
    case ScriptTokenKind_Minus:
    case ScriptTokenKind_Star:
    case ScriptTokenKind_Slash:
    case ScriptTokenKind_Percent:
    case ScriptTokenKind_AmpAmp:
    case ScriptTokenKind_PipePipe:
    case ScriptTokenKind_QMarkQMark: {
      const ScriptIntrinsic intr = token_op_binary(nextToken.kind);
      const ScriptExpr rhs = token_intr_rhs_scope(intr) ? read_expr_scope_single(ctx, opPrecedence)
                                                        : read_expr(ctx, opPrecedence);
      if (UNLIKELY(sentinel_check(rhs))) {
        return read_fail_structural(ctx);
      }
      const ScriptRange range      = script_range(start, script_expr_range(ctx->doc, rhs).end);
      const ScriptExpr  intrArgs[] = {res, rhs};
      res                          = script_add_intrinsic(ctx->doc, range, intr, intrArgs);
    } break;
    default:
      diag_assert_fail("Invalid operator token");
      UNREACHABLE
    }
  }
  --ctx->recursionDepth;
  return res;
}

static void read_sym_push_keywords(ScriptReadContext* ctx) {
  if (!ctx->syms) {
    return;
  }
  for (u32 i = 0; i != script_lex_keyword_count(); ++i) {
    const String label = script_lex_keyword_data()[i].id;
    script_sym_push_keyword(ctx->syms, label);
  }
}

static void read_sym_push_builtin(ScriptReadContext* ctx) {
  if (!ctx->syms) {
    return;
  }
  for (u32 i = 0; i != g_scriptBuiltinConstCount; ++i) {
    script_sym_push_builtin_const(ctx->syms, g_scriptBuiltinConsts[i].id);
  }
  for (u32 i = 0; i != g_scriptBuiltinFuncCount; ++i) {
    script_sym_push_builtin_func(
        ctx->syms,
        g_scriptBuiltinFuncs[i].id,
        g_scriptBuiltinFuncs[i].doc,
        g_scriptBuiltinFuncs[i].intr,
        g_scriptBuiltinFuncs[i].sig);
  }
}

static void read_sym_push_extern(ScriptReadContext* ctx) {
  if (!ctx->syms || !ctx->binder) {
    return;
  }
  ScriptBinderSlot itr = script_binder_first(ctx->binder);
  for (; !sentinel_check(itr); itr = script_binder_next(ctx->binder, itr)) {
    const String     label = script_binder_name(ctx->binder, itr);
    const String     doc   = script_binder_doc(ctx->binder, itr);
    const ScriptSig* sig   = script_binder_sig(ctx->binder, itr);
    script_sym_push_extern_func(ctx->syms, label, doc, itr, sig);
  }
}

static void read_sym_push_mem_keys(ScriptReadContext* ctx) {
  if (!ctx->syms || !ctx->stringtable) {
    return;
  }
  for (u32 i = 0; i != script_tracked_mem_keys_max; ++i) {
    if (!ctx->trackedMemKeys[i]) {
      break;
    }
    const String keyStr = stringtable_lookup(ctx->stringtable, ctx->trackedMemKeys[i]);
    if (!string_is_empty(keyStr)) {
      const String label = fmt_write_scratch("${}", fmt_text(keyStr));
      script_sym_push_mem_key(ctx->syms, label, ctx->trackedMemKeys[i]);
    }
  }
}

static void script_link_binder(ScriptDoc* doc, const ScriptBinder* binder) {
  const ScriptBinderHash hash = script_binder_hash(binder);
  if (doc->binderHash && doc->binderHash != hash) {
    diag_assert_fail("ScriptDoc was already used with a different (and incompatible binder)");
  }
  doc->binderHash = hash;
}

static void script_read_init(void) {
  static bool           g_init;
  static ThreadSpinLock g_initLock;
  if (g_init) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_init) {
    script_builtin_init();
    g_init = true;
  }
  thread_spinlock_unlock(&g_initLock);
}

ScriptExpr script_read(
    ScriptDoc*          doc,
    const ScriptBinder* binder,
    const String        src,
    StringTable*        stringtable,
    ScriptDiagBag*      diags,
    ScriptSymBag*       syms) {
  script_read_init();

  if (binder) {
    script_link_binder(doc, binder);
  }

  ScriptScope       scopeRoot = {0};
  ScriptReadContext ctx       = {
      .doc         = doc,
      .binder      = binder,
      .stringtable = stringtable,
      .diags       = diags,
      .syms        = syms,
      .input       = src,
      .inputTotal  = src,
      .scopeRoot   = &scopeRoot,
  };
  read_var_free_all(&ctx);

  read_sym_push_keywords(&ctx);
  read_sym_push_builtin(&ctx);
  read_sym_push_extern(&ctx);

  const ScriptExpr expr = read_expr_block(&ctx, ScriptBlockType_Implicit);
  if (!sentinel_check(expr)) {
    diag_assert_msg(read_peek(&ctx).kind == ScriptTokenKind_End, "Not all input consumed");
  }

  read_sym_push_mem_keys(&ctx);
  read_sym_set_var_valid_ranges(&ctx, &scopeRoot);
  read_emit_unused_vars(&ctx, &scopeRoot);

  const bool fail = sentinel_check(expr) || (ctx.flags & ScriptReadFlags_ProgramInvalid) != 0;
#ifndef VOLO_FAST
  if (diags) {
    const bool hasErrDiag = script_diag_count(diags, ScriptDiagFilter_Error);
    diag_assert_msg(!fail || hasErrDiag, "No error diagnostic was produced for a failed read");
    diag_assert_msg(fail || !hasErrDiag, "Error diagnostic was produced for a successful read");
  }
#endif

  /**
   * NOTE: Currently we assume that if the caller provides a diagnostic bag it will check if the bag
   * has any errors to determine if the program is valid instead of just checking the output
   * expression. This is useful for tools that want to inspect the program even if it is invalid.
   * TODO: This should probably be an input flag instead.
   */
  const bool allowInvalidProgram = diags != null;

  return (fail && !allowInvalidProgram) ? script_expr_sentinel : expr;
}
