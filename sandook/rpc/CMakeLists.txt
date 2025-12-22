message(STATUS "Building rpc")

add_library(rpc STATIC
  rpc.cc
)

target_link_libraries(rpc
  mem
  sandook_base
  sandook_bindings
)
