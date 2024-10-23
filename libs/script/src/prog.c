#include "core_alloc.h"
#include "core_diag.h"
#include "core_search.h"
#include "core_stringtable.h"
#include "script_binder.h"
#include "script_mem.h"
#include "script_prog.h"

#include "val_internal.h"

#define script_prog_ops_max 25000

// clang-format off
#define VM_VISIT_OP_SIMPLE                                                       \
  VM_OP_SIMPLE_UNARY(       Truthy,             script_truthy_as_val            )\
  VM_OP_SIMPLE_UNARY(       Falsy,              script_falsy_as_val             )\
  VM_OP_SIMPLE_UNARY(       NonNull,            script_non_null_as_val          )\
  VM_OP_SIMPLE_UNARY(       Type,               script_val_type                 )\
  VM_OP_SIMPLE_UNARY(       Hash,               script_val_hash                 )\
  VM_OP_SIMPLE_BINARY(      Equal,              script_val_equal_as_val         )\
  VM_OP_SIMPLE_BINARY(      Less,               script_val_less_as_val          )\
  VM_OP_SIMPLE_BINARY(      Greater,            script_val_greater_as_val       )\
  VM_OP_SIMPLE_BINARY(      Add,                script_val_add                  )\
  VM_OP_SIMPLE_BINARY(      Sub,                script_val_sub                  )\
  VM_OP_SIMPLE_BINARY(      Mul,                script_val_mul                  )\
  VM_OP_SIMPLE_BINARY(      Div,                script_val_div                  )\
  VM_OP_SIMPLE_BINARY(      Mod,                script_val_mod                  )\
  VM_OP_SIMPLE_UNARY(       Negate,             script_val_neg                  )\
  VM_OP_SIMPLE_UNARY(       Invert,             script_val_inv                  )\
  VM_OP_SIMPLE_BINARY(      Distance,           script_val_dist                 )\
  VM_OP_SIMPLE_BINARY(      Angle,              script_val_angle                )\
  VM_OP_SIMPLE_UNARY(       Sin,                script_val_sin                  )\
  VM_OP_SIMPLE_UNARY(       Cos,                script_val_cos                  )\
  VM_OP_SIMPLE_UNARY(       Normalize,          script_val_norm                 )\
  VM_OP_SIMPLE_UNARY(       Magnitude,          script_val_mag                  )\
  VM_OP_SIMPLE_UNARY(       Absolute,           script_val_abs                  )\
  VM_OP_SIMPLE_UNARY(       VecX,               script_val_vec_x                )\
  VM_OP_SIMPLE_UNARY(       VecY,               script_val_vec_y                )\
  VM_OP_SIMPLE_UNARY(       VecZ,               script_val_vec_z                )\
  VM_OP_SIMPLE_TERNARY(     Vec3Compose,        script_val_vec3_compose         )\
  VM_OP_SIMPLE_TERNARY(     QuatFromEuler,      script_val_quat_from_euler      )\
  VM_OP_SIMPLE_BINARY(      QuatFromAngleAxis,  script_val_quat_from_angle_axis )\
  VM_OP_SIMPLE_QUATERNARY(  ColorCompose,       script_val_color_compose        )\
  VM_OP_SIMPLE_QUATERNARY(  ColorComposeHsv,    script_val_color_compose_hsv    )\
  VM_OP_SIMPLE_UNARY(       ColorFor,           script_val_color_for_val        )\
  VM_OP_SIMPLE_ZERO(        Random,             script_val_random               )\
  VM_OP_SIMPLE_ZERO(        RandomSphere,       script_val_random_sphere        )\
  VM_OP_SIMPLE_ZERO(        RandomCircleXZ,     script_val_random_circle_xz     )\
  VM_OP_SIMPLE_BINARY(      RandomBetween,      script_val_random_between       )\
  VM_OP_SIMPLE_UNARY(       RoundDown,          script_val_round_down           )\
  VM_OP_SIMPLE_UNARY(       RoundNearest,       script_val_round_nearest        )\
  VM_OP_SIMPLE_UNARY(       RoundUp,            script_val_round_up             )\
  VM_OP_SIMPLE_TERNARY(     Clamp,              script_val_clamp                )\
  VM_OP_SIMPLE_TERNARY(     Lerp,               script_val_lerp                 )\
  VM_OP_SIMPLE_BINARY(      Min,                script_val_min                  )\
  VM_OP_SIMPLE_BINARY(      Max,                script_val_max                  )\
  VM_OP_SIMPLE_UNARY(       Perlin3,            script_val_perlin3              )
