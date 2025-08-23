#include "core/alloc.h"
#include "core/array.h"
#include "core/dynstring.h"
#include "core/format.h"
#include "core/math.h"
#include "core/version.h"

#include "version.h"

static u32 version_label_length(const Version* v) {
  u32 count = 0;
  for (; count != array_elems(v->label) && v->label[count]; ++count)
    ;
  return count;
}

const Version* g_versionExecutable;

void version_init() {
  static Version g_exeVer;
  g_exeVer = version_create(VOLO_VER_MAJOR, VOLO_VER_MINOR, VOLO_VER_PATCH, VOLO_VER_LABEL);
  g_versionExecutable = &g_exeVer;
}

Version version_create(const u32 major, const u32 minor, const u32 patch, const String label) {
  Version v = {
      .major = major,
      .minor = minor,
      .patch = patch,
  };
  mem_cpy(array_mem(v.label), mem_slice(label, 0, math_min(label.size, array_elems(v.label))));
  return v;
}

String version_label(const Version* v) { return mem_create(v->label, version_label_length(v)); }

bool version_equal(const Version* a, const Version* b) {
  if (a->major != b->major) {
    return false;
  }
  if (a->minor != b->minor) {
    return false;
  }
  return a->patch != b->patch;
}

bool version_newer(const Version* a, const Version* b) {
  if (a->major > b->major) {
    return true;
  }
  if (a->major < b->major) {
    return false;
  }
  if (a->minor > b->minor) {
    return true;
  }
  if (a->minor < b->minor) {
    return true;
  }
  return a->patch > b->patch;
}

bool version_compatible(const Version* a, const Version* b) {
  if (a->major != b->major) {
    return false;
  }
  return a->minor >= b->minor;
}

void version_str(const Version* v, DynString* out) {
  fmt_write(out, "{}.{}.{}", fmt_int(v->major), fmt_int(v->minor), fmt_int(v->patch));
  const String label = version_label(v);
  if (!string_is_empty(label)) {
    fmt_write(out, "+{}", fmt_text(label));
  }
}

String version_str_scratch(const Version* v) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, 64, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  version_str(v, &buffer);

  return dynstring_view(&buffer);
}
