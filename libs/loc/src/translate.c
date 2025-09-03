#include "core/search.h"
#include "core/string.h"
#include "core/thread.h"
#include "ecs/entity.h"
#include "loc/translate.h"

#include "translate.h"

static ThreadSpinLock         g_locTranslateLock;
static EcsEntityId            g_locTranslateAsset;
static const AssetLocaleText* g_locTranslateEntries;
static usize                  g_locTranslateEntryCount;

void loc_translate_source_set(const EcsEntityId localeAsset, const AssetLocaleComp* localeComp) {
  thread_spinlock_lock(&g_locTranslateLock);
  {
    /**
     * NOTE: Its important to store the entry pointers instead of storing a pointer to the
     * localeComp as the ECS component pointers are not stable across frames.
     */
    g_locTranslateAsset      = localeAsset;
    g_locTranslateEntries    = localeComp->textEntries.values;
    g_locTranslateEntryCount = localeComp->textEntries.count;
  }
  thread_spinlock_unlock(&g_locTranslateLock);
}

void loc_translate_source_unset(const EcsEntityId localeAsset) {
  thread_spinlock_lock(&g_locTranslateLock);
  {
    if (g_locTranslateAsset == localeAsset) {
      g_locTranslateAsset      = ecs_entity_invalid;
      g_locTranslateEntries    = null;
      g_locTranslateEntryCount = 0;
    }
  }
  thread_spinlock_unlock(&g_locTranslateLock);
}

String loc_translate(const StringHash key) {
  const AssetLocaleText tgt = {.key = key};

  String result = string_empty;
  thread_spinlock_lock(&g_locTranslateLock);
  {
    const AssetLocaleText* entry = search_binary_t(
        g_locTranslateEntries,
        g_locTranslateEntries + g_locTranslateEntryCount,
        AssetLocaleText,
        asset_locale_text_compare,
        &tgt);
    if (LIKELY(entry)) {
      result = entry->value;
    }
  }
  thread_spinlock_unlock(&g_locTranslateLock);
  return result;
}
