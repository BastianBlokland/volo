#include "app_check.h"

void app_check_configure(CheckDef* check) {
  register_spec(check, alloc_block);
  register_spec(check, alloc_bump);
  register_spec(check, alloc_chunked);
  register_spec(check, alloc_page);
  register_spec(check, alloc_scratch);
  register_spec(check, array);
  register_spec(check, ascii);
  register_spec(check, base64);
  register_spec(check, bits);
  register_spec(check, bitset);
  register_spec(check, compare);
  register_spec(check, dynarray);
  register_spec(check, dynbitset);
  register_spec(check, dynstring);
  register_spec(check, env);
  register_spec(check, file_monitor);
  register_spec(check, file);
  register_spec(check, float);
  register_spec(check, format);
  register_spec(check, macro);
  register_spec(check, math);
  register_spec(check, memory);
  register_spec(check, path);
  register_spec(check, rng);
  register_spec(check, search);
  register_spec(check, shuffle);
  register_spec(check, sort);
  register_spec(check, string);
  register_spec(check, stringtable);
  register_spec(check, thread);
  register_spec(check, time);
  register_spec(check, unicode);
  register_spec(check, utf8);
  register_spec(check, winutils);
}
