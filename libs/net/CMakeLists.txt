# --------------------------------------------------------------------------------------------------
# Network library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(net STATIC
  src/addr.c
  src/http.c
  src/init.c
  src/pal.c
  src/rest.c
  src/result.c
  src/socket.c
  src/stats.c
  src/tls.c
  )
target_include_directories(net PUBLIC include)
target_link_libraries(net PUBLIC core)
target_link_libraries(net PRIVATE log)

add_executable(test_net
  test/config.c
  test/test_addr.c
  test/test_http.c
  test/test_socket.c
  )
target_link_libraries(test_net PRIVATE app_check net)

configure_debuggable(test_net)
