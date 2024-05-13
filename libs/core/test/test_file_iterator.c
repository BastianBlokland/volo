#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_file_iterator.h"
#include "core_path.h"
#include "core_rng.h"

spec(file_iterator) {

  FileIteratorEntry entry;
  String            dirPath;

  setup() {
    const String dirName = path_name_random_scratch(g_rng, string_lit("volo"), string_empty);
    dirPath              = path_build_scratch(g_pathTempDir, dirName);

    if (file_create_dir_sync(dirPath) != FileResult_Success) {
      diag_crash_msg("file_iterator: Failed to setup test directory");
    }
  }

  it("finds zero entries in an empty directory") {
    FileIterator* itr = file_iterator_create(g_allocHeap, dirPath);

    // Assert end of iterator.
    check_eq_int(file_iterator_next(itr, &entry), FileIteratorResult_End);

    file_iterator_destroy(itr);
  }

  it("finds a single entry in a directory with one file") {
    static const String g_fileName = string_static("hello-world.txt");
    const String        filePath   = path_build_scratch(dirPath, g_fileName);

    check_eq_int(file_write_to_path_sync(filePath, string_lit("Hello World")), FileResult_Success);

    FileIterator* itr = file_iterator_create(g_allocHeap, dirPath);

    // Assert we find our file.
    check_eq_int(file_iterator_next(itr, &entry), FileIteratorResult_Found);
    check_eq_string(entry.name, g_fileName);
    check_eq_int(entry.type, FileType_Regular);

    // Assert end of iterator.
    check_eq_int(file_iterator_next(itr, &entry), FileIteratorResult_End);

    file_iterator_destroy(itr);
    check_eq_int(file_delete_sync(filePath), FileResult_Success);
  }

  it("finds a single entry in a directory with one sub-directory") {
    static const String g_subDirName = string_static("sub-directory");
    const String        subDirPath   = path_build_scratch(dirPath, g_subDirName);

    check_eq_int(file_create_dir_sync(subDirPath), FileResult_Success);

    FileIterator* itr = file_iterator_create(g_allocHeap, dirPath);

    // Assert we find our sub-directory.
    check_eq_int(file_iterator_next(itr, &entry), FileIteratorResult_Found);
    check_eq_string(entry.name, g_subDirName);
    check_eq_int(entry.type, FileType_Directory);

    // Assert end of iterator.
    check_eq_int(file_iterator_next(itr, &entry), FileIteratorResult_End);

    file_iterator_destroy(itr);
    check_eq_int(file_delete_dir_sync(subDirPath), FileResult_Success);
  }

  it("can find multiple files in a directory") {
    static const String g_fileNames[] = {
        string_static("a"),
        string_static("b"),
        string_static("c"),
        string_static("d"),
    };
    array_for_t(g_fileNames, String, name) {
      const String filePath = path_build_scratch(dirPath, *name);
      check_eq_int(file_write_to_path_sync(filePath, *name), FileResult_Success);
    }

    FileIterator* itr = file_iterator_create(g_allocHeap, dirPath);

    // Try to find all files.
    u32 foundFiles = 0;
    for (u32 i = 0; i != array_elems(g_fileNames); ++i) {
      check_eq_int(file_iterator_next(itr, &entry), FileIteratorResult_Found);
      check_eq_int(entry.type, FileType_Regular);

      for (u32 expectedIdx = 0; expectedIdx != array_elems(g_fileNames); ++expectedIdx) {
        if (string_eq(entry.name, g_fileNames[expectedIdx])) {
          foundFiles |= 1 << expectedIdx;
          break;
        }
      }
    }

    // Assert all files are found.
    check_eq_int(bits_popcnt(foundFiles), array_elems(g_fileNames));

    // Assert end of iterator.
    check_eq_int(file_iterator_next(itr, &entry), FileIteratorResult_End);

    file_iterator_destroy(itr);

    array_for_t(g_fileNames, String, name) {
      const String filePath = path_build_scratch(dirPath, *name);
      check_eq_int(file_delete_sync(filePath), FileResult_Success);
    }
  }

  it("fails when iterating a directory that does not exist") {
    FileIterator* itr = file_iterator_create(g_allocHeap, string_lit("does-not-exist-42"));

    // Assert error.
    check_eq_int(file_iterator_next(itr, &entry), FileIteratorResult_DirectoryDoesNotExist);

    // Assert that the same error is returned on sequential calls.
    check_eq_int(file_iterator_next(itr, &entry), FileIteratorResult_DirectoryDoesNotExist);

    file_iterator_destroy(itr);
  }

  it("fails when iterating a regular file") {
    FileIterator* itr = file_iterator_create(g_allocHeap, g_pathExecutable);

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
