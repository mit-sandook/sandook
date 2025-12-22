message(STATUS "Building bindings")

add_library(sandook_bindings
  thread.cc
  timer.cc
  log.cc
)
target_link_libraries(sandook_bindings PUBLIC
  caladan_cc_bindings
  caladan_runtime
)
