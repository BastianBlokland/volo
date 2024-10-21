#include "script_args.h"
#include "script_binder.h"
#include "script_enum.h"
#include "script_error.h"
#include "script_val.h"

#include "val_internal.h"

NO_INLINE_HINT static void script_arg_set_err(ScriptBinderCall* c, const u16 i) {
  if (c->argCount > i) {
    if (val_type(c->args[i]) == ScriptType_Null) {
      c->err = script_error_arg(ScriptError_ArgumentNull, i);
      return;
    }
    c->err = script_error_arg(ScriptError_ArgumentInvalid, i);
    return;
  }
  c->err = script_error_arg(ScriptError_ArgumentMissing, i);
}

bool script_arg_check(ScriptBinderCall* c, const u16 i, const ScriptMask mask) {
  if (LIKELY(c->argCount > i && val_type_check(c->args[i], mask))) {
    return true;
  }
  return script_arg_set_err(c, i), false;
}

bool script_arg_has(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(c->argCount > i && val_type(c->args[i]) != ScriptType_Null)) {
    return true;
  }
  return false;
}

ScriptVal script_arg_any(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(c->argCount > i)) {
    return c->args[i];
  }
  return script_arg_set_err(c, i), val_null();
}

f64 script_arg_num(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(c->argCount > i && val_type(c->args[i]) == ScriptType_Num)) {
    return val_as_num(c->args[i]);
  }
  return script_arg_set_err(c, i), 0.0;
}

f64 script_arg_num_range(ScriptBinderCall* c, const u16 i, const f64 min, const f64 max) {
  if (LIKELY(c->argCount > i && val_type(c->args[i]) == ScriptType_Num)) {
    const f64 res = val_as_num(c->args[i]);
    if (LIKELY(res >= min && res <= max)) {
      return res;
    }
    return script_error_arg(ScriptError_ArgumentOutOfRange, i), 0.0;
  }
  return script_arg_set_err(c, i), 0.0;
}

bool script_arg_bool(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(c->argCount > i && val_type(c->args[i]) == ScriptType_Bool)) {
    return val_as_bool(c->args[i]);
  }
  return script_arg_set_err(c, i), false;
}

GeoVector script_arg_vec3(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(c->argCount > i && val_type(c->args[i]) == ScriptType_Vec3)) {
    return val_as_vec3(c->args[i]);
  }
  return script_arg_set_err(c, i), geo_vector(0);
}

GeoQuat script_arg_quat(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(c->argCount > i && val_type(c->args[i]) == ScriptType_Quat)) {
    return val_as_quat(c->args[i]);
  }
  return script_arg_set_err(c, i), geo_quat_ident;
}

GeoColor script_arg_color(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(c->argCount > i && val_type(c->args[i]) == ScriptType_Color)) {
    return val_as_color(c->args[i]);
  }
  return script_arg_set_err(c, i), geo_color_clear;
}

EcsEntityId script_arg_entity(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(c->argCount > i && val_type(c->args[i]) == ScriptType_Entity)) {
    return val_as_entity(c->args[i]);
  }
  return script_arg_set_err(c, i), 0;
}

StringHash script_arg_str(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(c->argCount > i && val_type(c->args[i]) == ScriptType_Str)) {
    return val_as_str(c->args[i]);
  }
  return script_arg_set_err(c, i), 0;
}

TimeDuration script_arg_time(ScriptBinderCall* c, const u16 i) {
  if (LIKELY(c->argCount > i && val_type(c->args[i]) == ScriptType_Num)) {
    return (TimeDuration)time_seconds(val_as_num(c->args[i]));
  }
  return script_arg_set_err(c, i), 0;
}

i32 script_arg_enum(ScriptBinderCall* c, const u16 i, const ScriptEnum* e) {
  if (LIKELY(c->argCount > i && val_type(c->args[i]) == ScriptType_Str)) {
    const i32 res = script_enum_lookup_value(e, val_as_str(c->args[i]), &c->err);
    if (UNLIKELY(c->err.kind)) {
      c->err.argIndex = i; // Preserve argument index.
    }
    return res;
  }
  return script_arg_set_err(c, i), 0;
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
    return script_arg_set_err(c, i), 0.0;
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
      return script_error_arg(ScriptError_ArgumentOutOfRange, i), 0.0;
    }
    if (val_type(c->args[i]) == ScriptType_Null) {
      return def;
    }
    return script_arg_set_err(c, i), 0.0;
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
    return script_arg_set_err(c, i), false;
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
    return script_arg_set_err(c, i), geo_vector(0);
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
    return script_arg_set_err(c, i), geo_quat_ident;
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
    return script_arg_set_err(c, i), geo_color_clear;
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
    return script_arg_set_err(c, i), 0;
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
    return script_arg_set_err(c, i), 0;
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
    return script_arg_set_err(c, i), 0;
  }
  return def;
}

i32 script_arg_opt_enum(ScriptBinderCall* c, const u16 i, const ScriptEnum* e, const i32 def) {
  if (c->argCount > i) {
    if (val_type(c->args[i]) == ScriptType_Str) {
      return script_enum_lookup_value(e, val_as_str(c->args[i]), &c->err);
    }
    if (val_type(c->args[i]) == ScriptType_Null) {
      return def;
    }
    return script_arg_set_err(c, i), 0;
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
