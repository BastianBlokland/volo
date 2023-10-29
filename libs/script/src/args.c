#include "script_args.h"
#include "script_enum.h"
#include "script_val.h"

f64 script_arg_maybe_number(const ScriptArgs args, const u32 i, const f64 def) {
  return args.count > i ? script_get_number(args.values[i], def) : def;
}

bool script_arg_maybe_bool(const ScriptArgs args, const u32 i, const bool def) {
  return args.count > i ? script_get_bool(args.values[i], def) : def;
}

GeoVector script_arg_maybe_vector3(const ScriptArgs args, const u32 i, const GeoVector def) {
  return args.count > i ? script_get_vector3(args.values[i], def) : def;
}

GeoQuat script_arg_maybe_quat(const ScriptArgs args, const u32 i, const GeoQuat def) {
  return args.count > i ? script_get_quat(args.values[i], def) : def;
}

EcsEntityId script_arg_maybe_entity(const ScriptArgs args, const u32 i, const EcsEntityId def) {
  return args.count > i ? script_get_entity(args.values[i], def) : def;
}

StringHash script_arg_maybe_string(const ScriptArgs args, const u32 i, const StringHash def) {
  return args.count > i ? script_get_string(args.values[i], def) : def;
}

TimeDuration script_arg_maybe_time(const ScriptArgs args, const u32 i, const TimeDuration def) {
  return args.count > i ? script_get_time(args.values[i], def) : def;
}

i32 script_arg_maybe_enum(const ScriptArgs args, const u32 i, const ScriptEnum* e, const i32 def) {
  if (args.count <= i) {
    return def;
  }
  const StringHash hash = script_get_string(args.values[i], string_hash_invalid);
  if (!hash) {
    return def;
  }
  return script_enum_lookup_value(e, hash, def);
}

ScriptVal script_arg_last_or_null(const ScriptArgs args) {
  return args.count ? args.values[args.count - 1] : script_null();
}
