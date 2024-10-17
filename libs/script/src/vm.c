#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_stringtable.h"
#include "script_binder.h"
#include "script_error.h"
#include "script_mem.h"
#include "script_vm.h"

#include "doc_internal.h"
#include "val_internal.h"

#define script_vm_ops_max 25000

INLINE_HINT static bool vm_reg_valid(const u8 regId) { return regId < script_vm_regs; }

INLINE_HINT static bool vm_reg_set_valid(const u8 regId, const u8 regCount) {
  return regId + regCount <= script_vm_regs;
}

INLINE_HINT static bool vm_val_valid(const ScriptDoc* doc, const u8 valId) {
  return valId < doc->values.size;
}

INLINE_HINT static u16 vm_read_u16(const u8 data[]) {
  // NOTE: Input data is not required to be aligned to 16 bit.
  return (u16)data[0] | (u16)data[1] << 8;
}

INLINE_HINT static u32 vm_read_u32(const u8 data[]) {
  // NOTE: Input data is not required to be aligned to 32 bit.
  return (u32)data[0] | (u32)data[1] << 8 | (u32)data[2] << 16 | (u32)data[3] << 24;
}

ScriptVmResult script_vm_eval(
    const ScriptDoc*    doc,
    const String        code,
    ScriptMem*          m,
    const ScriptBinder* binder,
    void*               bindCtx) {
  if (binder) {
    diag_assert_msg(script_binder_hash(binder) == doc->binderHash, "Incompatible binder");
  }

  ScriptVmResult res;
  res.executedOps = 0;

  ScriptVal regs[script_vm_regs];
  for (const u8* ip = mem_begin(code);;) {
    if (UNLIKELY(res.executedOps++ == script_vm_ops_max)) {
      res.panic = (ScriptPanic){.kind = ScriptPanic_ExecutionLimitExceeded};
      res.val   = val_null();
      return res;
    }
    // clang-format off
    switch ((ScriptOp)*ip) {
    case ScriptOp_Fail:
      res.panic = (ScriptPanic){.kind = ScriptPanic_ExecutionFailed};
      res.val = val_null();
      return res;
    case ScriptOp_Assert: ip += 2;
      if (script_falsy(regs[ip[-1]])) {
        res.panic = (ScriptPanic){.kind = ScriptPanic_AssertionFailed};
        res.val = val_null();
        return res;
      }
      regs[ip[-1]] = val_null();
      continue;
    case ScriptOp_Return: ip += 2;
      res.panic = (ScriptPanic){0};
      res.val = regs[ip[-1]];
      return res;
    case ScriptOp_ReturnNull:
      res.panic = (ScriptPanic){0};
      res.val = val_null();
      return res;
    case ScriptOp_Move: ip += 3;
      regs[ip[-2]] = regs[ip[-1]];
      continue;
    case ScriptOp_Jump: ip += 3;
      ip = mem_begin(code) + vm_read_u16(&ip[-2]);
      continue;
    case ScriptOp_JumpIfTruthy: ip += 4;
      if (script_truthy(regs[ip[-3]])) {
        ip = mem_begin(code) + vm_read_u16(&ip[-2]);
      }
      continue;
    case ScriptOp_JumpIfFalsy: ip += 4;
      if (script_falsy(regs[ip[-3]])) {
        ip = mem_begin(code) + vm_read_u16(&ip[-2]);
      }
      continue;
    case ScriptOp_JumpIfNonNull: ip += 4;
      if (script_non_null(regs[ip[-3]])) {
        ip = mem_begin(code) + vm_read_u16(&ip[-2]);
      }
      continue;
    case ScriptOp_Value: ip += 3;
      regs[ip[-2]] = dynarray_begin_t(&doc->values, ScriptVal)[ip[-1]];
      continue;
    case ScriptOp_ValueNull: ip += 2;
      regs[ip[-1]] = val_null();
      continue;
    case ScriptOp_ValueBool: ip += 3;
      regs[ip[-2]] = val_bool(ip[-1] != 0);
      continue;
    case ScriptOp_ValueSmallInt: ip += 3;
      regs[ip[-2]] = val_num(ip[-1]);
      continue;
    case ScriptOp_MemLoad: ip += 6;
      regs[ip[-5]] = script_mem_load(m, vm_read_u32(&ip[-4]));
      continue;
    case ScriptOp_MemStore: ip += 6;
      script_mem_store(m, vm_read_u32(&ip[-4]), regs[ip[-5]]);
      continue;
    case ScriptOp_MemLoadDyn: ip += 2;
      if(val_type(regs[ip[-1]]) == ScriptType_Str) {
        regs[ip[-1]] = script_mem_load(m, val_as_str(regs[ip[-1]]));
      } else {
        regs[ip[-1]] = val_null();
      }
      continue;
    case ScriptOp_MemStoreDyn: ip += 3;
      if(val_type(regs[ip[-2]]) == ScriptType_Str) {
        script_mem_store(m, val_as_str(regs[ip[-2]]), regs[ip[-1]]);
        regs[ip[-2]] = regs[ip[-1]];
      } else {
        regs[ip[-2]] = val_null();
      }
      continue;
    case ScriptOp_Extern: ip += 6;
      const ScriptBinderSlot funcSlot = vm_read_u16(&ip[-4]);
      const ScriptArgs args = {.values = &regs[ip[-2]], .count = ip[-1]};
      ScriptError err = {0};
      regs[ip[-5]] = script_binder_exec(binder, funcSlot, bindCtx, args, &err);
      if (UNLIKELY(err.kind)) {
        res.panic = (ScriptPanic){.kind = script_error_to_panic(err.kind)};
        res.val = val_null();
        return res;
      }
      continue;
#define OP_SIMPLE_ZERO(_OP_, _FUNC_)                                                               \
    case ScriptOp_##_OP_: ip += 2;                                                                 \
      regs[ip[-1]] = _FUNC_();                                                                     \
      continue
#define OP_SIMPLE_UNARY(_OP_, _FUNC_)                                                              \
    case ScriptOp_##_OP_: ip += 2;                                                                 \
      regs[ip[-1]] = _FUNC_(regs[ip[-1]]);                                                         \
      continue
#define OP_SIMPLE_BINARY(_OP_, _FUNC_)                                                             \
    case ScriptOp_##_OP_: ip += 3;                                                                 \
      regs[ip[-2]] = _FUNC_(regs[ip[-2]], regs[ip[-1]]);                                           \
      continue
#define OP_SIMPLE_TERNARY(_OP_, _FUNC_)                                                            \
    case ScriptOp_##_OP_: ip += 4;                                                                 \
      regs[ip[-3]] = _FUNC_(regs[ip[-3]], regs[ip[-2]], regs[ip[-1]]);                             \
      continue
#define OP_SIMPLE_QUATERNARY(_OP_, _FUNC_)                                                         \
    case ScriptOp_##_OP_: ip += 5;                                                                 \
      regs[ip[-4]] = _FUNC_(regs[ip[-4]], regs[ip[-3]], regs[ip[-2]], regs[ip[-1]]);               \
      continue

    OP_SIMPLE_UNARY(Truthy,               script_truthy_as_val);
    OP_SIMPLE_UNARY(Falsy,                script_falsy_as_val);
    OP_SIMPLE_UNARY(NonNull,              script_non_null_as_val);
    OP_SIMPLE_UNARY(Type,                 script_val_type);
    OP_SIMPLE_UNARY(Hash,                 script_val_hash);
    OP_SIMPLE_BINARY(Equal,               script_val_equal_as_val);
    OP_SIMPLE_BINARY(Less,                script_val_less_as_val);
    OP_SIMPLE_BINARY(Greater,             script_val_greater_as_val);
    OP_SIMPLE_BINARY(Add,                 script_val_add);
    OP_SIMPLE_BINARY(Sub,                 script_val_sub);
    OP_SIMPLE_BINARY(Mul,                 script_val_mul);
    OP_SIMPLE_BINARY(Div,                 script_val_div);
    OP_SIMPLE_BINARY(Mod,                 script_val_mod);
    OP_SIMPLE_UNARY(Negate,               script_val_neg);
    OP_SIMPLE_UNARY(Invert,               script_val_inv);
    OP_SIMPLE_BINARY(Distance,            script_val_dist);
    OP_SIMPLE_BINARY(Angle,               script_val_angle);
    OP_SIMPLE_UNARY(Sin,                  script_val_sin);
    OP_SIMPLE_UNARY(Cos,                  script_val_cos);
    OP_SIMPLE_UNARY(Normalize,            script_val_norm);
    OP_SIMPLE_UNARY(Magnitude,            script_val_mag);
    OP_SIMPLE_UNARY(Absolute,             script_val_abs);
    OP_SIMPLE_UNARY(VecX,                 script_val_vec_x);
    OP_SIMPLE_UNARY(VecY,                 script_val_vec_y);
    OP_SIMPLE_UNARY(VecZ,                 script_val_vec_z);
    OP_SIMPLE_TERNARY(Vec3Compose,        script_val_vec3_compose);
    OP_SIMPLE_TERNARY(QuatFromEuler,      script_val_quat_from_euler);
    OP_SIMPLE_BINARY(QuatFromAngleAxis,   script_val_quat_from_angle_axis);
    OP_SIMPLE_QUATERNARY(ColorCompose,    script_val_color_compose);
    OP_SIMPLE_QUATERNARY(ColorComposeHsv, script_val_color_compose_hsv);
    OP_SIMPLE_UNARY(ColorFor,             script_val_color_for_val);
    OP_SIMPLE_ZERO(Random,                script_val_random);
    OP_SIMPLE_ZERO(RandomSphere,          script_val_random_sphere);
    OP_SIMPLE_ZERO(RandomCircleXZ,        script_val_random_circle_xz);
    OP_SIMPLE_BINARY(RandomBetween,       script_val_random_between);
    OP_SIMPLE_UNARY(RoundDown,            script_val_round_down);
    OP_SIMPLE_UNARY(RoundNearest,         script_val_round_nearest);
    OP_SIMPLE_UNARY(RoundUp,              script_val_round_up);
    OP_SIMPLE_TERNARY(Clamp,              script_val_clamp);
    OP_SIMPLE_TERNARY(Lerp,               script_val_lerp);
    OP_SIMPLE_BINARY(Min,                 script_val_min);
    OP_SIMPLE_BINARY(Max,                 script_val_max);
    OP_SIMPLE_UNARY(Perlin3,              script_val_perlin3);

#undef OP_SIMPLE_QUATERNARY
#undef OP_SIMPLE_TERNARY
#undef OP_SIMPLE_BINARY
#undef OP_SIMPLE_UNARY
#undef OP_SIMPLE_ZERO
    }
    UNREACHABLE
  }
  // clang-format on
}

