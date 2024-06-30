#include "core_annotation.h"
#include "core_array.h"

#include "format_internal.h"

String asset_format_str(const AssetFormat fmt) {
  static const String g_names[AssetFormat_Count] = {
      [AssetFormat_ArrayTex] = string_static("arraytex"),
      [AssetFormat_Atlas]    = string_static("atlas"),
      [AssetFormat_Bin]      = string_static("bin"),
      [AssetFormat_Cursor]   = string_static("cursor"),
      [AssetFormat_Decal]    = string_static("decal"),
      [AssetFormat_FontTex]  = string_static("fonttex"),
      [AssetFormat_GlslFrag] = string_static("frag"),
      [AssetFormat_GlslVert] = string_static("vert"),
      [AssetFormat_Gltf]     = string_static("gltf"),
      [AssetFormat_Graphic]  = string_static("graphic"),
      [AssetFormat_Inputs]   = string_static("inputs"),
      [AssetFormat_Level]    = string_static("level"),
      [AssetFormat_Obj]      = string_static("obj"),
      [AssetFormat_Ppm]      = string_static("ppm"),
      [AssetFormat_Prefabs]  = string_static("prefabs"),
      [AssetFormat_ProcMesh] = string_static("procmesh"),
      [AssetFormat_ProcTex]  = string_static("proctex"),
      [AssetFormat_Products] = string_static("products"),
      [AssetFormat_R16]      = string_static("r16"),
      [AssetFormat_R32]      = string_static("r32"),
      [AssetFormat_Raw]      = string_static("raw"),
      [AssetFormat_Script]   = string_static("script"),
      [AssetFormat_Spv]      = string_static("spv"),
      [AssetFormat_Terrain]  = string_static("terrain"),
      [AssetFormat_Tga]      = string_static("tga"),
      [AssetFormat_Ttf]      = string_static("ttf"),
      [AssetFormat_Vfx]      = string_static("vfx"),
      [AssetFormat_Wav]      = string_static("wav"),
      [AssetFormat_Weapons]  = string_static("weapons"),
  };
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
