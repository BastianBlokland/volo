#pragma once
#include "core/forward.h"
#include "core/macro.h"

/**
 * Build an absolute path by combining the given segment strings.
 * If the first segment does not start from a filesystem root then the working dir is prepended.
 */
#define path_build(_DYNSTRING_, ...)                                                               \
  path_build_raw(                                                                                  \
      (_DYNSTRING_), (const String[]){VA_ARGS_SKIP_FIRST(0, ##__VA_ARGS__, string_empty)})

/**
 * Build an absolute path by combining the given segment strings.
 * If the first segment does not start from a filesystem root then the working dir is prepended.
 */
#define path_build_scratch(...)                                                                    \
  path_build_scratch_raw((const String[]){VA_ARGS_SKIP_FIRST(0, ##__VA_ARGS__, string_empty)})

/**
 * Working directory of the process.
 * NOTE: Cached at startup.
 */
extern String g_pathWorkingDir;

/**
 * Path to the running executable.
 * NOTE: Cached at startup.
 */
extern String g_pathExecutable;

/**
 * Path to the system's temporary directory.
 * NOTE: Cached at startup.
 */
extern String g_pathTempDir;

/**
 * Check if the given path is absolute (starts from a root directory).
 */
bool path_is_absolute(String);

/**
 * Check if the given path is a root directory.
 */
bool path_is_root(String);

/**
 * Retrieve the filename for the given path (last path segment).
 */
String path_filename(String);

/**
 * Retrieve the file extension for the given path.
 * Returns empty string if no extention was found.
 */
String path_extension(String);

/**
 * Retrieve the file name without extension for the given path.
 */
String path_stem(String);

/**
 * Retrieve the parent directory of the given path.
 */
String path_parent(String);

/**
 * Convert a path into a canonical form.
 * - Converts the segment separators into '/'.
 * - Converts windows drive-letters into uppercase.
 * - Flattens any '.' and '..' segments.
 * - Removes trailing separators.
 *
 * NOTE: Only performs basic lexical canonization, does NOT resolve symlinks, or validate that the
 * path is compatible with the underlying filesystem.
 * Return false if there was no canonical form possible.
 */
bool   path_canonize(DynString*, String path);
String path_canonize_scratch(String path);

/**
 * Append a new segment to a path. Will insert a '/' separator if required.
 */
void path_append(DynString*, String path);

/**
 * Build an absolute path by combining a list of segments.
 * If the first segment does not start from a filesystem root then the working dir is prepended.
 *
 * Pre-condition: 'segments' array should be terminated with an empty string (at least an pointer
 * sized section of 0 bytes).
 */
void path_build_raw(DynString*, const String* segments);

/**
 * Build an absolute path in scratch memory by combining a list of segments.
 * If the first segment does not start from a filesystem root then the working dir is prepended.
 *
 * Pre-condition: 'segments' array should be terminated with an empty string (at least an pointer
 * sized section of 0 bytes).
 */
String path_build_scratch_raw(const String* segments);

/**
 * Generate a random file name.
 * Useful for avoiding name collisions, should not be used for anything security related.
 * NOTE: Prefix and extension are optional.
 */
void path_name_random(DynString*, Rng*, String prefix, String extension);

/**
 * Generate a random file name into a scratch buffer.
 * Useful for avoiding name collisions, should not be used for anything security related.
 * NOTE: Prefix and extension are optional.
 */
String path_name_random_scratch(Rng*, String prefix, String extension);

/**
 * Generate a timestamped file name.
 * NOTE: Prefix and extension are optional.
 */
void path_name_timestamp(DynString*, String prefix, String extension);

/**
 * Generate a timestamped file name into a scratch buffer.
 * NOTE: Prefix and extension are optional.
 */
String path_name_timestamp_scratch(String prefix, String extension);
