#include "script_args.h"
#include "script_enum.h"
#include "script_error.h"
#include "script_val.h"

#include "val_internal.h"

NO_INLINE_HINT static ScriptError script_arg_err(const ScriptArgs args, const u16 i) {
  if (args.count > i) {
    return script_error_arg(ScriptError_ArgumentInvalid, i);
  }
  return script_error_arg(ScriptError_ArgumentMissing, i);
}

bool script_arg_check(const ScriptArgs args, const u16 i, const ScriptMask mask, ScriptError* err) {
  if (LIKELY(args.count > i && val_type_check(args.values[i], mask))) {
    return true;
  }
  return *err = script_arg_err(args, i), false;
}

ScriptVal script_arg_any(const ScriptArgs args, const u16 i, ScriptError* err) {
  if (LIKELY(args.count > i)) {
    return args.values[i];
  }
  return *err = script_arg_err(args, i), val_null();
}

f64 script_arg_num(const ScriptArgs args, const u16 i, ScriptError* err) {
  if (LIKELY(args.count > i && val_type(args.values[i]) == ScriptType_Num)) {
    return val_as_num(args.values[i]);
  }
  return *err = script_arg_err(args, i), 0.0;
}

f64 script_arg_num_range(
    const ScriptArgs args, const u16 i, const f64 min, const f64 max, ScriptError* err) {
  if (LIKELY(args.count > i && val_type(args.values[i]) == ScriptType_Num)) {
    const f64 res = val_as_num(args.values[i]);
    if (LIKELY(res >= min && res <= max)) {
      return res;
    }
    return *err = script_error_arg(ScriptError_ArgumentOutOfRange, i), 0.0;
  }
  return *err = script_arg_err(args, i), 0.0;
}

bool script_arg_bool(const ScriptArgs args, const u16 i, ScriptError* err) {
  if (LIKELY(args.count > i && val_type(args.values[i]) == ScriptType_Bool)) {
    return val_as_bool(args.values[i]);
  }
  return *err = script_arg_err(args, i), false;
}

GeoVector script_arg_vec3(const ScriptArgs args, const u16 i, ScriptError* err) {
  if (LIKELY(args.count > i && val_type(args.values[i]) == ScriptType_Vec3)) {
    return val_as_vec3(args.values[i]);
  }
  return *err = script_arg_err(args, i), geo_vector(0);
}

GeoQuat script_arg_quat(const ScriptArgs args, const u16 i, ScriptError* err) {
  if (LIKELY(args.count > i && val_type(args.values[i]) == ScriptType_Quat)) {
    return val_as_quat(args.values[i]);
  }
  return *err = script_arg_err(args, i), geo_quat_ident;
}

GeoColor script_arg_color(const ScriptArgs args, const u16 i, ScriptError* err) {
  if (LIKELY(args.count > i && val_type(args.values[i]) == ScriptType_Color)) {
    return val_as_color(args.values[i]);
  }
  return *err = script_arg_err(args, i), geo_color_clear;
}

EcsEntityId script_arg_entity(const ScriptArgs args, const u16 i, ScriptError* err) {
  if (LIKELY(args.count > i && val_type(args.values[i]) == ScriptType_Entity)) {
    return val_as_entity(args.values[i]);
  }
  return *err = script_arg_err(args, i), 0;
}

StringHash script_arg_str(const ScriptArgs args, const u16 i, ScriptError* err) {
  if (LIKELY(args.count > i && val_type(args.values[i]) == ScriptType_Str)) {
    return val_as_str(args.values[i]);
  }
  return *err = script_arg_err(args, i), 0;
}

TimeDuration script_arg_time(const ScriptArgs args, const u16 i, ScriptError* err) {
  if (LIKELY(args.count > i && val_type(args.values[i]) == ScriptType_Num)) {
    return (TimeDuration)time_seconds(val_as_num(args.values[i]));
  }
  return *err = script_arg_err(args, i), 0;
}