bool script_vm_validate(const ScriptDoc* doc, const String code, const ScriptBinder* binder) {
  if (UNLIKELY(code.size > u16_max)) {
    return false;
  }
  const u8* ip    = mem_begin(code);
  const u8* ipEnd = mem_end(code);
  while (ip != ipEnd) {
    // clang-format off
    switch ((ScriptOp)*ip) {
    case ScriptOp_Fail:
      if (UNLIKELY((ip += 1) > ipEnd)) return false;
      break;
    case ScriptOp_Assert:
      if (UNLIKELY((ip += 2) > ipEnd)) return false;
      if (UNLIKELY(!vm_reg_valid(ip[-1]))) return false;
      break;
    case ScriptOp_Return:
      if (UNLIKELY((ip += 2) > ipEnd)) return false;
      if (UNLIKELY(!vm_reg_valid(ip[-1]))) return false;
      break;
    case ScriptOp_ReturnNull:
      if (UNLIKELY((ip += 1) > ipEnd)) return false;
      break;
    case ScriptOp_Move:
      if (UNLIKELY((ip += 3) > ipEnd)) return false;
      if (UNLIKELY(!vm_reg_valid(ip[-2]))) return false;
      if (UNLIKELY(!vm_reg_valid(ip[-1]))) return false;
      break;
    case ScriptOp_Jump: {
      if (UNLIKELY((ip += 3) > ipEnd)) return false;
      const u16 ipOffset = vm_read_u16(&ip[-2]);
      if (UNLIKELY(ipOffset >= (code.size - 1))) return false;
    } break;
    case ScriptOp_JumpIfTruthy: {
      if (UNLIKELY((ip += 4) > ipEnd)) return false;
      if (UNLIKELY(!vm_reg_valid(ip[-3]))) return false;
      const u16 ipOffset = vm_read_u16(&ip[-2]);
      if (UNLIKELY(ipOffset >= (code.size - 1))) return false;
    } break;
    case ScriptOp_JumpIfFalsy: {
      if (UNLIKELY((ip += 4) > ipEnd)) return false;
      if (UNLIKELY(!vm_reg_valid(ip[-3]))) return false;
      const u16 ipOffset = vm_read_u16(&ip[-2]);
      if (UNLIKELY(ipOffset >= (code.size - 1))) return false;
    } break;
    case ScriptOp_JumpIfNonNull:
      if (UNLIKELY((ip += 4) > ipEnd)) return false;
      if (UNLIKELY(!vm_reg_valid(ip[-3]))) return false;
      const u16 ipOffset = vm_read_u16(&ip[-2]);
      if (UNLIKELY(ipOffset >= (code.size - 1))) return false;
      break;
    case ScriptOp_Value:
      if (UNLIKELY((ip += 3) > ipEnd)) return false;
      if (UNLIKELY(!vm_reg_valid(ip[-2]))) return false;
      if (UNLIKELY(!vm_val_valid(doc, ip[-1]))) return false;
      break;
    case ScriptOp_ValueNull:
      if (UNLIKELY((ip += 2) > ipEnd)) return false;
      if (UNLIKELY(!vm_reg_valid(ip[-1]))) return false;
      break;
    case ScriptOp_ValueBool:
      if (UNLIKELY((ip += 3) > ipEnd)) return false;
      if (UNLIKELY(!vm_reg_valid(ip[-2]))) return false;
      break;
    case ScriptOp_ValueSmallInt:
      if (UNLIKELY((ip += 3) > ipEnd)) return false;
      if (UNLIKELY(!vm_reg_valid(ip[-2]))) return false;
      break;
    case ScriptOp_MemLoad:
      if (UNLIKELY((ip += 6) > ipEnd)) return false;
      if (UNLIKELY(!vm_reg_valid(ip[-5]))) return false;
      break;
    case ScriptOp_MemStore:
      if (UNLIKELY((ip += 6) > ipEnd)) return false;
      if (UNLIKELY(!vm_reg_valid(ip[-5]))) return false;
      break;
    case ScriptOp_MemLoadDyn:
      if (UNLIKELY((ip += 2) > ipEnd)) return false;
      if (UNLIKELY(!vm_reg_valid(ip[-1]))) return false;
      break;
    case ScriptOp_MemStoreDyn:
      if (UNLIKELY((ip += 3) > ipEnd)) return false;
      if (UNLIKELY(!vm_reg_valid(ip[-2]))) return false;
      if (UNLIKELY(!vm_reg_valid(ip[-1]))) return false;
      break;
    case ScriptOp_Extern: {
      if (UNLIKELY((ip += 6) > ipEnd)) return false;
      if (UNLIKELY(!vm_reg_valid(ip[-5]))) return false;
      if (UNLIKELY(!vm_reg_set_valid(ip[-2], ip[-1]))) return false;
      const ScriptBinderSlot funcSlot = vm_read_u16(&ip[-4]);
      if (UNLIKELY(!binder)) return false;
      if (UNLIKELY(funcSlot >= script_binder_count(binder))) return false;
    } break;
#define OP_SIMPLE_ZERO(_OP_)                                                                       \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 2) > ipEnd)) return false;                                               \
      if (UNLIKELY(!vm_reg_valid(ip[-1]))) return false;                                           \
      break
#define OP_SIMPLE_UNARY(_OP_)                                                                      \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 2) > ipEnd)) return false;                                               \
      if (UNLIKELY(!vm_reg_valid(ip[-1]))) return false;                                           \
      break
#define OP_SIMPLE_BINARY(_OP_)                                                                     \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 3) > ipEnd)) return false;                                               \
      if (UNLIKELY(!vm_reg_valid(ip[-2]))) return false;                                           \
      if (UNLIKELY(!vm_reg_valid(ip[-1]))) return false;                                           \
      break
#define OP_SIMPLE_TERNARY(_OP_)                                                                    \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 4) > ipEnd)) return false;                                               \
      if (UNLIKELY(!vm_reg_valid(ip[-3]))) return false;                                           \
      if (UNLIKELY(!vm_reg_valid(ip[-2]))) return false;                                           \
      if (UNLIKELY(!vm_reg_valid(ip[-1]))) return false;                                           \
      break
#define OP_SIMPLE_QUATERNARY(_OP_)                                                                 \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 5) > ipEnd)) return false;                                               \
      if (UNLIKELY(!vm_reg_valid(ip[-4]))) return false;                                           \
      if (UNLIKELY(!vm_reg_valid(ip[-3]))) return false;                                           \
      if (UNLIKELY(!vm_reg_valid(ip[-2]))) return false;                                           \
      if (UNLIKELY(!vm_reg_valid(ip[-1]))) return false;                                           \
      break

    OP_SIMPLE_UNARY(Truthy);
    OP_SIMPLE_UNARY(Falsy);
    OP_SIMPLE_UNARY(NonNull);
    OP_SIMPLE_UNARY(Type);
    OP_SIMPLE_UNARY(Hash);
    OP_SIMPLE_BINARY(Equal);
    OP_SIMPLE_BINARY(Less);
    OP_SIMPLE_BINARY(Greater);
    OP_SIMPLE_BINARY(Add);
    OP_SIMPLE_BINARY(Sub);
    OP_SIMPLE_BINARY(Mul);
    OP_SIMPLE_BINARY(Div);
    OP_SIMPLE_BINARY(Mod);
    OP_SIMPLE_UNARY(Negate);
    OP_SIMPLE_UNARY(Invert);
    OP_SIMPLE_BINARY(Distance);
    OP_SIMPLE_BINARY(Angle);
    OP_SIMPLE_UNARY(Sin);
    OP_SIMPLE_UNARY(Cos);
    OP_SIMPLE_UNARY(Normalize);
    OP_SIMPLE_UNARY(Magnitude);
    OP_SIMPLE_UNARY(Absolute);
    OP_SIMPLE_UNARY(VecX);
    OP_SIMPLE_UNARY(VecY);
    OP_SIMPLE_UNARY(VecZ);
    OP_SIMPLE_TERNARY(Vec3Compose);
    OP_SIMPLE_TERNARY(QuatFromEuler);
    OP_SIMPLE_BINARY(QuatFromAngleAxis);
    OP_SIMPLE_QUATERNARY(ColorCompose);
    OP_SIMPLE_QUATERNARY(ColorComposeHsv);
    OP_SIMPLE_UNARY(ColorFor);
    OP_SIMPLE_ZERO(Random);
    OP_SIMPLE_ZERO(RandomSphere);
    OP_SIMPLE_ZERO(RandomCircleXZ);
    OP_SIMPLE_BINARY(RandomBetween);
    OP_SIMPLE_UNARY(RoundDown);
    OP_SIMPLE_UNARY(RoundNearest);
    OP_SIMPLE_UNARY(RoundUp);
    OP_SIMPLE_TERNARY(Clamp);
    OP_SIMPLE_TERNARY(Lerp);
    OP_SIMPLE_BINARY(Min);
    OP_SIMPLE_BINARY(Max);
    OP_SIMPLE_UNARY(Perlin3);

#undef OP_SIMPLE_QUATERNARY
#undef OP_SIMPLE_TERNARY
#undef OP_SIMPLE_BINARY
#undef OP_SIMPLE_UNARY
#undef OP_SIMPLE_ZERO
    }
    // clang-format on
  }
  return true;
}