// clang-format on

INLINE_HINT static bool prog_reg_valid(const u8 regId) { return regId < script_prog_regs; }

INLINE_HINT static bool prog_reg_set_valid(const u8 regId, const u8 regCount) {
  return regId + regCount <= script_prog_regs;
}

INLINE_HINT static bool prog_val_valid(const ScriptProgram* prog, const u8 valId) {
  return valId < prog->literals.count;
}

INLINE_HINT static u16 prog_read_u16(const u8 data[]) {
  // NOTE: Input data is not required to be aligned to 16 bit.
  return (u16)data[0] | (u16)data[1] << 8;
}

INLINE_HINT static u32 prog_read_u32(const u8 data[]) {
  // NOTE: Input data is not required to be aligned to 32 bit.
  return (u32)data[0] | (u32)data[1] << 8 | (u32)data[2] << 16 | (u32)data[3] << 24;
}

static bool prog_op_is_terminating(const ScriptOp op) {
  switch (op) {
  case ScriptOp_Fail:
  case ScriptOp_Return:
  case ScriptOp_ReturnNull:
    return true;
  default:
    return false;
  }
}

static i8 prog_compare_loc(const void* a, const void* b) {
  const ScriptProgramLoc* posA = a;
  const ScriptProgramLoc* posB = b;
  return compare_u16(&posA->instruction, &posB->instruction);
}

static ScriptRangeLineCol prog_loc(const ScriptProgram* prog, const u16 instruction) {

  const ScriptProgramLoc* r = search_binary_t(
      prog->locations.values,
      prog->locations.values + prog->locations.count,
      ScriptProgramLoc,
      prog_compare_loc,
      &(ScriptProgramLoc){.instruction = instruction});

  return r ? r->range : (ScriptRangeLineCol){0};
}

static ScriptRangeLineCol prog_loc_from_ip(const ScriptProgram* prog, const u8* ip) {
  return prog_loc(prog, (u16)(ip - mem_begin(prog->code)));
}

void script_prog_destroy(ScriptProgram* prog, Allocator* alloc) {
  if (prog->code.size && !prog->code.external) {
    alloc_free(alloc, mem_create(prog->code.ptr, prog->code.size));
  }
  if (prog->literals.count) {
    alloc_free_array_t(alloc, prog->literals.values, prog->literals.count);
  }
  if (prog->locations.count) {
    alloc_free_array_t(alloc, prog->locations.values, prog->locations.count);
  }
}

void script_prog_clear(ScriptProgram* prog, Allocator* alloc) {
  if (prog->code.size) {
    if (!prog->code.external) {
      alloc_free(alloc, mem_create(prog->code.ptr, prog->code.size));
    }
    prog->code.ptr  = null;
    prog->code.size = 0;
  }
  if (prog->literals.count) {
    alloc_free_array_t(alloc, prog->literals.values, prog->literals.count);
    prog->literals.values = null;
    prog->literals.count  = 0;
  }
  if (prog->locations.count) {
    alloc_free_array_t(alloc, prog->locations.values, prog->locations.count);
    prog->locations.values = null;
    prog->locations.count  = 0;
  }
}

