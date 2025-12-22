message(STATUS "Building blk_dev")

# Generate Caladan configuration
set(blk_dev_config
"host_addr 192.168.127.10
host_netmask 255.255.255.0
host_gateway 192.168.127.1
host_mtu 9000
runtime_kthreads 20
runtime_spinning_kthreads 16
runtime_guaranteed_kthreads 16
runtime_priority lc
runtime_quantum_us 0
enable_directpath 1"
)
set(blk_dev_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/blk_dev.config
)
file(WRITE ${blk_dev_config_path} ${blk_dev_config})

add_executable(blk_dev
  request_dispatcher.cc
  blk_dev_agent.cc
  blk_dev.cc
  run.cc
)
target_link_directories(blk_dev
  PUBLIC ${CMAKE_SOURCE_DIR}/lib/ubdsrv/lib/.libs
  PUBLIC ${CMAKE_SOURCE_DIR}/lib/ubdsrv/lib
)
target_link_libraries(blk_dev
  libublksrv.a
  liburing.a
  mem
  sandook_base
  sandook_bindings
  virtual_disk
)
