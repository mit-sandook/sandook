include(ExternalProject)
include(ProcessorCount)

message(STATUS "Building caladan")

set(CALADAN_PATH ${CMAKE_SOURCE_DIR}/lib/caladan)

ProcessorCount(NPROC)

set(ENV{ROOT_PATH} ${CALADAN_PATH})

if(EXISTS "${CALADAN_PATH}/libruntime.a")
  set(FOUND_RUNTIME_FILE TRUE)
else()
  message(STATUS "No Caladan libruntime.a found to check LTO status; ignoring")
  set(FOUND_RUNTIME_FILE FALSE)
endif()

# Check if existing Caladan build has LTO enabled
if (FOUND_RUNTIME_FILE)
  execute_process(
    WORKING_DIRECTORY ${CALADAN_PATH}
    COMMAND readelf -a libruntime.a
    COMMAND grep \.lto
    RESULT_VARIABLE GREP_LTO_STATUS
    OUTPUT_QUIET
  )
  if (GREP_LTO_STATUS EQUAL 0)
    set(FOUND_LTO TRUE)
  else()
    set(FOUND_LTO FALSE)
  endif()
  message(STATUS "Existing Caladan build has LTO: ${FOUND_LTO}")
endif()

if (DISABLE_LTO)
  execute_process(
    COMMAND sed -i "s/CONFIG_LTO=[y/n]/CONFIG_LTO=n/g" ${CALADAN_PATH}/build/config
  )
  # LTO should be disabled but the existing build has LTO enabled
  if(NOT FOUND_RUNTIME_FILE OR FOUND_LTO)
    execute_process(
      WORKING_DIRECTORY ${CALADAN_PATH}
      COMMAND make clean
    )
    execute_process(
      WORKING_DIRECTORY ${CALADAN_PATH}/bindings/cc
      COMMAND make clean
    )
  endif()
  message(STATUS "LTO is disabled for Caladan.")
else()
  execute_process(
    COMMAND sed -i "s/CONFIG_LTO=[y/n]/CONFIG_LTO=y/g" ${CALADAN_PATH}/build/config
  )
  # LTO should be enabled but the existing build has LTO disabled
  if(NOT FOUND_RUNTIME_FILE OR NOT FOUND_LTO)
    execute_process(
      WORKING_DIRECTORY ${CALADAN_PATH}
      COMMAND make clean
    )
    execute_process(
      WORKING_DIRECTORY ${CALADAN_PATH}/bindings/cc
      COMMAND make clean
    )
  endif()
  message(STATUS "LTO is enabled for Caladan.")
endif()

ExternalProject_Add(caladan_src
  SOURCE_DIR ${CALADAN_PATH}
  CONFIGURE_COMMAND ""
  BUILD_COMMAND make -j ${NPROC}
  INSTALL_COMMAND ""
  BUILD_IN_SOURCE 1
  BUILD_ALWAYS 1
)

execute_process(
  COMMAND make -f ${CALADAN_PATH}/build/shared.mk print-RUNTIME_LIBS
  OUTPUT_VARIABLE CALADAN_RUNTIME_LIBS
)

execute_process(
  COMMAND make -f ${CALADAN_PATH}/build/shared.mk print-LDFLAGS
  OUTPUT_VARIABLE CALADAN_RUNTIME_LDFLAGS
)

string(REPLACE "RUNTIME_LIBS =" " " CALADAN_RUNTIME_LIBS "${CALADAN_RUNTIME_LIBS}")
separate_arguments(CALADAN_RUNTIME_LIBS UNIX_COMMAND "${CALADAN_RUNTIME_LIBS}")

string(REPLACE "LDFLAGS =" " " CALADAN_RUNTIME_LDFLAGS "${CALADAN_RUNTIME_LDFLAGS}")
separate_arguments(CALADAN_RUNTIME_LDFLAGS UNIX_COMMAND "${CALADAN_RUNTIME_LDFLAGS}")

add_library(caladan_runtime STATIC IMPORTED GLOBAL)
add_dependencies(caladan_runtime caladan_src)
set_target_properties(caladan_runtime PROPERTIES IMPORTED_LOCATION ${CALADAN_PATH}/libruntime.a)
target_link_libraries(caladan_runtime INTERFACE "${CALADAN_RUNTIME_LIBS}")
target_link_options(caladan_runtime INTERFACE "${CALADAN_RUNTIME_LDFLAGS}")

ExternalProject_Add(caladan_shim_src
  SOURCE_DIR ${CALADAN_PATH}/shim
  CONFIGURE_COMMAND ""
  BUILD_COMMAND make -j ${NPROC}
  INSTALL_COMMAND ""
  BUILD_IN_SOURCE 1
  BUILD_ALWAYS 1
)
ExternalProject_Add_StepDependencies(caladan_shim_src build caladan_src)

add_library(caladan_shim STATIC IMPORTED GLOBAL)
add_dependencies(caladan_shim caladan_shim_src)
set_target_properties(caladan_shim PROPERTIES IMPORTED_LOCATION ${CALADAN_PATH}/shim/libshim.a)
target_link_options(caladan_shim INTERFACE "-Wl,--wrap=main")
target_link_libraries(caladan_shim INTERFACE "-ldl" caladan_runtime)

ExternalProject_Add(caladan_cc_bindings_src
  SOURCE_DIR ${CALADAN_PATH}/bindings/cc
  CONFIGURE_COMMAND ""
  BUILD_COMMAND make -j ${NPROC}
  INSTALL_COMMAND ""
  BUILD_IN_SOURCE 1
  BUILD_ALWAYS 1
)
ExternalProject_Add_StepDependencies(caladan_cc_bindings_src build caladan_src)

add_library(caladan_cc_bindings STATIC IMPORTED GLOBAL)
add_dependencies(caladan_cc_bindings caladan_cc_bindings_src)
set_target_properties(caladan_cc_bindings PROPERTIES IMPORTED_LOCATION ${CALADAN_PATH}/bindings/cc/librt++.a)
