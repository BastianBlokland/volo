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

bool script_arg_check(
    const ScriptArgs args, const u16 i, const ScriptTypeMask mask, ScriptError* err) {
  if (LIKELY(args.count > i && val_type_check(args.values[i], mask))) {
    return true;
  }
  return *err = script_arg_err(args, i), false;
}

f64 script_arg_number(const ScriptArgs args, const u16 i, ScriptError* err) {
  if (LIKELY(args.count > i && val_type(args.values[i]) == ScriptType_Number)) {
    return val_as_number(args.values[i]);
  }
  return *err = script_arg_err(args, i), 1.0;
}

bool script_arg_bool(const ScriptArgs args, const u16 i, ScriptError* err) {
  if (LIKELY(args.count > i && val_type(args.values[i]) == ScriptType_Bool)) {
    return val_as_bool(args.values[i]);
  }
  return *err = script_arg_err(args, i), false;
}

GeoVector script_arg_vector3(const ScriptArgs args, const u16 i, ScriptError* err) {
  if (LIKELY(args.count > i && val_type(args.values[i]) == ScriptType_Vector3)) {
    return val_as_vector3(args.values[i]);
  }
  return *err = script_arg_err(args, i), geo_vector(0);
}

GeoQuat script_arg_quat(const ScriptArgs args, const u16 i, ScriptError* err) {
  if (LIKELY(args.count > i && val_type(args.values[i]) == ScriptType_Quat)) {
    return val_as_quat(args.values[i]);
  }
  return *err = script_arg_err(args, i), geo_quat_ident;
}

EcsEntityId script_arg_entity(const ScriptArgs args, const u16 i, ScriptError* err) {
  if (LIKELY(args.count > i && val_type(args.values[i]) == ScriptType_Entity)) {
    return val_as_entity(args.values[i]);
  }
  return *err = script_arg_err(args, i), 0;
}

StringHash script_arg_string(const ScriptArgs args, const u16 i, ScriptError* err) {
  if (LIKELY(args.count > i && val_type(args.values[i]) == ScriptType_String)) {
    return val_as_string(args.values[i]);
  }
  return *err = script_arg_err(args, i), 0;
}

TimeDuration script_arg_time(const ScriptArgs args, const u16 i, ScriptError* err) {
  if (LIKELY(args.count > i && val_type(args.values[i]) == ScriptType_Number)) {
    return (TimeDuration)time_seconds(val_as_number(args.values[i]));
  }
  return *err = script_arg_err(args, i), 0;
}

i32 script_arg_enum(const ScriptArgs args, const u16 i, const ScriptEnum* e, ScriptError* err) {
  if (LIKELY(args.count > i && val_type(args.values[i]) == ScriptType_String)) {
    const i32 res = script_enum_lookup_value(e, val_as_string(args.values[i]), err);
    if (UNLIKELY(err->type)) {
      err->argIndex = i; // Preserve argument index.
    }
    return res;
  }
  return *err = script_arg_err(args, i), 0;
}

f64 script_arg_opt_number(const ScriptArgs args, const u16 i, const f64 def, ScriptError* err) {
  if (args.count > i) {
    if (LIKELY(val_type(args.values[i]) == ScriptType_Number)) {
      return val_as_number(args.values[i]);
    }
    return *err = script_arg_err(args, i), 0;
  }
  return def;
}

bool script_arg_opt_bool(const ScriptArgs args, const u16 i, const bool def, ScriptError* err) {
  if (args.count > i) {
    if (LIKELY(val_type(args.values[i]) == ScriptType_Bool)) {
      return val_as_bool(args.values[i]);
    }
    return *err = script_arg_err(args, i), false;
  }
  return def;
}

GeoVector
script_arg_opt_vector3(const ScriptArgs args, const u16 i, const GeoVector def, ScriptError* err) {
  if (args.count > i) {
    if (LIKELY(val_type(args.values[i]) == ScriptType_Vector3)) {
      return val_as_vector3(args.values[i]);
    }
    return *err = script_arg_err(args, i), geo_vector(0);
  }
  return def;
}