ScriptProgResult script_prog_eval(
    const ScriptProgram* prog, ScriptMem* m, const ScriptBinder* binder, void* bindCtx) {

  const u8* ip = mem_begin(prog->code);

  ScriptProgResult res                    = {0};
  ScriptVal        regs[script_prog_regs] = {0};

  // clang-format off

#define VM_NEXT(_OP_SIZE_) { ip += (_OP_SIZE_); goto Dispatch; }
#define VM_JUMP(_INSTRUCTION_) { ip = mem_begin(prog->code) + (_INSTRUCTION_); goto Dispatch; }
#define VM_RETURN(_VALUE_) { res.val = (_VALUE_); return res; }
#define VM_PANIC(_PANIC_) { res.panic = (_PANIC_); return res; }

Dispatch:
  if (UNLIKELY(res.executedOps++ == script_prog_ops_max)) {
    VM_PANIC(((ScriptPanic){ScriptPanic_ExecutionLimitExceeded, .range = prog_loc_from_ip(prog, ip)}));
  }
  switch ((ScriptOp)ip[0]) {
  case ScriptOp_Fail:
    VM_PANIC(((ScriptPanic){ScriptPanic_AssertionFailed, .range = prog_loc_from_ip(prog, ip)}));
  case ScriptOp_Assert:
    if (UNLIKELY(script_falsy(regs[ip[1]]))) {
      VM_PANIC(((ScriptPanic){ScriptPanic_ExecutionFailed, .range = prog_loc_from_ip(prog, ip)}));
    }
    regs[ip[1]] = val_null();
    VM_NEXT(2);
  case ScriptOp_Return:
    VM_RETURN(regs[ip[1]]);
  case ScriptOp_ReturnNull:
    VM_RETURN(val_null());
  case ScriptOp_Move:
    regs[ip[1]] = regs[ip[2]];
    VM_NEXT(3);
  case ScriptOp_Jump:
    VM_JUMP(prog_read_u16(&ip[1]));
  case ScriptOp_JumpIfTruthy:
    if (script_truthy(regs[ip[1]])) {
      VM_JUMP(prog_read_u16(&ip[2]));
    }
    VM_NEXT(4);
  case ScriptOp_JumpIfFalsy:
    if (script_falsy(regs[ip[1]])) {
      VM_JUMP(prog_read_u16(&ip[2]));
    }
    VM_NEXT(4);
  case ScriptOp_JumpIfNonNull:
    if (script_non_null(regs[ip[1]])) {
      VM_JUMP(prog_read_u16(&ip[2]));
    }
    VM_NEXT(4);
  case ScriptOp_Value:
    regs[ip[1]] = prog->literals.values[ip[2]];
    VM_NEXT(3);
  case ScriptOp_ValueNull:
    regs[ip[1]] = val_null();
    VM_NEXT(2);
  case ScriptOp_ValueBool:
    regs[ip[1]] = val_bool(ip[2] != 0);
    VM_NEXT(3);
  case ScriptOp_ValueSmallInt:
    regs[ip[1]] = val_num(ip[2]);
    VM_NEXT(3);
  case ScriptOp_MemLoad:
    regs[ip[1]] = script_mem_load(m, prog_read_u32(&ip[2]));
    VM_NEXT(6);
  case ScriptOp_MemStore:
    script_mem_store(m, prog_read_u32(&ip[2]), regs[ip[1]]);
    VM_NEXT(6);
  case ScriptOp_MemLoadDyn:
    if(val_type(regs[ip[1]]) == ScriptType_Str) {
      regs[ip[1]] = script_mem_load(m, val_as_str(regs[ip[1]]));
    } else {
      regs[ip[1]] = val_null();
    }
    VM_NEXT(2);
  case ScriptOp_MemStoreDyn:
    if(val_type(regs[ip[1]]) == ScriptType_Str) {
      script_mem_store(m, val_as_str(regs[ip[1]]), regs[ip[2]]);
      regs[ip[1]] = regs[ip[2]];
    } else {
      regs[ip[1]] = val_null();
    }
    VM_NEXT(3);
  case ScriptOp_Extern: {
    ScriptBinderCall call = {
      .args     = &regs[ip[4]],
      .argCount = ip[5],
      .callId   = (u32)(ip - mem_begin(prog->code)),
    };
    regs[ip[1]] = script_binder_exec(binder, prog_read_u16(&ip[2]), bindCtx, &call);
  if (UNLIKELY(script_call_panicked(&call))) {
      call.panic.range = prog_loc_from_ip(prog, ip);
      VM_PANIC(call.panic);
    }
    VM_NEXT(6);
  }
#define VM_OP_SIMPLE_ZERO(_OP_, _FUNC_)                                                            \
  case ScriptOp_##_OP_:                                                                            \
    regs[ip[1]] = _FUNC_();                                                                        \
    VM_NEXT(2)
#define VM_OP_SIMPLE_UNARY(_OP_, _FUNC_)                                                           \
  case ScriptOp_##_OP_:                                                                            \
    regs[ip[1]] = _FUNC_(regs[ip[1]]);                                                             \
    VM_NEXT(2)
#define VM_OP_SIMPLE_BINARY(_OP_, _FUNC_)                                                          \
  case ScriptOp_##_OP_:                                                                            \
    regs[ip[1]] = _FUNC_(regs[ip[1]], regs[ip[2]]);                                                \
    VM_NEXT(3)
#define VM_OP_SIMPLE_TERNARY(_OP_, _FUNC_)                                                         \
  case ScriptOp_##_OP_:                                                                            \
    regs[ip[1]] = _FUNC_(regs[ip[1]], regs[ip[2]], regs[ip[3]]);                                   \
    VM_NEXT(4)
#define VM_OP_SIMPLE_QUATERNARY(_OP_, _FUNC_)                                                      \
  case ScriptOp_##_OP_:                                                                            \
    regs[ip[1]] = _FUNC_(regs[ip[1]], regs[ip[2]], regs[ip[3]], regs[ip[4]]);                      \
    VM_NEXT(5)

  VM_VISIT_OP_SIMPLE

#undef VM_OP_SIMPLE_QUATERNARY
#undef VM_OP_SIMPLE_TERNARY
#undef VM_OP_SIMPLE_BINARY
#undef VM_OP_SIMPLE_UNARY
#undef VM_OP_SIMPLE_ZERO
  }
  UNREACHABLE
  // clang-format on

#undef VM_NEXT
#undef VM_JUMP
#undef VM_PANIC
#undef VM_RETURN
}

