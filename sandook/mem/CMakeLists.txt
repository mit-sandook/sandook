message(STATUS "Building mem")

add_library(mem STATIC
  slab.cc
  mem_hook.cc
)

target_link_libraries(mem
  sandook_base
  sandook_bindings
)