void script_vm_disasm_write(const ScriptDoc* doc, const String code, DynString* out) {
  const u8* ip    = mem_begin(code);
  const u8* ipEnd = mem_end(code);
  while (ip != ipEnd) {
    // clang-format off
    fmt_write(out, "[{}] ", fmt_int((uptr)(ip - mem_begin(code)), .base = 16, .minDigits = 4));
    switch ((ScriptOp)*ip) {
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
      fmt_write(out, "Jump i{}\n", fmt_int(vm_read_u16(&ip[-2]),.base = 16, .minDigits = 4));
      break;
    case ScriptOp_JumpIfTruthy:
      if (UNLIKELY((ip += 4) > ipEnd)) return;
      fmt_write(out, "JumpIfTruthy r{} i{}\n", fmt_int(ip[-3]), fmt_int(vm_read_u16(&ip[-2]),.base = 16, .minDigits = 4));
      break;
    case ScriptOp_JumpIfFalsy:
      if (UNLIKELY((ip += 4) > ipEnd)) return;
      fmt_write(out, "JumpIfFalsy r{} i{}\n", fmt_int(ip[-3]), fmt_int(vm_read_u16(&ip[-2]),.base = 16, .minDigits = 4));
      break;
    case ScriptOp_JumpIfNonNull:
      if (UNLIKELY((ip += 4) > ipEnd)) return;
      fmt_write(out, "JumpIfNonNull r{} i{}\n", fmt_int(ip[-3]), fmt_int(vm_read_u16(&ip[-2]),.base = 16, .minDigits = 4));
      break;
    case ScriptOp_Value: {
      if (UNLIKELY((ip += 3) > ipEnd)) return;
      if (UNLIKELY(!vm_val_valid(doc, ip[-1]))) return;
      const ScriptVal val = dynarray_begin_t(&doc->values, ScriptVal)[ip[-1]];
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
      const String keyName = stringtable_lookup(g_stringtable, vm_read_u32(&ip[-4]));
      fmt_write(out, "MemLoad r{} ${}", fmt_int(ip[-5]), fmt_int(vm_read_u32(&ip[-4])));
      if (!string_is_empty(keyName)) {
        fmt_write(out, " '{}'", fmt_text(keyName));
      }
      dynstring_append_char(out, '\n');
    } break;
    case ScriptOp_MemStore: {
      if (UNLIKELY((ip += 6) > ipEnd)) return;
      const String keyName = stringtable_lookup(g_stringtable, vm_read_u32(&ip[-4]));
      fmt_write(out, "MemStore r{} ${}", fmt_int(ip[-5]), fmt_int(vm_read_u32(&ip[-4])));
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
      fmt_write(out, "Extern r{} f{} r{} c{}\n", fmt_int(ip[-5]), fmt_int(vm_read_u16(&ip[-4])), fmt_int(ip[-2]), fmt_int(ip[-1]));
      break;
#define OP_SIMPLE_ZERO(_OP_)                                                                       \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 2) > ipEnd)) return;                                                     \
      fmt_write(out, #_OP_ " r{}\n", fmt_int(ip[-1]));                                             \
      break
#define OP_SIMPLE_UNARY(_OP_)                                                                      \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 2) > ipEnd)) return;                                                     \
      fmt_write(out, #_OP_ " r{}\n", fmt_int(ip[-1]));                                             \
      break
#define OP_SIMPLE_BINARY(_OP_)                                                                     \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 3) > ipEnd)) return;                                                     \
      fmt_write(out, #_OP_ " r{} r{}\n", fmt_int(ip[-2]), fmt_int(ip[-1]));                        \
      break
#define OP_SIMPLE_TERNARY(_OP_)                                                                    \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 4) > ipEnd)) return;                                                     \
      fmt_write(out, #_OP_ " r{} r{} r{}\n",                                                       \
        fmt_int(ip[-3]), fmt_int(ip[-2]), fmt_int(ip[-1]));                                        \
      break
#define OP_SIMPLE_QUATERNARY(_OP_)                                                                 \
    case ScriptOp_##_OP_:                                                                          \
      if (UNLIKELY((ip += 5) > ipEnd)) return;                                                     \
      fmt_write(out, #_OP_ " r{} r{} r{} r{}\n",                                                   \
        fmt_int(ip[-4]), fmt_int(ip[-3]), fmt_int(ip[-2]), fmt_int(ip[-1]));                       \
      break

    OP_SIMPLE_UNARY(Truthy);
    OP_SIMPLE_UNARY(Falsy);
    OP_SIMPLE_UNARY(NonNull);
    OP_SIMPLE_UNARY(Type);
    OP_SIMPLE_UNARY(Hash);
    OP_SIMPLE_BINARY(Equal);
    OP_SIMPLE_BINARY(Less);
    OP_SIMPLE_BINARY(Greater);
    OP_SIMPLE_BINARY(Add);
    OP_SIMPLE_BINARY(Sub);
    OP_SIMPLE_BINARY(Mul);
    OP_SIMPLE_BINARY(Div);
    OP_SIMPLE_BINARY(Mod);
    OP_SIMPLE_UNARY(Negate);
    OP_SIMPLE_UNARY(Invert);
    OP_SIMPLE_BINARY(Distance);
    OP_SIMPLE_BINARY(Angle);
    OP_SIMPLE_UNARY(Sin);
    OP_SIMPLE_UNARY(Cos);
    OP_SIMPLE_UNARY(Normalize);
    OP_SIMPLE_UNARY(Magnitude);
    OP_SIMPLE_UNARY(Absolute);
    OP_SIMPLE_UNARY(VecX);
    OP_SIMPLE_UNARY(VecY);
    OP_SIMPLE_UNARY(VecZ);
    OP_SIMPLE_TERNARY(Vec3Compose);
    OP_SIMPLE_TERNARY(QuatFromEuler);
    OP_SIMPLE_BINARY(QuatFromAngleAxis);
    OP_SIMPLE_QUATERNARY(ColorCompose);
    OP_SIMPLE_QUATERNARY(ColorComposeHsv);
    OP_SIMPLE_UNARY(ColorFor);
    OP_SIMPLE_ZERO(Random);
    OP_SIMPLE_ZERO(RandomSphere);
    OP_SIMPLE_ZERO(RandomCircleXZ);
    OP_SIMPLE_BINARY(RandomBetween);
    OP_SIMPLE_UNARY(RoundDown);
    OP_SIMPLE_UNARY(RoundNearest);
    OP_SIMPLE_UNARY(RoundUp);
    OP_SIMPLE_TERNARY(Clamp);
    OP_SIMPLE_TERNARY(Lerp);
    OP_SIMPLE_BINARY(Min);
    OP_SIMPLE_BINARY(Max);
    OP_SIMPLE_UNARY(Perlin3);

#undef OP_SIMPLE_QUATERNARY
#undef OP_SIMPLE_TERNARY
#undef OP_SIMPLE_BINARY
#undef OP_SIMPLE_UNARY
#undef OP_SIMPLE_ZERO
    }
    // clang-format on
  }
}

String script_vm_disasm_scratch(const ScriptDoc* doc, const String code) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte * 16, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_vm_disasm_write(doc, code, &buffer);

  return dynstring_view(&buffer);
}
