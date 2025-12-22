message(STATUS "Building utils")

add_library(utils STATIC
  calibrated_time.cc
)

target_link_libraries(utils
  mem
  sandook_base
)

add_executable(storage_perf
  storage_perf.cc
)

target_link_libraries(storage_perf
  caladan_runtime
  caladan_cc_bindings
)

# Generate Caladan configuration
set(storage_perf_config
"host_addr 192.168.1.1
host_netmask 255.255.255.0
host_gateway 192.168.1.1
runtime_kthreads 26
enable_directpath 1
attach_spdk_pci 0000:82:00.00"
)
set(storage_perf_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/storage_perf.config
)
file(WRITE ${storage_perf_config_path} ${storage_perf_config})

add_executable(pre_fill
  pre_fill.cc
)

target_link_libraries(pre_fill
  caladan_runtime
  caladan_cc_bindings
)

# Generate Caladan configuration
set(pre_fill_config
"host_addr 192.168.1.1
host_netmask 255.255.255.0
host_gateway 192.168.1.1
runtime_kthreads 26
enable_directpath 1
attach_spdk_pci 0000:82:00.00"
)
set(pre_fill_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/pre_fill.config
)
file(WRITE ${pre_fill_config_path} ${pre_fill_config})
