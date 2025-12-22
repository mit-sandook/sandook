message(STATUS "Building base")

add_library(sandook_base STATIC
  error.cc
  io.cc
  counter.cc
)
