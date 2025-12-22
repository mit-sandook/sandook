message(STATUS "Building read_write_blk_dev")

include(FetchContent)
FetchContent_Declare(
    argparse
    GIT_REPOSITORY https://github.com/p-ranav/argparse.git
)
FetchContent_MakeAvailable(argparse)

add_executable(read_write_blk_dev
  run.cc
)
target_link_libraries(read_write_blk_dev
  argparse
)
