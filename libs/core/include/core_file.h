#pragma once
#include "core_alloc.h"
#include "core_dynstring.h"
#include "core_string.h"

/**
 * Handle to an open os file.
 */
typedef struct sFile File;

/**
 * File Result code.
 */
typedef enum {
  FileResult_Success = 0,
  FileResult_AlreadyExists,
  FileResult_DiskFull,
  FileResult_InvalidFilename,
  FileResult_Locked,
  FileResult_NoAccess,
  FileResult_NoDataAvailable,
  FileResult_NotFound,
  FileResult_PathTooLong,
  FileResult_PathInvalid,
  FileResult_TooManyOpenFiles,
  FileResult_UnknownError,

  FileResult_Count,
} FileResult;

/**
 * Mode to open a file with.
 * - Open: Open an existing file.
 *    > fails if the file does not exist.
 *    > Head is at the start.
 * - Append: Append to an existing file or create a new file.
 *    > Head is at the end.
 * - Create: Open an existing file or create a new file.
 *    > Head is at the start.
 */
typedef enum {
  FileMode_Open,
  FileMode_Append,
  FileMode_Create,
} FileMode;

/**
 * Access to request when opening a file.
 */
typedef enum {
  FileAccess_Read  = 1 << 0,
  FileAccess_Write = 1 << 1,
} FileAccessFlags;

extern File* g_file_stdin;
extern File* g_file_stdout;
extern File* g_file_stderr;

/**
 * Return a textual result message for a given FileResult.
 */
String file_result_str(FileResult);

/**
 * Create a file handle.
 * On success a file will be assigned to the file pointer. Retrieved file pointer should be
 * destroyed with 'file_destroy'.
 */
FileResult file_create(Allocator*, String path, FileMode, FileAccessFlags, File** file);

/**
 * Create a temporary file.
 * On success a file will be assigned to the file pointer. Retrieved file pointer should be
 * destroyed with 'file_destroy'.
 */
FileResult file_temp(Allocator*, File** file);

/**
 * Destroy a file handle.
 * Note: Does not destroy the file from the file-system.
 */
void file_destroy(File*);

/**
 * Synchronously write a string to a file.
 */
FileResult file_write_sync(File*, String);

/**
 * Synchronously read a block of available data in the dynamic-string.
 * Note: returns 'FileResult_NoDataAvailable' when the end of the file has been reached.
 */
FileResult file_read_sync(File*, DynString*);

/**
 * Synchronously seek an open file to the specified position.
 */
FileResult file_seek_sync(File*, usize position);

/**
 * Synchronously delete a file from the file-system.
 */
FileResult file_delete_sync(String path);
