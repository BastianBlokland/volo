#pragma once
#include "core_string.h"

/**
 * Check if the given asset-id is a normalmap.
 * NOTE: This uses a naming convention based detection (ending with nrm or normal).
 */
bool asset_texture_is_normalmap(String id);
