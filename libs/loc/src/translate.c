#include "core/search.h"
#include "core/string.h"
#include "core/thread.h"
#include "loc/translate.h"

#include "translate.h"

static ThreadSpinLock         g_locTranslateLock;
static const AssetLocaleText* g_locTranslateEntries;
static usize                  g_locTranslateEntryCount;

void loc_translate_source_set(const AssetLocaleText* entries, const usize entryCount) {
  thread_spinlock_lock(&g_locTranslateLock);
  {
    g_locTranslateEntries    = entries;
    g_locTranslateEntryCount = entryCount;
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
