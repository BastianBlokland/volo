#include "core/diag.h"
#include "core/time.h"
#include "script/args.h"
#include "script/binder.h"
#include "script/enum.h"
#include "script/panic.h"
#include "script/val.h"

#include "val_internal.h"

NORETURN static void arg_type_error(ScriptBinderCall* c, const u16 i, const ScriptMask mask) {
  script_panic_raise(
      c->panicHandler,
      (ScriptPanic){
          ScriptPanic_ArgumentTypeMismatch,
          .argIndex   = i,
          .typeMask   = mask,
          .typeActual = script_type(c->args[i]),
      });
}

INLINE_HINT static void arg_type_check(ScriptBinderCall* c, const u16 i, const ScriptMask mask) {
  if (UNLIKELY(c->argCount <= i)) {
    script_panic_raise(c->panicHandler, (ScriptPanic){ScriptPanic_ArgumentMissing, .argIndex = i});
  }
  if (UNLIKELY(!val_type_check(c->args[i], mask))) {
    script_panic_raise(
        c->panicHandler,
        (ScriptPanic){
            ScriptPanic_ArgumentTypeMismatch,
            .argIndex   = i,
            .typeMask   = mask,
            .typeActual = script_type(c->args[i]),
        });
  }
}

void script_arg_check(ScriptBinderCall* c, const u16 i, const ScriptMask mask) {
  arg_type_check(c, i, mask);
}

bool script_arg_has(ScriptBinderCall* c, const u16 i) {
  return c->argCount > i && val_type(c->args[i]) != ScriptType_Null;
}

void script_arg_shift(ScriptBinderCall* c) {
  diag_assert(c->argCount);
  ++c->args;
  --c->argCount;
}

ScriptVal script_arg_any(ScriptBinderCall* c, const u16 i) {
  if (UNLIKELY(c->argCount <= i)) {
    script_panic_raise(c->panicHandler, (ScriptPanic){ScriptPanic_ArgumentMissing, .argIndex = i});
  }
  return c->args[i];
}

f64 script_arg_num(ScriptBinderCall* c, const u16 i) {
  arg_type_check(c, i, script_mask_num);
  return val_as_num(c->args[i]);
}

f64 script_arg_num_range(ScriptBinderCall* c, const u16 i, const f64 min, const f64 max) {
  arg_type_check(c, i, script_mask_num);
  const f64 res = val_as_num(c->args[i]);
  if (LIKELY(res >= min && res <= max)) {
    return res;
  }
  script_panic_raise(c->panicHandler, (ScriptPanic){ScriptPanic_ArgumentOutOfRange, .argIndex = i});
}

bool script_arg_bool(ScriptBinderCall* c, const u16 i) {
  arg_type_check(c, i, script_mask_bool);
  return val_as_bool(c->args[i]);
}

GeoVector script_arg_vec3(ScriptBinderCall* c, const u16 i) {
  arg_type_check(c, i, script_mask_vec3);
  return val_as_vec3(c->args[i]);
}

GeoQuat script_arg_quat(ScriptBinderCall* c, const u16 i) {
  arg_type_check(c, i, script_mask_quat);
  return val_as_quat(c->args[i]);
}

GeoColor script_arg_color(ScriptBinderCall* c, const u16 i) {
  arg_type_check(c, i, script_mask_color);
  return val_as_color(c->args[i]);
}

EcsEntityId script_arg_entity(ScriptBinderCall* c, const u16 i) {
  arg_type_check(c, i, script_mask_entity);
  return val_as_entity(c->args[i]);
}

StringHash script_arg_str(ScriptBinderCall* c, const u16 i) {
  arg_type_check(c, i, script_mask_str);
  return val_as_str(c->args[i]);
}

TimeDuration script_arg_time(ScriptBinderCall* c, const u16 i) {
  arg_type_check(c, i, script_mask_num);
  return (TimeDuration)time_seconds(val_as_num(c->args[i]));
}

i32 script_arg_enum(ScriptBinderCall* c, const u16 i, const ScriptEnum* e) {
  arg_type_check(c, i, script_mask_str);
  return script_enum_lookup_value_at_index(e, val_as_str(c->args[i]), i, c->panicHandler);
}

ScriptType script_arg_opt_type(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(c->argCount > i)) {
    return val_type(c->args[i]);
  }
  return ScriptType_Null;
}

f64 script_arg_opt_num(ScriptBinderCall* c, const u16 i, const f64 def) {
  if (c->argCount > i) {
    if (val_type(c->args[i]) == ScriptType_Num) {
      return val_as_num(c->args[i]);
    }
    if (val_type(c->args[i]) == ScriptType_Null) {
      return def;
    }
    arg_type_error(c, i, script_mask_num | script_mask_null);
  }
  return def;
}

f64 script_arg_opt_num_range(
    ScriptBinderCall* c, const u16 i, const f64 min, const f64 max, const f64 def) {
  if (c->argCount > i) {
    if (val_type(c->args[i]) == ScriptType_Num) {
      const f64 res = val_as_num(c->args[i]);
      if (LIKELY(res >= min && res <= max)) {
        return res;
      }
      script_panic_raise(
          c->panicHandler, (ScriptPanic){ScriptPanic_ArgumentOutOfRange, .argIndex = i});
    }
    if (val_type(c->args[i]) == ScriptType_Null) {
      return def;
    }
    arg_type_error(c, i, script_mask_num | script_mask_null);
  }
  return def;
}

bool script_arg_opt_bool(ScriptBinderCall* c, const u16 i, const bool def) {
  if (c->argCount > i) {
    if (val_type(c->args[i]) == ScriptType_Bool) {
      return val_as_bool(c->args[i]);
    }
    if (val_type(c->args[i]) == ScriptType_Null) {
      return def;
    }
    arg_type_error(c, i, script_mask_bool | script_mask_null);
  }
  return def;
}