bool script_prog_validate(const ScriptProgram* prog, const ScriptBinder* binder) {
  // Validate literals.
  for (usize i = 0; i != prog->literals.count; ++i) {
    if (UNLIKELY(!script_val_valid(prog->literals.values[i]))) {
      return false;
    }
  }

  // Validate code.
  if (UNLIKELY(!prog->code.size || prog->code.size > u16_max)) {
    return false;
  }
  if (UNLIKELY(!prog_op_is_terminating(mem_end(prog->code)[-1]))) {
    return false;
  }
  const u8* ip    = mem_begin(prog->code);
  const u8* ipEnd = mem_end(prog->code);
  while (ip != ipEnd) {
    // clang-format off
    switch ((ScriptOp)ip[0]) {
    case ScriptOp_Fail:
      if (UNLIKELY((ip += 1) > ipEnd)) return false;
      continue;
    case ScriptOp_Assert:
      if (UNLIKELY((ip += 2) > ipEnd)) return false;
      if (UNLIKELY(!prog_reg_valid(ip[-1]))) return false;
      continue;
    case ScriptOp_Return:
      if (UNLIKELY((ip += 2) > ipEnd)) return false;
      if (UNLIKELY(!prog_reg_valid(ip[-1]))) return false;
      continue;
    case ScriptOp_ReturnNull:
      if (UNLIKELY((ip += 1) > ipEnd)) return false;
      continue;
    case ScriptOp_Move:
      if (UNLIKELY((ip += 3) > ipEnd)) return false;
      if (UNLIKELY(!prog_reg_valid(ip[-2]))) return false;
      if (UNLIKELY(!prog_reg_valid(ip[-1]))) return false;
      continue;
    case ScriptOp_Jump: {
      if (UNLIKELY((ip += 3) > ipEnd)) return false;
      const u16 ipOffset = prog_read_u16(&ip[-2]);
      if (UNLIKELY(ipOffset >= (prog->code.size - 1))) return false;
    } continue;
    case ScriptOp_JumpIfTruthy: {
      if (UNLIKELY((ip += 4) > ipEnd)) return false;
      if (UNLIKELY(!prog_reg_valid(ip[-3]))) return false;
      const u16 ipOffset = prog_read_u16(&ip[-2]);
      if (UNLIKELY(ipOffset >= (prog->code.size - 1))) return false;
    } continue;
    case ScriptOp_JumpIfFalsy: {
      if (UNLIKELY((ip += 4) > ipEnd)) return false;
      if (UNLIKELY(!prog_reg_valid(ip[-3]))) return false;
      const u16 ipOffset = prog_read_u16(&ip[-2]);
      if (UNLIKELY(ipOffset >= (prog->code.size - 1))) return false;
    } continue;
    case ScriptOp_JumpIfNonNull:
      if (UNLIKELY((ip += 4) > ipEnd)) return false;
      if (UNLIKELY(!prog_reg_valid(ip[-3]))) return false;
      const u16 ipOffset = prog_read_u16(&ip[-2]);
      if (UNLIKELY(ipOffset >= (prog->code.size - 1))) return false;
      continue;
    case ScriptOp_Value:
      if (UNLIKELY((ip += 3) > ipEnd)) return false;
      if (UNLIKELY(!prog_reg_valid(ip[-2]))) return false;
      if (UNLIKELY(!prog_val_valid(prog, ip[-1]))) return false;
      continue;
    case ScriptOp_ValueNull:
      if (UNLIKELY((ip += 2) > ipEnd)) return false;
      if (UNLIKELY(!prog_reg_valid(ip[-1]))) return false;
      continue;
    case ScriptOp_ValueBool:
      if (UNLIKELY((ip += 3) > ipEnd)) return false;
      if (UNLIKELY(!prog_reg_valid(ip[-2]))) return false;
      continue;
    case ScriptOp_ValueSmallInt:
      if (UNLIKELY((ip += 3) > ipEnd)) return false;
      if (UNLIKELY(!prog_reg_valid(ip[-2]))) return false;
      continue;
    case ScriptOp_MemLoad:
      if (UNLIKELY((ip += 6) > ipEnd)) return false;
      if (UNLIKELY(!prog_reg_valid(ip[-5]))) return false;
      continue;
    case ScriptOp_MemStore:
      if (UNLIKELY((ip += 6) > ipEnd)) return false;
      if (UNLIKELY(!prog_reg_valid(ip[-5]))) return false;
      continue;
    case ScriptOp_MemLoadDyn:
      if (UNLIKELY((ip += 2) > ipEnd)) return false;
      if (UNLIKELY(!prog_reg_valid(ip[-1]))) return false;
      continue;
    case ScriptOp_MemStoreDyn:
      if (UNLIKELY((ip += 3) > ipEnd)) return false;
      if (UNLIKELY(!prog_reg_valid(ip[-2]))) return false;
      if (UNLIKELY(!prog_reg_valid(ip[-1]))) return false;
      continue;
    case ScriptOp_Extern: {
      if (UNLIKELY((ip += 6) > ipEnd)) return false;
      if (UNLIKELY(!prog_reg_valid(ip[-5]))) return false;
      if (UNLIKELY(!prog_reg_set_valid(ip[-2], ip[-1]))) return false;
      const ScriptBinderSlot funcSlot = prog_read_u16(&ip[-4]);
      if (UNLIKELY(!binder)) return false;
      if (UNLIKELY(funcSlot >= script_binder_count(binder))) return false;
    } continue;
#define VM_OP_SIMPLE_ZERO(_OP_, _FUNC_)                                                            \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 2) > ipEnd)) return false;                                               \
      if (UNLIKELY(!prog_reg_valid(ip[-1]))) return false;                                         \
      continue;
#define VM_OP_SIMPLE_UNARY(_OP_, _FUNC_)                                                           \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 2) > ipEnd)) return false;                                               \
      if (UNLIKELY(!prog_reg_valid(ip[-1]))) return false;                                         \
      continue;
#define VM_OP_SIMPLE_BINARY(_OP_, _FUNC_)                                                          \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 3) > ipEnd)) return false;                                               \
      if (UNLIKELY(!prog_reg_valid(ip[-2]))) return false;                                         \
      if (UNLIKELY(!prog_reg_valid(ip[-1]))) return false;                                         \
      continue;
#define VM_OP_SIMPLE_TERNARY(_OP_, _FUNC_)                                                         \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 4) > ipEnd)) return false;                                               \
      if (UNLIKELY(!prog_reg_valid(ip[-3]))) return false;                                         \
      if (UNLIKELY(!prog_reg_valid(ip[-2]))) return false;                                         \
      if (UNLIKELY(!prog_reg_valid(ip[-1]))) return false;                                         \
      continue;
#define VM_OP_SIMPLE_QUATERNARY(_OP_, _FUNC_)                                                      \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 5) > ipEnd)) return false;                                               \
      if (UNLIKELY(!prog_reg_valid(ip[-4]))) return false;                                         \
      if (UNLIKELY(!prog_reg_valid(ip[-3]))) return false;                                         \
      if (UNLIKELY(!prog_reg_valid(ip[-2]))) return false;                                         \
      if (UNLIKELY(!prog_reg_valid(ip[-1]))) return false;                                         \
      continue;

    VM_VISIT_OP_SIMPLE

#undef VM_OP_SIMPLE_QUATERNARY
#undef VM_OP_SIMPLE_TERNARY
#undef VM_OP_SIMPLE_BINARY
#undef VM_OP_SIMPLE_UNARY
#undef VM_OP_SIMPLE_ZERO
    }
    // clang-format on
    return false; // Unknown op-code.
  }
  return true;
}

