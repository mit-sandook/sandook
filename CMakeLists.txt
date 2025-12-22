cmake_minimum_required(VERSION 3.24)
project(sandook C CXX ASM)

# System configurations
set(SPDK_PCI_ADDR "0000:81:00.0") # The PCI address of the NVMe SSD to attach

################################################################################

if (CMAKE_C_COMPILER_VERSION VERSION_LESS 13)
  message(FATAL_ERROR "GCC version must be at least 13!  " ${CMAKE_C_COMPILER_VERSION})
endif()

if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
  message(FATAL_ERROR "G++ version must be at least 13!  " ${CMAKE_CXX_COMPILER_VERSION})
endif()

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_COLOR_MAKEFILE ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_CXX_STANDARD 23)
set(THREADS_PREFER_PTHREAD_FLAG ON)
SET(ASM_OPTIONS "-x assembler-with-cpp")
SET(CMAKE_ASM_FLAGS "${CFLAGS} ${ASM_OPTIONS}")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -g -fno-stack-protector -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "-DDEBUG")

if (DISABLE_LTO)
  set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O3 -ffast-math")
  message(STATUS "LTO is disabled.")
else()
  set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O3 -flto=auto -ffast-math")
  message(STATUS "LTO is enabled.")
endif()

# Get GoogleTest
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG release-1.12.1
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Prepare for tests
enable_testing()
set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE POST_BUILD)

include_directories(
  ${CMAKE_SOURCE_DIR}/lib/ubdsrv/include
  ${CMAKE_SOURCE_DIR}/lib/caladan/inc
  ${CMAKE_SOURCE_DIR}/lib/caladan/bindings/cc
  ${CMAKE_SOURCE_DIR}
)

add_subdirectory(lib)
add_subdirectory(sandook)