GeoVector script_arg_opt_vec3(ScriptBinderCall* c, const u16 i, const GeoVector def) {
  if (c->argCount > i) {
    if (val_type(c->args[i]) == ScriptType_Vec3) {
      return val_as_vec3(c->args[i]);
    }
    if (val_type(c->args[i]) == ScriptType_Null) {
      return def;
    }
    arg_type_error(c, i, script_mask_vec3 | script_mask_null);
  }
  return def;
}

GeoQuat script_arg_opt_quat(ScriptBinderCall* c, const u16 i, const GeoQuat def) {
  if (c->argCount > i) {
    if (val_type(c->args[i]) == ScriptType_Quat) {
      return val_as_quat(c->args[i]);
    }
    if (val_type(c->args[i]) == ScriptType_Null) {
      return def;
    }
    arg_type_error(c, i, script_mask_quat | script_mask_null);
  }
  return def;
}

GeoColor script_arg_opt_color(ScriptBinderCall* c, const u16 i, const GeoColor def) {
  if (c->argCount > i) {
    if (val_type(c->args[i]) == ScriptType_Color) {
      return val_as_color(c->args[i]);
    }
    if (val_type(c->args[i]) == ScriptType_Null) {
      return def;
    }
    arg_type_error(c, i, script_mask_color | script_mask_null);
  }
  return def;
}

EcsEntityId script_arg_opt_entity(ScriptBinderCall* c, const u16 i, const EcsEntityId def) {
  if (c->argCount > i) {
    if (val_type(c->args[i]) == ScriptType_Entity) {
      return val_as_entity(c->args[i]);
    }
    if (val_type(c->args[i]) == ScriptType_Null) {
      return def;
    }
    arg_type_error(c, i, script_mask_entity | script_mask_null);
  }
  return def;
}

StringHash script_arg_opt_str(ScriptBinderCall* c, const u16 i, const StringHash def) {
  if (c->argCount > i) {
    if (val_type(c->args[i]) == ScriptType_Str) {
      return val_as_str(c->args[i]);
    }
    if (val_type(c->args[i]) == ScriptType_Null) {
      return def;
    }
    arg_type_error(c, i, script_mask_str | script_mask_null);
  }
  return def;
}

TimeDuration script_arg_opt_time(ScriptBinderCall* c, const u16 i, const TimeDuration def) {
  if (c->argCount > i) {
    if (val_type(c->args[i]) == ScriptType_Num) {
      return (TimeDuration)time_seconds(val_as_num(c->args[i]));
    }
    if (val_type(c->args[i]) == ScriptType_Null) {
      return def;
    }
    arg_type_error(c, i, script_mask_num | script_mask_null);
  }
  return def;
}

i32 script_arg_opt_enum(ScriptBinderCall* c, const u16 i, const ScriptEnum* e, const i32 def) {
  if (c->argCount > i) {
    if (val_type(c->args[i]) == ScriptType_Str) {
      return script_enum_lookup_value(e, val_as_str(c->args[i]), c->panicHandler);
    }
    if (val_type(c->args[i]) == ScriptType_Null) {
      return def;
    }
    arg_type_error(c, i, script_mask_str | script_mask_null);
  }
  return def;
}

f64 script_arg_maybe_num(ScriptBinderCall* c, const u16 i, const f64 def) {
  if (c->argCount > i && val_type(c->args[i]) == ScriptType_Num) {
    return val_as_num(c->args[i]);
  }
  return def;
}

bool script_arg_maybe_bool(ScriptBinderCall* c, const u16 i, const bool def) {
  if (c->argCount > i && val_type(c->args[i]) == ScriptType_Bool) {
    return val_as_bool(c->args[i]);
  }
  return def;
}

GeoVector script_arg_maybe_vec3(ScriptBinderCall* c, const u16 i, const GeoVector def) {
  if (c->argCount > i && val_type(c->args[i]) == ScriptType_Vec3) {
    return val_as_vec3(c->args[i]);
  }
  return def;
}

GeoQuat script_arg_maybe_quat(ScriptBinderCall* c, const u16 i, const GeoQuat def) {
  if (c->argCount > i && val_type(c->args[i]) == ScriptType_Quat) {
    return val_as_quat(c->args[i]);
  }
  return def;
}

GeoColor script_arg_maybe_color(ScriptBinderCall* c, const u16 i, const GeoColor def) {
  if (c->argCount > i && val_type(c->args[i]) == ScriptType_Color) {
    return val_as_color(c->args[i]);
  }
  return def;
}

EcsEntityId script_arg_maybe_entity(ScriptBinderCall* c, const u16 i, const EcsEntityId def) {
  if (c->argCount > i && val_type(c->args[i]) == ScriptType_Entity) {
    return val_as_entity(c->args[i]);
  }
  return def;
}

StringHash script_arg_maybe_str(ScriptBinderCall* c, const u16 i, const StringHash def) {
  if (c->argCount > i && val_type(c->args[i]) == ScriptType_Str) {
    return val_as_str(c->args[i]);
  }
  return def;
}

TimeDuration script_arg_maybe_time(ScriptBinderCall* c, const u16 i, const TimeDuration def) {
  if (c->argCount > i && val_type(c->args[i]) == ScriptType_Num) {
    return (TimeDuration)time_seconds(val_as_num(c->args[i]));
  }
  return def;
}

i32 script_arg_maybe_enum(ScriptBinderCall* c, const u16 i, const ScriptEnum* e, const i32 def) {
  if (c->argCount > i && val_type(c->args[i]) == ScriptType_Str) {
    return script_enum_lookup_maybe_value(e, val_as_str(c->args[i]), def);
  }
  return def;
}
