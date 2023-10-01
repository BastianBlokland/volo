#include "script_args.h"
#include "script_val.h"

ScriptVal script_arg_last_or_null(const ScriptArgs args) {
  return args.count ? args.values[args.count - 1] : script_null();
}
