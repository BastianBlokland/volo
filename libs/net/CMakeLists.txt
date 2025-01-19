# --------------------------------------------------------------------------------------------------
# Network library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: net")

add_library(lib_net STATIC
  src/dns.c
  src/init.c
  src/pal.c
  )
target_include_directories(lib_net PUBLIC include)
target_link_libraries(lib_net PUBLIC lib_core)