GeoQuat
script_arg_opt_quat(const ScriptArgs args, const u16 i, const GeoQuat def, ScriptError* err) {
  if (args.count > i) {
    if (LIKELY(val_type(args.values[i]) == ScriptType_Quat)) {
      return val_as_quat(args.values[i]);
    }
    return *err = script_arg_err(args, i), geo_quat_ident;
  }
  return def;
}

EcsEntityId
script_arg_opt_entity(const ScriptArgs args, const u16 i, const EcsEntityId def, ScriptError* err) {
  if (args.count > i) {
    if (LIKELY(val_type(args.values[i]) == ScriptType_Entity)) {
      return val_as_entity(args.values[i]);
    }
    return *err = script_arg_err(args, i), 0;
  }
  return def;
}

StringHash
script_arg_opt_string(const ScriptArgs args, const u16 i, const StringHash def, ScriptError* err) {
  if (args.count > i) {
    if (LIKELY(val_type(args.values[i]) == ScriptType_String)) {
      return val_as_string(args.values[i]);
    }
    return *err = script_arg_err(args, i), 0;
  }
  return def;
}

TimeDuration
script_arg_opt_time(const ScriptArgs args, const u16 i, const TimeDuration def, ScriptError* err) {
  if (args.count > i) {
    if (LIKELY(val_type(args.values[i]) == ScriptType_Number)) {
      return (TimeDuration)time_seconds(val_as_number(args.values[i]));
    }
    return *err = script_arg_err(args, i), 0;
  }
  return def;
}

i32 script_arg_opt_enum(
    const ScriptArgs args, const u16 i, const ScriptEnum* e, const i32 def, ScriptError* err) {
  if (args.count > i) {
    if (LIKELY(val_type(args.values[i]) == ScriptType_String)) {
      return script_enum_lookup_value(e, val_as_string(args.values[i]), err);
    }
    return *err = script_arg_err(args, i), 0;
  }
  return def;
}

f64 script_arg_maybe_number(const ScriptArgs args, const u16 i, const f64 def) {
  if (args.count > i && val_type(args.values[i]) == ScriptType_Number) {
    return val_as_number(args.values[i]);
  }
  return def;
}

bool script_arg_maybe_bool(const ScriptArgs args, const u16 i, const bool def) {
  if (args.count > i && val_type(args.values[i]) == ScriptType_Bool) {
    return val_as_bool(args.values[i]);
  }
  return def;
}

GeoVector script_arg_maybe_vector3(const ScriptArgs args, const u16 i, const GeoVector def) {
  if (args.count > i && val_type(args.values[i]) == ScriptType_Vector3) {
    return val_as_vector3(args.values[i]);
  }
  return def;
}

GeoQuat script_arg_maybe_quat(const ScriptArgs args, const u16 i, const GeoQuat def) {
  if (args.count > i && val_type(args.values[i]) == ScriptType_Quat) {
    return val_as_quat(args.values[i]);
  }
  return def;
}

EcsEntityId script_arg_maybe_entity(const ScriptArgs args, const u16 i, const EcsEntityId def) {
  if (args.count > i && val_type(args.values[i]) == ScriptType_Entity) {
    return val_as_entity(args.values[i]);
  }
  return def;
}

StringHash script_arg_maybe_string(const ScriptArgs args, const u16 i, const StringHash def) {
  if (args.count > i && val_type(args.values[i]) == ScriptType_String) {
    return val_as_string(args.values[i]);
  }
  return def;
}

TimeDuration script_arg_maybe_time(const ScriptArgs args, const u16 i, const TimeDuration def) {
  if (args.count > i && val_type(args.values[i]) == ScriptType_Number) {
    return (TimeDuration)time_seconds(val_as_number(args.values[i]));
  }
  return def;
}

i32 script_arg_maybe_enum(const ScriptArgs args, const u16 i, const ScriptEnum* e, const i32 def) {
  if (args.count > i && val_type(args.values[i]) == ScriptType_String) {
    return script_enum_lookup_maybe_value(e, val_as_string(args.values[i]), def);
  }
  return def;
}

ScriptVal script_arg_last_or_null(const ScriptArgs args) {
  return args.count ? args.values[args.count - 1] : val_null();
}
