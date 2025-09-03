#pragma once
#include "asset/locale.h"

/**
 * Update the global translation source.
 * NOTE: Asset has to remain acquired while its set as the global translation source, before
 * releasing the asset call 'loc_translate_source_unset'.
 */
void loc_translate_source_set(EcsEntityId localeAsset, const AssetLocaleComp*);

/**
 * Unset the given asset as the global translation source.
 * NOTE: Does nothing if asset is not currently the global translation source.
 */
void loc_translate_source_unset(EcsEntityId localeAsset);
