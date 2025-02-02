# --------------------------------------------------------------------------------------------------
# Network library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: net")

add_library(lib_net STATIC
  src/addr.c
  src/http.c
  src/init.c
  src/pal.c
  src/rest.c
  src/result.c
  src/stats.c
  src/tls.c
  )
target_include_directories(lib_net PUBLIC include)
target_link_libraries(lib_net PUBLIC lib_core)
target_link_libraries(lib_net PRIVATE lib_log)

add_executable(test_lib_net
  test/config.c
  test/test_addr.c
  test/test_http.c
  test/test_socket.c
  )
target_link_libraries(test_lib_net PRIVATE lib_app_check lib_net)

configure_test(test_lib_net SHORT_NAME net)
configure_debuggable(test_lib_net)
