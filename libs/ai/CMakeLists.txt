# --------------------------------------------------------------------------------------------------
# AI library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: ai")

add_library(lib_ai STATIC
  src/blackboard.c
  src/eval.c
  src/node/failure.c
  src/node/invert.c
  src/node/knowledgecheck.c
  src/node/knowledgeclear.c
  src/node/knowledgecompare.c
  src/node/knowledgeset.c
  src/node/parallel.c
  src/node/repeat.c
  src/node/running.c
  src/node/selector.c
  src/node/sequence.c
  src/node/source.c
  src/node/success.c
  src/node/try.c
  src/result.c
  src/tracer_count.c
  src/tracer_record.c
  src/value.c
  )
target_include_directories(lib_ai PUBLIC include)
target_link_libraries(lib_ai PUBLIC lib_core lib_asset lib_geo)
target_link_libraries(lib_ai PRIVATE lib_log)

add_executable(test_lib_ai
  test/config.c
  test/test_blackboard.c
  test/test_node_failure.c
  test/test_node_invert.c
  test/test_node_knowledgecheck.c
  test/test_node_knowledgeclear.c
  test/test_node_knowledgecompare.c
  test/test_node_knowledgeset.c
  test/test_node_parallel.c
  test/test_node_repeat.c
  test/test_node_running.c
  test/test_node_selector.c
  test/test_node_sequence.c
  test/test_node_success.c
  test/test_node_try.c
  test/test_tracer_record.c
  test/test_value.c
  test/utils.c
  )
target_link_libraries(test_lib_ai PRIVATE lib_app_check lib_ai)

configure_test(test_lib_ai SHORT_NAME ai)
configure_debuggable(test_lib_ai)
