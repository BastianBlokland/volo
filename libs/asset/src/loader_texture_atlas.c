#include "asset_atlas.h"
#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_search.h"
#include "core_sort.h"
#include "core_thread.h"
#include "data.h"
#include "data_registry.h"
#include "data_schema.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

#define atlas_max_size (1024 * 16)

static DataReg* g_dataReg;
static DataMeta g_dataAtlasDefMeta;

typedef struct {
  String name;
  String texture;
} AtlasEntryDef;

typedef struct {
  u32  size, entrySize, entryPadding, maxMipMaps;
  bool mipmaps, srgb;
  struct {
    AtlasEntryDef* values;
    usize          count;
  } entries;
} AtlasDef;

static void atlas_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_struct_t(g_dataReg, AtlasEntryDef);
    data_reg_field_t(g_dataReg, AtlasEntryDef, name, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AtlasEntryDef, texture, data_prim_t(String), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(g_dataReg, AtlasDef);
    data_reg_field_t(g_dataReg, AtlasDef, size, data_prim_t(u32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AtlasDef, entrySize, data_prim_t(u32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AtlasDef, entryPadding, data_prim_t(u32));
    data_reg_field_t(g_dataReg, AtlasDef, maxMipMaps, data_prim_t(u32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AtlasDef, mipmaps, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AtlasDef, srgb, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AtlasDef, entries, t_AtlasEntryDef, .flags = DataFlags_NotEmpty, .container = DataContainer_Array);
    // clang-format on

    g_dataAtlasDefMeta = data_meta_t(t_AtlasDef);
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetAtlasComp);

ecs_comp_define(AssetAtlasLoadComp) {
  AtlasDef def;
  u32      maxEntries;
  DynArray textures; // EcsEntityId[].
};

static void ecs_destruct_atlas_comp(void* data) {
  AssetAtlasComp* comp = data;
  alloc_free_array_t(g_alloc_heap, comp->entries, comp->entryCount);
}

static void ecs_destruct_atlas_load_comp(void* data) {
  AssetAtlasLoadComp* comp = data;
  data_destroy(g_dataReg, g_alloc_heap, g_dataAtlasDefMeta, mem_var(comp->def));
  dynarray_destroy(&comp->textures);
}

typedef enum {
  AtlasError_None = 0,
  AtlasError_NoEntries,
  AtlasError_TooManyEntries,
  AtlasError_InvalidTexture,
  AtlasError_SizeNonPow2,
  AtlasError_SizeTooBig,
  AtlasError_EntrySizeNonPow2,
  AtlasError_EntryPaddingTooBig,
  AtlasError_EntryTextureLayerCountUnsupported,

  AtlasError_Count,
} AtlasError;

static String atlas_error_str(const AtlasError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Atlas does not specify any entries"),
      string_static("Atlas specifies more entries then fit in the texture"),
      string_static("Atlas specifies an invalid texture"),
      string_static("Atlas specifies a non power-of-two texture size"),
      string_static("Atlas specifies a texture size larger then is supported"),
      string_static("Atlas specifies a non power-of-two entry size"),
      string_static("Atlas specifies an entry padding size that leaves no space for the texture"),
      string_static("Atlas entry specifies texture with a non-supported layer count"),
  };
  ASSERT(array_elems(g_msgs) == AtlasError_Count, "Incorrect number of atlas-error messages");
  return g_msgs[err];
}

static i8 atlas_compare_entry(const void* a, const void* b) {
  return compare_stringhash(
      field_ptr(a, AssetAtlasEntry, name), field_ptr(b, AssetAtlasEntry, name));
}

static AssetTextureFlags atlas_texture_flags(const AtlasDef* def, const bool hasAlpha) {
  AssetTextureFlags flags = 0;
  if (def->mipmaps) {
    flags |= AssetTextureFlags_GenerateMipMaps;
  }
  if (def->srgb) {
    flags |= AssetTextureFlags_Srgb;
  }
  if (hasAlpha) {
    flags |= AssetTextureFlags_Alpha;
  }
  return flags;
}

static AssetTexturePixelB4 atlas_color_to_b4(const GeoColor color) {
  static const f32 g_u8MaxPlusOneRoundDown = 255.999f;
  return (AssetTexturePixelB4){
      .r = (u8)(color.r * g_u8MaxPlusOneRoundDown),
      .g = (u8)(color.g * g_u8MaxPlusOneRoundDown),
      .b = (u8)(color.b * g_u8MaxPlusOneRoundDown),
      .a = (u8)(color.a * g_u8MaxPlusOneRoundDown),
  };
}

static f32 atlas_clamp01(const f32 val) {
  if (val < 0.0f) {
    return 0.0f;
  }
  if (val > 1.0f) {
    return 1.0f;
  }
  return val;
}

static void atlas_generate_entry(
    const AtlasDef*         def,
    const AssetTextureComp* texture,
    const u32               index,
    AssetTexturePixelB4*    out) {

  const u32 padding               = def->entryPadding;
  const u32 sizeWithPadding       = def->entrySize;
  const u32 sizeWithoutPadding    = sizeWithPadding - padding * 2;
  const f32 sizeWithoutPaddingInv = 1.0f / sizeWithoutPadding;

  const u32 texY = index * sizeWithPadding / def->size * sizeWithPadding;
  const u32 texX = index * sizeWithPadding % def->size;

  diag_assert(texY + sizeWithPadding <= def->size);
  diag_assert(texX + sizeWithPadding <= def->size);

  for (u32 entryPixelY = 0; entryPixelY != sizeWithPadding; ++entryPixelY) {
    const f32 yNorm = atlas_clamp01((entryPixelY - padding + 0.5f) * sizeWithoutPaddingInv);
    for (u32 entryPixelX = 0; entryPixelX != sizeWithPadding; ++entryPixelX) {
      const u32 layer = 0;
      const f32 xNorm = atlas_clamp01((entryPixelX - padding + 0.5f) * sizeWithoutPaddingInv);

      GeoColor color = asset_texture_sample(texture, xNorm, yNorm, layer);
      if (def->srgb) {
        color = geo_color_linear_to_srgb(color);
      }

      const usize outTexPixelY                     = texY + entryPixelY;
      const usize outTexPixelX                     = texX + entryPixelX;
      out[outTexPixelY * def->size + outTexPixelX] = atlas_color_to_b4(color);
    }
  }
}

static void atlas_generate(
    const AtlasDef*          def,
    const AssetTextureComp** textures,
    AssetAtlasComp*          outAtlas,
    AssetTextureComp*        outTexture,
    AtlasError*              err) {

  // Validate textures.
  for (u32 i = 0; i != def->entries.count; ++i) {
    if (UNLIKELY(textures[i]->layers > 1)) {
      *err = AtlasError_EntryTextureLayerCountUnsupported;
      return;
    }
  }

  // Allocate output texture.
  Mem pixelMem = alloc_alloc(g_alloc_heap, sizeof(AssetTexturePixelB4) * def->size * def->size, 4);
  mem_set(pixelMem, 0); // Initialize to black.
  bool hasAlpha = false;

  const u32        entryCount = (u32)def->entries.count;
  AssetAtlasEntry* entries    = alloc_array_t(g_alloc_heap, AssetAtlasEntry, entryCount);

  // Render entries into output texture.
  AssetTexturePixelB4* pixels = pixelMem.ptr;
  for (u32 i = 0; i != def->entries.count; ++i) {
    if (textures[i]->flags & AssetTextureFlags_Alpha) {
      hasAlpha = true;
    }
    atlas_generate_entry(def, textures[i], i, pixels);
    entries[i] = (AssetAtlasEntry){
        .name       = string_hash(def->entries.values[i].name),
        .atlasIndex = i,
    };
  }

  // Sort the entries on their name hash.
  sort_quicksort_t(entries, entries + entryCount, AssetAtlasEntry, atlas_compare_entry);

  *outAtlas = (AssetAtlasComp){
      .entriesPerDim = def->size / def->entrySize,
      .entryPadding  = def->entryPadding / (f32)def->size,
      .entries       = entries,
      .entryCount    = entryCount,
  };
  *outTexture = (AssetTextureComp){
      .type         = AssetTextureType_U8,
      .channels     = AssetTextureChannels_Four,
      .flags        = atlas_texture_flags(def, hasAlpha),
      .pixelsB4     = pixels,
      .width        = def->size,
      .height       = def->size,
      .layers       = 1,
      .srcMipLevels = 1,
      .maxMipLevels = def->maxMipMaps,
  };
  *err = AtlasError_None;
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(LoadView) { ecs_access_write(AssetAtlasLoadComp); }
ecs_view_define(TextureView) { ecs_access_read(AssetTextureComp); }

/**
 * Update all active loads.
 */
ecs_system_define(AtlasLoadAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }
  EcsView*     loadView   = ecs_world_view_t(world, LoadView);
  EcsIterator* textureItr = ecs_view_itr(ecs_world_view_t(world, TextureView));

  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId   entity = ecs_view_entity(itr);
    AssetAtlasLoadComp* load   = ecs_view_write_t(itr, AssetAtlasLoadComp);
    AtlasError          err;

    /**
     * Start loading all entry textures.
     */
    if (!load->textures.size) {
      array_ptr_for_t(load->def.entries, AtlasEntryDef, entryDef) {
        const EcsEntityId texAsset = asset_lookup(world, manager, entryDef->texture);
        *dynarray_push_t(&load->textures, EcsEntityId) = texAsset;
        asset_acquire(world, texAsset);
        asset_register_dep(world, entity, texAsset);
      }
    }

    /**
     * Gather all textures.
     */
    const AssetTextureComp** textures = mem_stack(sizeof(void*) * load->textures.size).ptr;
    for (usize i = 0; i != load->textures.size; ++i) {
      const EcsEntityId texAsset = *dynarray_at_t(&load->textures, i, EcsEntityId);
      if (ecs_world_has_t(world, texAsset, AssetFailedComp)) {
        err = AtlasError_InvalidTexture;
        goto Error;
      }
      if (!ecs_world_has_t(world, texAsset, AssetLoadedComp)) {
        goto Next; // Wait for the texture to be loaded.
      }
      if (UNLIKELY(!ecs_view_maybe_jump(textureItr, texAsset))) {
        err = AtlasError_InvalidTexture;
        goto Error;
      }
      textures[i] = ecs_view_read_t(textureItr, AssetTextureComp);
    }

    AssetAtlasComp   atlas;
    AssetTextureComp texture;
    atlas_generate(&load->def, textures, &atlas, &texture, &err);
    if (UNLIKELY(err)) {
      goto Error;
    }

    *ecs_world_add_t(world, entity, AssetAtlasComp)   = atlas;
    *ecs_world_add_t(world, entity, AssetTextureComp) = texture;
    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    goto Cleanup;

  Error:
    log_e("Failed to load atlas texture", log_param("error", fmt_text(atlas_error_str(err))));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);

  Cleanup:
    ecs_world_remove_t(world, entity, AssetAtlasLoadComp);
    dynarray_for_t(&load->textures, EcsEntityId, texAsset) { asset_release(world, *texAsset); }

  Next:
    continue;
  }
}

