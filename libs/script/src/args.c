#include "core_diag.h"
#include "script_args.h"
#include "script_binder.h"
#include "script_enum.h"
#include "script_val.h"

#include "val_internal.h"

INLINE_HINT static bool arg_type_check(ScriptBinderCall* c, const u16 i, const ScriptMask mask) {
  if (UNLIKELY(c->argCount <= i)) {
    c->panic = (ScriptPanic){ScriptPanic_ArgumentMissing, .argIndex = i};
    return false;
  }
  if (UNLIKELY(!val_type_check(c->args[i], mask))) {
    c->panic = (ScriptPanic){
        ScriptPanic_ArgumentTypeMismatch,
        .argIndex   = i,
        .typeMask   = mask,
        .typeActual = script_type(c->args[i]),
    };
    return false;
  }
  return true;
}

bool script_arg_check(ScriptBinderCall* c, const u16 i, const ScriptMask mask) {
  return arg_type_check(c, i, mask);
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
    c->panic = (ScriptPanic){ScriptPanic_ArgumentMissing, .argIndex = i};
    return val_null();
  }
  return c->args[i];
}

f64 script_arg_num(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(arg_type_check(c, i, script_mask_num))) {
    return val_as_num(c->args[i]);
  }
  return 0.0f;
}

f64 script_arg_num_range(ScriptBinderCall* c, const u16 i, const f64 min, const f64 max) {
  if (LIKELY(arg_type_check(c, i, script_mask_num))) {
    const f64 res = val_as_num(c->args[i]);
    if (LIKELY(res >= min && res <= max)) {
      return res;
    }
    return c->panic = (ScriptPanic){ScriptPanic_ArgumentOutOfRange, .argIndex = i}, 0.0;
  }
  return 0.0;
}

bool script_arg_bool(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(arg_type_check(c, i, script_mask_bool))) {
    return val_as_bool(c->args[i]);
  }
  return false;
}

GeoVector script_arg_vec3(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(arg_type_check(c, i, script_mask_vec3))) {
    return val_as_vec3(c->args[i]);
  }
  return geo_vector(0);
}

GeoQuat script_arg_quat(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(arg_type_check(c, i, script_mask_quat))) {
    return val_as_quat(c->args[i]);
  }
  return geo_quat_ident;
}

GeoColor script_arg_color(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(arg_type_check(c, i, script_mask_color))) {
    return val_as_color(c->args[i]);
  }
  return geo_color_clear;
}

EcsEntityId script_arg_entity(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(arg_type_check(c, i, script_mask_entity))) {
    return val_as_entity(c->args[i]);
  }
  return 0;
}

StringHash script_arg_str(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(arg_type_check(c, i, script_mask_str))) {
    return val_as_str(c->args[i]);
  }
  return 0;
}

TimeDuration script_arg_time(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(arg_type_check(c, i, script_mask_num))) {
    return (TimeDuration)time_seconds(val_as_num(c->args[i]));
  }
  return 0;
}

i32 script_arg_enum(ScriptBinderCall* c, const u16 i, const ScriptEnum* e) {
  if (LIKELY(arg_type_check(c, i, script_mask_str))) {
    const i32 res = script_enum_lookup_value(e, val_as_str(c->args[i]), &c->panic);
    if (UNLIKELY(script_call_panicked(c))) {
      c->panic.argIndex = i; // Preserve argument index.
    }
    return res;
  }
  return 0;
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
    return arg_type_check(c, i, script_mask_num | script_mask_null), 0.0f;
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
      return c->panic = (ScriptPanic){ScriptPanic_ArgumentOutOfRange, .argIndex = i}, 0.0;
    }
    if (val_type(c->args[i]) == ScriptType_Null) {
      return def;
    }
    return arg_type_check(c, i, script_mask_num | script_mask_null), 0.0f;
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
    return arg_type_check(c, i, script_mask_bool | script_mask_null), false;
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
    return arg_type_check(c, i, script_mask_vec3 | script_mask_null), geo_vector(0);
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
    return arg_type_check(c, i, script_mask_quat | script_mask_null), geo_quat_ident;
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
    return arg_type_check(c, i, script_mask_color | script_mask_null), geo_color_clear;
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
    return arg_type_check(c, i, script_mask_entity | script_mask_null), 0;
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
    return arg_type_check(c, i, script_mask_str | script_mask_null), 0;
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
    return arg_type_check(c, i, script_mask_num | script_mask_null), 0;
  }
  return def;
}

i32 script_arg_opt_enum(ScriptBinderCall* c, const u16 i, const ScriptEnum* e, const i32 def) {
  if (c->argCount > i) {
    if (val_type(c->args[i]) == ScriptType_Str) {
      return script_enum_lookup_value(e, val_as_str(c->args[i]), &c->panic);
    }
    if (val_type(c->args[i]) == ScriptType_Null) {
      return def;
    }
    return arg_type_check(c, i, script_mask_str | script_mask_null), 0;
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
