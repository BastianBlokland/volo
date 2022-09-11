#include "core_annotation.h"
#include "core_array.h"

#include "format_internal.h"

String asset_format_str(const AssetFormat fmt) {
  static const String g_names[] = {
      string_static("atx"),
      string_static("bin"),
      string_static("bt"),
      string_static("ftx"),
      string_static("gltf"),
      string_static("gra"),
      string_static("imp"),
      string_static("obj"),
      string_static("pme"),
      string_static("ppm"),
      string_static("ptx"),
      string_static("r32"),
      string_static("raw"),
      string_static("spv"),
      string_static("tga"),
      string_static("ttf"),
  };
  ASSERT(array_elems(g_names) == AssetFormat_Count, "Incorrect number of asset-format names");
  return g_names[fmt];
}

AssetFormat asset_format_from_ext(const String ext) {
  for (AssetFormat fmt = 0; fmt != AssetFormat_Count; ++fmt) {
    if (string_eq(ext, asset_format_str(fmt))) {
      return fmt;
    }
  }
  return AssetFormat_Raw;
}
