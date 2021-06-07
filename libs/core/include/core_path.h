#pragma once
#include "core_dynstring.h"

/**
 * Working directory of the process.
 * Note: Cached at startup
 */
extern String g_path_workingdir;

/**
 * Path to the running executable.
 * Note: Cached at startup
 */
extern String g_path_executable;

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
 * - Converts the segment seperators into '/'.
 * - Converts windows drive-letters into uppercase.
 * - Flattens any '.' and '..' segments.
 * - Removes trailing seperators.
 *
 * Note: Only performs basic lexical canonization, does NOT resolve symlinks, or validate that the
 * path is compatible with the underlying filesystem.
 * Return false if there was no canonical form possible.
 */
bool path_canonize(DynString*, String path);

/**
 * Append a new segment to a path. Will insert a '/' seperator if required.
 */
void path_append(DynString*, String path);
