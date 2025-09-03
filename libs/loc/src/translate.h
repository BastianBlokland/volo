#pragma once
#include "asset/locale.h"

/**
 * Update the global translation source entries.
 * NOTE: The entries are NOT copied, allocation has to remain stable (and immutable) until cleared.
 * NOTE: To clear the source provide 'null' as the source.
 */
void loc_translate_source_set(const AssetLocaleText* entries, usize entryCount);
