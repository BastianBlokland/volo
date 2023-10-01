#include "script_args.h"
#include "script_enum.h"
#include "script_val.h"

f64 script_arg_number(const ScriptArgs args, const u32 i, const f64 fallback) {
  return args.count > i ? script_get_number(args.values[i], fallback) : fallback;
}

bool script_arg_bool(const ScriptArgs args, const u32 i, const bool fallback) {
  return args.count > i ? script_get_bool(args.values[i], fallback) : fallback;
}

GeoVector script_arg_vector3(const ScriptArgs args, const u32 i, const GeoVector fallback) {
  return args.count > i ? script_get_vector3(args.values[i], fallback) : fallback;
}

GeoQuat script_arg_quat(const ScriptArgs args, const u32 i, const GeoQuat fallback) {
  return args.count > i ? script_get_quat(args.values[i], fallback) : fallback;
}

EcsEntityId script_arg_entity(const ScriptArgs args, const u32 i, const EcsEntityId fallback) {
  return args.count > i ? script_get_entity(args.values[i], fallback) : fallback;
}

StringHash script_arg_string(const ScriptArgs args, const u32 i, const StringHash fallback) {
  return args.count > i ? script_get_string(args.values[i], fallback) : fallback;
}

TimeDuration script_arg_time(const ScriptArgs args, const u32 i, const TimeDuration fallback) {
  return args.count > i ? script_get_time(args.values[i], fallback) : fallback;
}

i32 script_arg_enum(const ScriptArgs args, const u32 i, const ScriptEnum* e, const i32 fallback) {
  if (args.count <= i) {
    return fallback;
  }
  const StringHash hash = script_get_string(args.values[i], string_hash_invalid);
  if (!hash) {
    return fallback;
  }
  return script_enum_lookup(e, hash, fallback);
}

ScriptVal script_arg_last_or_null(const ScriptArgs args) {
  return args.count ? args.values[args.count - 1] : script_null();
}
