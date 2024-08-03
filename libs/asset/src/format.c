#include "core_annotation.h"
#include "core_array.h"
#include "core_diag.h"

#include "format_internal.h"

static const String g_assetFormatExtensions[AssetFormat_Count] = {
    [AssetFormat_ArrayTex] = string_static("arraytex"),
    [AssetFormat_Atlas]    = string_static("atlas"),
    [AssetFormat_Cursor]   = string_static("cursor"),
    [AssetFormat_Decal]    = string_static("decal"),
    [AssetFormat_FontTex]  = string_static("fonttex"),
    [AssetFormat_Glsl]     = string_static("glsl"),
    [AssetFormat_GlslFrag] = string_static("frag"),
    [AssetFormat_GlslVert] = string_static("vert"),
    [AssetFormat_Gltf]     = string_static("gltf"),
    [AssetFormat_Graphic]  = string_static("graphic"),
    [AssetFormat_Height16] = string_static("r16"),
    [AssetFormat_Height32] = string_static("r32"),
    [AssetFormat_Inputs]   = string_static("inputs"),
    [AssetFormat_Level]    = string_static("level"),
    [AssetFormat_Obj]      = string_static("obj"),
    [AssetFormat_Ppm]      = string_static("ppm"),
    [AssetFormat_Prefabs]  = string_static("prefabs"),
    [AssetFormat_ProcMesh] = string_static("procmesh"),
    [AssetFormat_ProcTex]  = string_static("proctex"),
    [AssetFormat_Products] = string_static("products"),
    [AssetFormat_Script]   = string_static("script"),
    [AssetFormat_Spv]      = string_static("spv"),
    [AssetFormat_Terrain]  = string_static("terrain"),
    [AssetFormat_Tga]      = string_static("tga"),
    [AssetFormat_Ttf]      = string_static("ttf"),
    [AssetFormat_Vfx]      = string_static("vfx"),
    [AssetFormat_Wav]      = string_static("wav"),
    [AssetFormat_Weapons]  = string_static("weapons"),
};

static const String g_assetFormatNames[AssetFormat_Count] = {
    [AssetFormat_ArrayTex] = string_static("ArrayTex"),
    [AssetFormat_Atlas]    = string_static("Atlas"),
    [AssetFormat_Cursor]   = string_static("Cursor"),
    [AssetFormat_Decal]    = string_static("Decal"),
    [AssetFormat_FontTex]  = string_static("FontTex"),
    [AssetFormat_Glsl]     = string_static("Glsl"),
    [AssetFormat_GlslFrag] = string_static("GlslFrag"),
    [AssetFormat_GlslVert] = string_static("GlslVert"),
    [AssetFormat_Gltf]     = string_static("Gltf"),
    [AssetFormat_Graphic]  = string_static("Graphic"),
    [AssetFormat_Height16] = string_static("Height16"),
    [AssetFormat_Height32] = string_static("Height32"),
    [AssetFormat_Inputs]   = string_static("Inputs"),
    [AssetFormat_Level]    = string_static("Level"),
    [AssetFormat_Obj]      = string_static("Obj"),
    [AssetFormat_Ppm]      = string_static("Ppm"),
    [AssetFormat_Prefabs]  = string_static("Prefabs"),
    [AssetFormat_ProcMesh] = string_static("ProcMesh"),
    [AssetFormat_ProcTex]  = string_static("ProcTex"),
    [AssetFormat_Products] = string_static("Products"),
    [AssetFormat_Raw]      = string_static("Raw"),
    [AssetFormat_Script]   = string_static("Script"),
    [AssetFormat_Spv]      = string_static("Spv"),
    [AssetFormat_Terrain]  = string_static("Terrain"),
    [AssetFormat_Tga]      = string_static("Tga"),
    [AssetFormat_Ttf]      = string_static("Ttf"),
    [AssetFormat_Vfx]      = string_static("Vfx"),
    [AssetFormat_Wav]      = string_static("Wav"),
    [AssetFormat_Weapons]  = string_static("Weapons"),
};

String asset_format_str(const AssetFormat fmt) {
  diag_assert(fmt < AssetFormat_Count);
  return g_assetFormatNames[fmt];
}

AssetFormat asset_format_from_ext(const String ext) {
  for (AssetFormat fmt = 0; fmt != AssetFormat_Count; ++fmt) {
    if (string_eq(ext, g_assetFormatExtensions[fmt])) {
      return fmt;
    }
  }
  return AssetFormat_Raw;
}