i32 script_arg_enum(const ScriptArgs args, const u16 i, const ScriptEnum* e, ScriptError* err) {
  if (LIKELY(args.count > i && val_type(args.values[i]) == ScriptType_Str)) {
    const i32 res = script_enum_lookup_value(e, val_as_str(args.values[i]), err);
    if (UNLIKELY(err->kind)) {
      err->argIndex = i; // Preserve argument index.
    }
    return res;
  }
  return *err = script_arg_err(args, i), 0;
}

ScriptType script_arg_opt_type(const ScriptArgs args, const u16 i) {
  if (LIKELY(args.count > i)) {
    return val_type(args.values[i]);
  }
  return ScriptType_Null;
}

f64 script_arg_opt_num(const ScriptArgs args, const u16 i, const f64 def, ScriptError* err) {
  if (args.count > i) {
    if (val_type(args.values[i]) == ScriptType_Num) {
      return val_as_num(args.values[i]);
    }
    if (val_type(args.values[i]) == ScriptType_Null) {
      return def;
    }
    return *err = script_arg_err(args, i), 0.0;
  }
  return def;
}

f64 script_arg_opt_num_range(
    const ScriptArgs args,
    const u16        i,
    const f64        min,
    const f64        max,
    const f64        def,
    ScriptError*     err) {
  if (args.count > i) {
    if (val_type(args.values[i]) == ScriptType_Num) {
      const f64 res = val_as_num(args.values[i]);
      if (LIKELY(res >= min && res <= max)) {
        return res;
      }
      return *err = script_error_arg(ScriptError_ArgumentOutOfRange, i), 0.0;
    }
    if (val_type(args.values[i]) == ScriptType_Null) {
      return def;
    }
    return *err = script_arg_err(args, i), 0.0;
  }
  return def;
}

bool script_arg_opt_bool(const ScriptArgs args, const u16 i, const bool def, ScriptError* err) {
  if (args.count > i) {
    if (val_type(args.values[i]) == ScriptType_Bool) {
      return val_as_bool(args.values[i]);
    }
    if (val_type(args.values[i]) == ScriptType_Null) {
      return def;
    }
    return *err = script_arg_err(args, i), false;
  }
  return def;
}

GeoVector
script_arg_opt_vec3(const ScriptArgs args, const u16 i, const GeoVector def, ScriptError* err) {
  if (args.count > i) {
    if (val_type(args.values[i]) == ScriptType_Vec3) {
      return val_as_vec3(args.values[i]);
    }
    if (val_type(args.values[i]) == ScriptType_Null) {
      return def;
    }
    return *err = script_arg_err(args, i), geo_vector(0);
  }
  return def;
}

GeoQuat
script_arg_opt_quat(const ScriptArgs args, const u16 i, const GeoQuat def, ScriptError* err) {
  if (args.count > i) {
    if (val_type(args.values[i]) == ScriptType_Quat) {
      return val_as_quat(args.values[i]);
    }
    if (val_type(args.values[i]) == ScriptType_Null) {
      return def;
    }
    return *err = script_arg_err(args, i), geo_quat_ident;
  }
  return def;
}

GeoColor
script_arg_opt_color(const ScriptArgs args, const u16 i, const GeoColor def, ScriptError* err) {
  if (args.count > i) {
    if (val_type(args.values[i]) == ScriptType_Color) {
      return val_as_color(args.values[i]);
    }
    if (val_type(args.values[i]) == ScriptType_Null) {
      return def;
    }
    return *err = script_arg_err(args, i), geo_color_clear;
  }
  return def;
}

EcsEntityId
script_arg_opt_entity(const ScriptArgs args, const u16 i, const EcsEntityId def, ScriptError* err) {
  if (args.count > i) {
    if (val_type(args.values[i]) == ScriptType_Entity) {
      return val_as_entity(args.values[i]);
    }
    if (val_type(args.values[i]) == ScriptType_Null) {
      return def;
    }
    return *err = script_arg_err(args, i), 0;
  }
  return def;
}