ecs_view_define(AtlasUnloadView) {
  ecs_access_with(AssetAtlasComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any atlas-asset component for unloaded assets.
 */
ecs_system_define(AtlasUnloadAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, AtlasUnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetAtlasComp);
  }
}

ecs_module_init(asset_atlas_module) {
  atlas_datareg_init();

  ecs_register_comp(AssetAtlasComp, .destructor = ecs_destruct_atlas_comp);
  ecs_register_comp(AssetAtlasLoadComp, .destructor = ecs_destruct_atlas_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(TextureView);
  ecs_register_view(AtlasUnloadView);

  ecs_register_system(
      AtlasLoadAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView), ecs_view_id(TextureView));

  ecs_register_system(AtlasUnloadAssetSys, ecs_view_id(AtlasUnloadView));
}

void asset_load_atlas(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  String         errMsg;
  AtlasDef       def;
  DataReadResult result;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataAtlasDefMeta, mem_var(def), &result);

  if (UNLIKELY(result.error)) {
    errMsg = result.errorMsg;
    goto Error;
  }
  if (UNLIKELY(!bits_ispow2(def.size))) {
    errMsg = atlas_error_str(AtlasError_SizeNonPow2);
    goto Error;
  }
  if (UNLIKELY(def.size > atlas_max_size)) {
    errMsg = atlas_error_str(AtlasError_SizeTooBig);
    goto Error;
  }
  if (UNLIKELY(!bits_ispow2(def.entrySize))) {
    errMsg = atlas_error_str(AtlasError_EntrySizeNonPow2);
    goto Error;
  }
  if (UNLIKELY(def.entryPadding * 2 >= def.entrySize)) {
    errMsg = atlas_error_str(AtlasError_EntryPaddingTooBig);
    goto Error;
  }
  if (UNLIKELY(!def.entries.count)) {
    errMsg = atlas_error_str(AtlasError_NoEntries);
    goto Error;
  }
  const u32 entriesPerDim = def.size / def.entrySize;
  const u32 maxEntries    = entriesPerDim * entriesPerDim;
  if (UNLIKELY(def.entries.count > maxEntries)) {
    errMsg = atlas_error_str(AtlasError_TooManyEntries);
    goto Error;
  }
  array_ptr_for_t(def.entries, String, texName) {
    if (UNLIKELY(string_is_empty(*texName))) {
      errMsg = atlas_error_str(AtlasError_InvalidTexture);
      goto Error;
    }
  }

  ecs_world_add_t(
      world,
      entity,
      AssetAtlasLoadComp,
      .def        = def,
      .maxEntries = maxEntries,
      .textures   = dynarray_create_t(g_alloc_heap, EcsEntityId, def.entries.count));
  asset_repo_source_close(src);
  return;

Error:
  log_e("Failed to load atlas texture", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
  data_destroy(g_dataReg, g_alloc_heap, g_dataAtlasDefMeta, mem_var(def));
  asset_repo_source_close(src);
}

const AssetAtlasEntry* asset_atlas_lookup(const AssetAtlasComp* atlas, const StringHash name) {
  const AssetAtlasEntry target = {.name = name};
  return search_binary_t(
      atlas->entries,
      atlas->entries + atlas->entryCount,
      AssetAtlasEntry,
      atlas_compare_entry,
      &target);
}

void asset_atlas_jsonschema_write(DynString* str) {
  atlas_datareg_init();

  const DataJsonSchemaFlags schemaFlags = DataJsonSchemaFlags_Compact;
  data_jsonschema_write(g_dataReg, str, g_dataAtlasDefMeta, schemaFlags);
}
