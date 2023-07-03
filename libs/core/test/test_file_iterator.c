#include "check_spec.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_file_iterator.h"
#include "core_path.h"
#include "core_rng.h"

spec(file_iterator) {

  FileIteratorEntry entry;
  String            dirPath;

  setup() {
    const String dirName = path_name_random_scratch(g_rng, string_lit("volo"), string_empty);
    dirPath              = path_build_scratch(g_path_tempdir, dirName);

    if (file_create_dir_sync(dirPath) != FileResult_Success) {
      diag_crash_msg("file_iterator: Failed to setup test directory");
    }
  }

  it("finds zero entries in an empty directory") {
    FileIterator* itr = file_iterator_create(g_alloc_heap, dirPath);

    // Assert end of iterator.
    check_eq_int(file_iterator_next(itr, &entry), FileIteratorResult_End);

    file_iterator_destroy(itr);
  }

  it("finds a single entry in a directory with one file") {
    static const String g_fileName = string_static("hello-world.txt");
    const String        filePath   = path_build_scratch(dirPath, g_fileName);

    check_eq_int(file_write_to_path_sync(filePath, string_lit("Hello World")), FileResult_Success);

    FileIterator* itr = file_iterator_create(g_alloc_heap, dirPath);

    // Assert we find our file.
    check_eq_int(file_iterator_next(itr, &entry), FileIteratorResult_Found);
    check_eq_string(entry.name, g_fileName);
    check_eq_int(entry.type, FileType_Regular);

    // Assert end of iterator.
    check_eq_int(file_iterator_next(itr, &entry), FileIteratorResult_End);

    check_eq_int(file_delete_sync(filePath), FileResult_Success);
    file_iterator_destroy(itr);
  }

  it("finds a single entry in a directory with one sub-directory") {
    static const String g_subDirName = string_static("sub-directory");
    const String        subDirPath   = path_build_scratch(dirPath, g_subDirName);

    check_eq_int(file_create_dir_sync(subDirPath), FileResult_Success);

    FileIterator* itr = file_iterator_create(g_alloc_heap, dirPath);

    // Assert we find our sub-directory.
    check_eq_int(file_iterator_next(itr, &entry), FileIteratorResult_Found);
    check_eq_string(entry.name, g_subDirName);
    check_eq_int(entry.type, FileType_Directory);

    // Assert end of iterator.
    check_eq_int(file_iterator_next(itr, &entry), FileIteratorResult_End);

    check_eq_int(file_delete_dir_sync(subDirPath), FileResult_Success);
    file_iterator_destroy(itr);
  }

  it("fails when iterating a directory that does not exist") {
    FileIterator* itr = file_iterator_create(g_alloc_heap, string_lit("does-not-exist-42"));

    // Assert error.
    check_eq_int(file_iterator_next(itr, &entry), FileIteratorResult_DirectoryDoesNotExist);

    // Assert that the same error is returned on sequential calls.
    check_eq_int(file_iterator_next(itr, &entry), FileIteratorResult_DirectoryDoesNotExist);

    file_iterator_destroy(itr);
  }

  it("fails when iterating a regular file") {
    FileIterator* itr = file_iterator_create(g_alloc_heap, g_path_executable);

    // Assert error.
    check_eq_int(file_iterator_next(itr, &entry), FileIteratorResult_PathIsNotADirectory);

    // Assert that the same error is returned on sequential calls.
    check_eq_int(file_iterator_next(itr, &entry), FileIteratorResult_PathIsNotADirectory);

    file_iterator_destroy(itr);
  }

  teardown() {
    if (file_delete_dir_sync(dirPath) != FileResult_Success) {
      diag_crash_msg("file_iterator: Failed to cleanup test directory");
    }
  }
}