StringHash
script_arg_opt_str(const ScriptArgs args, const u16 i, const StringHash def, ScriptError* err) {
  if (args.count > i) {
    if (val_type(args.values[i]) == ScriptType_Str) {
      return val_as_str(args.values[i]);
    }
    if (val_type(args.values[i]) == ScriptType_Null) {
      return def;
    }
    return *err = script_arg_err(args, i), 0;
  }
  return def;
}

TimeDuration
script_arg_opt_time(const ScriptArgs args, const u16 i, const TimeDuration def, ScriptError* err) {
  if (args.count > i) {
    if (val_type(args.values[i]) == ScriptType_Num) {
      return (TimeDuration)time_seconds(val_as_num(args.values[i]));
    }
    if (val_type(args.values[i]) == ScriptType_Null) {
      return def;
    }
    return *err = script_arg_err(args, i), 0;
  }
  return def;
}

i32 script_arg_opt_enum(
    const ScriptArgs args, const u16 i, const ScriptEnum* e, const i32 def, ScriptError* err) {
  if (args.count > i) {
    if (val_type(args.values[i]) == ScriptType_Str) {
      return script_enum_lookup_value(e, val_as_str(args.values[i]), err);
    }
    if (val_type(args.values[i]) == ScriptType_Null) {
      return def;
    }
    return *err = script_arg_err(args, i), 0;
  }
  return def;
}

f64 script_arg_maybe_num(const ScriptArgs args, const u16 i, const f64 def) {
  if (args.count > i && val_type(args.values[i]) == ScriptType_Num) {
    return val_as_num(args.values[i]);
  }
  return def;
}

bool script_arg_maybe_bool(const ScriptArgs args, const u16 i, const bool def) {
  if (args.count > i && val_type(args.values[i]) == ScriptType_Bool) {
    return val_as_bool(args.values[i]);
  }
  return def;
}

GeoVector script_arg_maybe_vec3(const ScriptArgs args, const u16 i, const GeoVector def) {
  if (args.count > i && val_type(args.values[i]) == ScriptType_Vec3) {
    return val_as_vec3(args.values[i]);
  }
  return def;
}

GeoQuat script_arg_maybe_quat(const ScriptArgs args, const u16 i, const GeoQuat def) {
  if (args.count > i && val_type(args.values[i]) == ScriptType_Quat) {
    return val_as_quat(args.values[i]);
  }
  return def;
}

GeoColor script_arg_maybe_color(const ScriptArgs args, const u16 i, const GeoColor def) {
  if (args.count > i && val_type(args.values[i]) == ScriptType_Color) {
    return val_as_color(args.values[i]);
  }
  return def;
}

EcsEntityId script_arg_maybe_entity(const ScriptArgs args, const u16 i, const EcsEntityId def) {
  if (args.count > i && val_type(args.values[i]) == ScriptType_Entity) {
    return val_as_entity(args.values[i]);
  }
  return def;
}

StringHash script_arg_maybe_str(const ScriptArgs args, const u16 i, const StringHash def) {
  if (args.count > i && val_type(args.values[i]) == ScriptType_Str) {
    return val_as_str(args.values[i]);
  }
  return def;
}

TimeDuration script_arg_maybe_time(const ScriptArgs args, const u16 i, const TimeDuration def) {
  if (args.count > i && val_type(args.values[i]) == ScriptType_Num) {
    return (TimeDuration)time_seconds(val_as_num(args.values[i]));
  }
  return def;
}

i32 script_arg_maybe_enum(const ScriptArgs args, const u16 i, const ScriptEnum* e, const i32 def) {
  if (args.count > i && val_type(args.values[i]) == ScriptType_Str) {
    return script_enum_lookup_maybe_value(e, val_as_str(args.values[i]), def);
  }
  return def;
}
