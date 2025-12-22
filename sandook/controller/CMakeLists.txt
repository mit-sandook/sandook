message(STATUS "Building controller")

add_executable(controller
  controller_agent.cc
  controller_conn_handler.cc
  controller.cc
  run.cc
)
target_link_libraries(controller
  config
  rpc
  mem
  sandook_base
  sandook_bindings
)

# Generate Caladan configuration
set(controller_config
"host_addr 192.168.127.8
host_netmask 255.255.255.0
host_gateway 192.168.127.1
host_mtu 9000
runtime_kthreads 8
runtime_spinning_kthreads 8
runtime_guaranteed_kthreads 8
runtime_priority lc
runtime_quantum_us 0
enable_directpath 1
log_level 6"
)
set(controller_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/controller.config
)
file(WRITE ${controller_config_path} ${controller_config})
