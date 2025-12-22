message(STATUS "Building virtual_disk")

add_library(virtual_disk STATIC
  virtual_disk_base.cc
  virtual_disk_local.cc
  virtual_disk_remote.cc
  bindings.cc
  loadgen_utils.cc
)
target_link_libraries(virtual_disk
  config
  rpc
  mem
  sandook_base
  sandook_bindings
  utils
)

# Generate Caladan configuration
set(virtual_disk_config
"host_addr 192.168.127.9
host_netmask 255.255.255.0
host_gateway 192.168.127.1
host_mtu 9000
runtime_kthreads 8
runtime_spinning_kthreads 8
runtime_guaranteed_kthreads 8
runtime_priority lc
runtime_quantum_us 0
enable_directpath 1"
)
set(virtual_disk_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/virtual_disk.config
)
file(WRITE ${virtual_disk_config_path} ${virtual_disk_config})
