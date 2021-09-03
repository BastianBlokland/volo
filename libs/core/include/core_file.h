#pragma once
#include "core_alloc.h"
#include "core_dynstring.h"
#include "core_string.h"

// Forward declare from 'core_time.h'.
typedef i64 TimeReal;

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
  FileResult_IsDirectory,
  FileResult_AllocationFailed,
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
  FileAccess_None  = 0,
  FileAccess_Read  = 1 << 0,
  FileAccess_Write = 1 << 1,
} FileAccessFlags;

/**
 * File Type code.
 */
typedef enum {
  FileType_Regular,
  FileType_Directory,
  FileType_Unknown,
} FileType;

/**
 * Output structure for 'file_stat_sync'.
 */
typedef struct {
  usize    size;
  FileType type;
  TimeReal accessTime, modTime;
} FileInfo;

extern File* g_file_stdin;
extern File* g_file_stdout;
extern File* g_file_stderr;

/**
 * Return a textual representation of the given FileResult.
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
 * Keep reading synchronously into the dynamic-string until the end of the file is reached.
 */
FileResult file_read_to_end_sync(File*, DynString*);

/**
 * Synchronously seek an open file to the specified position.
 */
FileResult file_seek_sync(File*, usize position);

/**
 * Synchronously retrieve information about a file.
 */
FileInfo file_stat_sync(File*);

/**
 * Synchronously delete a file from the file-system.
 */
FileResult file_delete_sync(String path);

/**
 * Synchronously delete a directory from the file-system.
 */
FileResult file_delete_dir_sync(String path);

/**
 * Memory map the given file.
 * On success the mapped memory will be assigned to the output pointer. Memory mappings are
 * automatically closed when the file-handle is destroyed.
 *
 * Pre-condition: file has not been mapped yet.
 */
FileResult file_map(File*, String* output);

/**
 * Synchronously create a new file-system directory.
 * Note: Will also create the parent directory if its missing.
 */
FileResult file_create_dir_sync(String path);
