#pragma once
#include "asset/locale.h"

/**
 * Update the global translation source entries.
 * NOTE: The entries are NOT copied, allocation has to remain stable (and immutable) until cleared.
 */
void loc_translate_source_set(const AssetLocaleText* entries, usize entryCount);

/**
 * Unset the given entries as the global translation source.
 * NOTE: Does nothing if 'entries' is not currently the global translation source.
 */
void loc_translate_source_unset(const AssetLocaleText* entries);
