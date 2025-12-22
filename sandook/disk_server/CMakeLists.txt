message(STATUS "Building disk_server")

add_executable(disk_server
  disk_conn_handler.cc
  blk_server.cc
  spdk_server.cc
  mem_server.cc
  storage_server.cc
  disk_server.cc
  run.cc
)
target_link_libraries(disk_server
  config
  rpc
  mem
  sandook_base
  sandook_bindings
  utils
)

# Generate Caladan configuration
set(disk_server_config
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
set(disk_server_posix_config
${disk_server_config}
)
set(disk_server_mem_config
${disk_server_config}
)
set(disk_server_spdk_config
"${disk_server_config}
attach_spdk_pci ${SPDK_PCI_ADDR}"
)
set(disk_server_posix_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/disk_server_posix.config
)
set(disk_server_mem_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/disk_server_mem.config
)
set(disk_server_spdk_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/disk_server_spdk.config
)
file(WRITE ${disk_server_posix_config_path} ${disk_server_posix_config})
file(WRITE ${disk_server_mem_config_path} ${disk_server_mem_config})
file(WRITE ${disk_server_spdk_config_path} ${disk_server_spdk_config})
