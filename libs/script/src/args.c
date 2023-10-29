#include "script_args.h"
#include "script_enum.h"
#include "script_error.h"
#include "script_val.h"

#include "val_internal.h"

NO_INLINE_HINT static ScriptError script_arg_err(const ScriptArgs args, const u16 i) {
  if (args.count > i) {
    return script_error_arg(ScriptError_InvalidArgument, i);
  }
  return script_error_arg(ScriptError_MissingArgument, i);
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

f64 script_arg_maybe_number(const ScriptArgs args, const u16 i, const f64 def) {
  return args.count > i ? script_get_number(args.values[i], def) : def;
}

bool script_arg_maybe_bool(const ScriptArgs args, const u16 i, const bool def) {
  return args.count > i ? script_get_bool(args.values[i], def) : def;
}

GeoVector script_arg_maybe_vector3(const ScriptArgs args, const u16 i, const GeoVector def) {
  return args.count > i ? script_get_vector3(args.values[i], def) : def;
}

GeoQuat script_arg_maybe_quat(const ScriptArgs args, const u16 i, const GeoQuat def) {
  return args.count > i ? script_get_quat(args.values[i], def) : def;
}

EcsEntityId script_arg_maybe_entity(const ScriptArgs args, const u16 i, const EcsEntityId def) {
  return args.count > i ? script_get_entity(args.values[i], def) : def;
}

StringHash script_arg_maybe_string(const ScriptArgs args, const u16 i, const StringHash def) {
  return args.count > i ? script_get_string(args.values[i], def) : def;
}

TimeDuration script_arg_maybe_time(const ScriptArgs args, const u16 i, const TimeDuration def) {
  return args.count > i ? script_get_time(args.values[i], def) : def;
}

i32 script_arg_maybe_enum(const ScriptArgs args, const u16 i, const ScriptEnum* e, const i32 def) {
  if (args.count <= i) {
    return def;
  }
  const StringHash hash = script_get_string(args.values[i], string_hash_invalid);
  if (!hash) {
    return def;
  }
  return script_enum_lookup_maybe_value(e, hash, def);
}

ScriptVal script_arg_last_or_null(const ScriptArgs args) {
  return args.count ? args.values[args.count - 1] : val_null();
}
