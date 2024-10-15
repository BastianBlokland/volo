# --------------------------------------------------------------------------------------------------
# Script library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: script")

add_library(lib_script STATIC
  src/args.c
  src/binder.c
  src/compile.c
  src/diag.c
  src/doc.c
  src/enum.c
  src/error.c
  src/eval.c
  src/format.c
  src/intrinsic.c
  src/lex.c
  src/mem.c
  src/optimize.c
  src/panic.c
  src/pos.c
  src/read.c
  src/sig.c
  src/sym.c
  src/val.c
  src/vm.c
  )
target_include_directories(lib_script PUBLIC include)
target_link_libraries(lib_script PUBLIC lib_core lib_geo lib_ecs)
target_link_libraries(lib_script PRIVATE lib_json)

add_executable(test_lib_script
  test/config.c
  test/test_binder.c
  test/test_doc.c
  test/test_enum.c
  test/test_eval.c
  test/test_format.c
  test/test_lex.c
  test/test_mem.c
  test/test_read.c
  test/test_sig.c
  test/test_val.c
  test/test_vm.c
  test/utils.c
  )
target_link_libraries(test_lib_script PRIVATE lib_app_check lib_script)

configure_test(test_lib_script SHORT_NAME script)
configure_debuggable(test_lib_script)
