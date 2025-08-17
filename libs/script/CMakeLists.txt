# --------------------------------------------------------------------------------------------------
# Script library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(script STATIC
  src/args.c
  src/binder.c
  src/compile.c
  src/diag.c
  src/doc.c
  src/enum.c
  src/eval.c
  src/format.c
  src/intrinsic.c
  src/lex.c
  src/mem.c
  src/optimize.c
  src/panic.c
  src/pos.c
  src/prog.c
  src/read.c
  src/sig.c
  src/sym.c
  src/val.c
  )
target_include_directories(script PUBLIC include)
target_link_libraries(script PUBLIC core geo ecs)
target_link_libraries(script PRIVATE json)

add_executable(test_script
  test/config.c
  test/test_binder.c
  test/test_doc.c
  test/test_enum.c
  test/test_eval.c
  test/test_format.c
  test/test_lex.c
  test/test_mem.c
  test/test_optimize.c
  test/test_prog.c
  test/test_read.c
  test/test_sig.c
  test/test_val.c
  test/utils.c
  )
target_link_libraries(test_script PRIVATE app_check script)

configure_debuggable(test_script)