ScriptRangeLineCol script_prog_location(const ScriptProgram* prog, const u32 callId) {
  diag_assert(callId < u16_max);
  return prog_loc(prog, (u16)callId);
}

void script_prog_write(const ScriptProgram* prog, DynString* out) {
  const u8* ipBegin = mem_begin(prog->code);
  const u8* ipEnd   = mem_end(prog->code);
  const u8* ip      = ipBegin;
  while (ip != ipEnd) {
    // clang-format off
    fmt_write(out, "[{}] ", fmt_int((uptr)(ip - ipBegin), .base = 16, .minDigits = 4));
    switch ((ScriptOp)ip[0]) {
    case ScriptOp_Fail:
      if (UNLIKELY((ip += 1) > ipEnd)) return;
      fmt_write(out, "Fail\n");
      break;
    case ScriptOp_Assert:
      if (UNLIKELY((ip += 2) > ipEnd)) return;
      fmt_write(out, "Assert r{}\n", fmt_int(ip[-1]));
      break;
    case ScriptOp_Return:
      if (UNLIKELY((ip += 2) > ipEnd)) return;
      fmt_write(out, "Return r{}\n", fmt_int(ip[-1]));
      break;
    case ScriptOp_ReturnNull:
      if (UNLIKELY((ip += 1) > ipEnd)) return;
      fmt_write(out, "ReturnNull\n");
      break;
    case ScriptOp_Move:
      if (UNLIKELY((ip += 3) > ipEnd)) return;
      fmt_write(out, "Move r{} r{}\n", fmt_int(ip[-2]), fmt_int(ip[-1]));
      break;
    case ScriptOp_Jump:
      if (UNLIKELY((ip += 3) > ipEnd)) return;
      fmt_write(out, "Jump i{}\n", fmt_int(prog_read_u16(&ip[-2]),.base = 16, .minDigits = 4));
      break;
    case ScriptOp_JumpIfTruthy:
      if (UNLIKELY((ip += 4) > ipEnd)) return;
      fmt_write(out, "JumpIfTruthy r{} i{}\n", fmt_int(ip[-3]), fmt_int(prog_read_u16(&ip[-2]),.base = 16, .minDigits = 4));
      break;
    case ScriptOp_JumpIfFalsy:
      if (UNLIKELY((ip += 4) > ipEnd)) return;
      fmt_write(out, "JumpIfFalsy r{} i{}\n", fmt_int(ip[-3]), fmt_int(prog_read_u16(&ip[-2]),.base = 16, .minDigits = 4));
      break;
    case ScriptOp_JumpIfNonNull:
      if (UNLIKELY((ip += 4) > ipEnd)) return;
      fmt_write(out, "JumpIfNonNull r{} i{}\n", fmt_int(ip[-3]), fmt_int(prog_read_u16(&ip[-2]),.base = 16, .minDigits = 4));
      break;
    case ScriptOp_Value: {
      if (UNLIKELY((ip += 3) > ipEnd)) return;
      if (UNLIKELY(!prog_val_valid(prog, ip[-1]))) return;
      const ScriptVal val = prog->literals.values[ip[-1]];
      fmt_write(out, "Value r{} v{} '{}'\n", fmt_int(ip[-2]), fmt_int(ip[-1]), script_val_fmt(val));
    } break;
    case ScriptOp_ValueNull:
      if (UNLIKELY((ip += 2) > ipEnd)) return;
      fmt_write(out, "ValueNull r{}\n", fmt_int(ip[-1]));
      break;
    case ScriptOp_ValueBool:
      if (UNLIKELY((ip += 3) > ipEnd)) return;
      fmt_write(out, "ValueBool r{} '{}'\n", fmt_int(ip[-2]), fmt_bool(ip[-1]));
      break;
    case ScriptOp_ValueSmallInt:
      if (UNLIKELY((ip += 3) > ipEnd)) return;
      fmt_write(out, "ValueSmallInt r{} '{}'\n", fmt_int(ip[-2]), fmt_int(ip[-1]));
      break;
    case ScriptOp_MemLoad: {
      if (UNLIKELY((ip += 6) > ipEnd)) return;
      const String keyName = stringtable_lookup(g_stringtable, prog_read_u32(&ip[-4]));
      fmt_write(out, "MemLoad r{} ${}", fmt_int(ip[-5]), fmt_int(prog_read_u32(&ip[-4])));
      if (!string_is_empty(keyName)) {
        fmt_write(out, " '{}'", fmt_text(keyName));
      }
      dynstring_append_char(out, '\n');
    } break;
    case ScriptOp_MemStore: {
      if (UNLIKELY((ip += 6) > ipEnd)) return;
      const String keyName = stringtable_lookup(g_stringtable, prog_read_u32(&ip[-4]));
      fmt_write(out, "MemStore r{} ${}", fmt_int(ip[-5]), fmt_int(prog_read_u32(&ip[-4])));
      if (!string_is_empty(keyName)) {
        fmt_write(out, " '{}'", fmt_text(keyName));
      }
      dynstring_append_char(out, '\n');
    } break;
    case ScriptOp_MemLoadDyn:
      if (UNLIKELY((ip += 2) > ipEnd)) return;
      fmt_write(out, "MemLoadDyn r{}\n", fmt_int(ip[-1]));
      break;
    case ScriptOp_MemStoreDyn:
      if (UNLIKELY((ip += 3) > ipEnd)) return;
      fmt_write(out, "MemStoreDyn r{} r{}\n", fmt_int(ip[-2]), fmt_int(ip[-1]));
      break;
    case ScriptOp_Extern:
      if (UNLIKELY((ip += 6) > ipEnd)) return;
      fmt_write(out, "Extern r{} f{} r{} c{}\n", fmt_int(ip[-5]), fmt_int(prog_read_u16(&ip[-4])), fmt_int(ip[-2]), fmt_int(ip[-1]));
      break;
#define VM_OP_SIMPLE_ZERO(_OP_, _FUNC_)                                                            \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 2) > ipEnd)) return;                                                     \
      fmt_write(out, #_OP_ " r{}\n", fmt_int(ip[-1]));                                             \
      break;
#define VM_OP_SIMPLE_UNARY(_OP_, _FUNC_)                                                           \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 2) > ipEnd)) return;                                                     \
      fmt_write(out, #_OP_ " r{}\n", fmt_int(ip[-1]));                                             \
      break;
#define VM_OP_SIMPLE_BINARY(_OP_, _FUNC_)                                                          \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 3) > ipEnd)) return;                                                     \
      fmt_write(out, #_OP_ " r{} r{}\n", fmt_int(ip[-2]), fmt_int(ip[-1]));                        \
      break;
#define VM_OP_SIMPLE_TERNARY(_OP_, _FUNC_)                                                         \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 4) > ipEnd)) return;                                                     \
      fmt_write(out, #_OP_ " r{} r{} r{}\n",                                                       \
        fmt_int(ip[-3]), fmt_int(ip[-2]), fmt_int(ip[-1]));                                        \
      break;
#define VM_OP_SIMPLE_QUATERNARY(_OP_, _FUNC_)                                                      \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 5) > ipEnd)) return;                                                     \
      fmt_write(out, #_OP_ " r{} r{} r{} r{}\n",                                                   \
        fmt_int(ip[-4]), fmt_int(ip[-3]), fmt_int(ip[-2]), fmt_int(ip[-1]));                       \
      break;

    VM_VISIT_OP_SIMPLE

#undef VM_OP_SIMPLE_QUATERNARY
#undef VM_OP_SIMPLE_TERNARY
#undef VM_OP_SIMPLE_BINARY
#undef VM_OP_SIMPLE_UNARY
#undef VM_OP_SIMPLE_ZERO
    }
    // clang-format on
  }
}

String script_prog_write_scratch(const ScriptProgram* prog) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte * 16, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_prog_write(prog, &buffer);

  return dynstring_view(&buffer);
}
