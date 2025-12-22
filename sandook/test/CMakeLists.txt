message(STATUS "Building test")

include(GoogleTest)

set(WRAP_MAIN -Wl,--wrap=main -Wl,-no-whole-archive -ldl)

# === VirtualDisk ===
add_executable(bench_virtual_disk
  utils/virtual_disk_utils.cc
  bench_virtual_disk.cc
)
target_link_libraries(bench_virtual_disk
  virtual_disk
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,gtest_main>"
)
target_link_options(bench_virtual_disk PUBLIC
  ${WRAP_MAIN}
)

# Generate Caladan configuration
set(bench_virtual_disk_config
"host_addr 192.168.127.7
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
set(bench_virtual_disk_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/bench_virtual_disk.config
)
file(WRITE ${bench_virtual_disk_config_path} ${bench_virtual_disk_config})

add_test(NAME bench_virtual_disk
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMAND sh -c "sudo SANDOOK_CONFIG=${sandook_config_path} $<TARGET_FILE:bench_virtual_disk> ${bench_virtual_disk_config_path}"
)

add_executable(test_virtual_disk
  utils/virtual_disk_utils.cc
  test_virtual_disk.cc
)
target_link_libraries(test_virtual_disk
  virtual_disk
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,gtest_main>"
)
target_link_options(test_virtual_disk PUBLIC
  ${WRAP_MAIN}
)

# Generate Caladan configuration
set(test_virtual_disk_config
"host_addr 192.168.127.7
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
set(test_virtual_disk_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/test_virtual_disk.config
)
file(WRITE ${test_virtual_disk_config_path} ${test_virtual_disk_config})

add_test(NAME test_virtual_disk
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMAND sh -c "sudo SANDOOK_CONFIG=${sandook_config_path} $<TARGET_FILE:test_virtual_disk> ${test_virtual_disk_config_path}"
)

# === BlkDev ===
add_executable(test_blk_dev
  utils/blk_dev_utils.cc
  test_blk_dev.cc
)
# Note: This benchmark does not *require* any sandook libraries to be linked
#       (e.g., sandook_base/sandook_bindings) but we still link them to use Caladan threads in
#       order to make a fair comparison to the bench_virtual_disk which also
#       uses Caladan threads.
target_link_libraries(test_blk_dev
  mem
  sandook_base
  sandook_bindings
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,gtest_main>"
)
target_link_options(test_blk_dev PUBLIC
  ${WRAP_MAIN}
)

set(test_blk_dev_config
"host_addr 192.168.127.11
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
set(test_blk_dev_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/test_blk_dev.config
)
file(WRITE ${test_blk_dev_config_path} ${test_blk_dev_config})

add_test(NAME test_blk_dev
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMAND sh -c "sudo SANDOOK_CONFIG=${sandook_config_path} $<TARGET_FILE:test_blk_dev> ${test_blk_dev_config_path}"
)

# === RPC ===
add_executable(bench_rpc
  bench_rpc.cc
)
target_link_libraries(bench_rpc
  rpc
  mem
  sandook_base
  sandook_bindings
)
set(bench_rpc_server_config
"host_addr 192.168.127.20
host_netmask 255.255.255.0
host_gateway 192.168.127.1
host_mtu 9000
runtime_kthreads 26
runtime_spinning_kthreads 26
runtime_guaranteed_kthreads 0
runtime_priority lc
runtime_quantum_us 0
enable_directpath 1"
)
set(bench_rpc_server_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/bench_rpc_server.config
)
file(WRITE ${bench_rpc_server_config_path} ${bench_rpc_server_config})
set(bench_rpc_client_config
"host_addr 192.168.127.21
host_netmask 255.255.255.0
host_gateway 192.168.127.1
host_mtu 9000
runtime_kthreads 26
runtime_spinning_kthreads 26
runtime_guaranteed_kthreads 0
runtime_priority lc
runtime_quantum_us 0
enable_directpath 1"
)
set(bench_rpc_client_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/bench_rpc_client.config
)
file(WRITE ${bench_rpc_client_config_path} ${bench_rpc_client_config})

# === Apps ===
add_executable(test_apps
  utils/virtual_disk_utils.cc
  test_apps.cc
)
target_link_libraries(test_apps
  virtual_disk
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,gtest_main>"
)
target_link_options(test_apps PUBLIC
  ${WRAP_MAIN}
)
set(test_apps_config
"host_addr 192.168.127.24
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
set(test_apps_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/test_apps.config
)
file(WRITE ${test_apps_config_path} ${test_apps_config})

add_test(NAME test_apps
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMAND sh -c "sudo SANDOOK_CONFIG=${sandook_config_path} $<TARGET_FILE:test_apps> ${test_apps_config_path}"
)

# === Telemetry ===
add_executable(bench_telemetry
  bench_telemetry.cc
)
target_link_libraries(bench_telemetry
  mem
  sandook_base
  sandook_bindings
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,gtest_main>"
)
target_link_options(bench_telemetry PUBLIC
  ${WRAP_MAIN}
)

set(bench_telemetry_config
"host_addr 192.168.127.25
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
set(bench_telemetry_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/bench_telemetry.config
)
file(WRITE ${bench_telemetry_config_path} ${bench_telemetry_config})

add_test(NAME bench_telemetry
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMAND sh -c "sudo SANDOOK_CONFIG=${sandook_config_path} $<TARGET_FILE:bench_telemetry> ${bench_telemetry_config_path}"
)

add_executable(test_telemetry
  test_telemetry.cc
)
target_link_libraries(test_telemetry
  mem
  sandook_base
  sandook_bindings
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,gtest_main>"
)
target_link_options(test_telemetry PUBLIC
  ${WRAP_MAIN}
)

set(test_telemetry_config
"host_addr 192.168.127.25
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
set(test_telemetry_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/test_telemetry.config
)
file(WRITE ${test_telemetry_config_path} ${test_telemetry_config})

add_test(NAME test_telemetry
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMAND sh -c "sudo SANDOOK_CONFIG=${sandook_config_path} $<TARGET_FILE:test_telemetry> ${test_telemetry_config_path}"
)

# === Slab ===
add_executable(test_slab
  test_slab.cc
)
target_link_libraries(test_slab
  mem
  sandook_base
  sandook_bindings
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,gtest_main>"
)
target_link_options(test_slab PUBLIC
  ${WRAP_MAIN}
)

set(test_slab_config
"host_addr 192.168.127.25
host_netmask 255.255.255.0
host_gateway 192.168.127.1
host_mtu 9000
runtime_kthreads 8
runtime_spinning_kthreads 0
runtime_guaranteed_kthreads 0
runtime_priority lc
runtime_quantum_us 0
enable_directpath 1"
)
set(test_slab_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/test_slab.config
)
file(WRITE ${test_slab_config_path} ${test_slab_config})

add_test(NAME test_slab
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMAND sh -c "sudo SANDOOK_CONFIG=${sandook_config_path} $<TARGET_FILE:test_slab> ${test_slab_config_path}"
)

# === Control Plane Scheduler ===
add_executable(test_control_plane_scheduler
  test_control_plane_scheduler.cc
)
target_link_libraries(test_control_plane_scheduler
  config
  mem
  sandook_base
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,gtest_main>"
)
target_link_options(test_control_plane_scheduler PUBLIC
  ${WRAP_MAIN}
)

set(test_control_plane_scheduler_config
"host_addr 192.168.127.25
host_netmask 255.255.255.0
host_gateway 192.168.127.1
host_mtu 9000
runtime_kthreads 8
runtime_spinning_kthreads 0
runtime_guaranteed_kthreads 0
runtime_priority lc
runtime_quantum_us 0
enable_directpath 1"
)
set(test_control_plane_scheduler_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/test_control_plane_scheduler.config
)
file(WRITE ${test_control_plane_scheduler_config_path} ${test_control_plane_scheduler_config})

add_test(NAME test_control_plane_scheduler
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMAND sh -c "sudo SANDOOK_CONFIG=${sandook_config_path} $<TARGET_FILE:test_control_plane_scheduler> ${test_control_plane_scheduler_config_path}"
)

# === Data Plane Scheduler ===
add_executable(test_data_plane_scheduler
  test_data_plane_scheduler.cc
)
target_link_libraries(test_data_plane_scheduler
  mem
  config
  sandook_base
  sandook_bindings
  caladan_runtime
  caladan_cc_bindings
  virtual_disk
  utils
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,gtest_main>"
)
target_link_options(test_data_plane_scheduler PUBLIC
  ${WRAP_MAIN}
)

set(test_data_plane_scheduler_config
"host_addr 192.168.127.25
host_netmask 255.255.255.0
host_gateway 192.168.127.1
host_mtu 9000
runtime_kthreads 8
runtime_spinning_kthreads 0
runtime_guaranteed_kthreads 0
runtime_priority lc
runtime_quantum_us 0
enable_directpath 1"
)
set(test_data_plane_scheduler_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/test_data_plane_scheduler.config
)
file(WRITE ${test_data_plane_scheduler_config_path} ${test_data_plane_scheduler_config})

add_test(NAME test_data_plane_scheduler
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMAND sh -c "sudo SANDOOK_CONFIG=${sandook_config_path} $<TARGET_FILE:test_data_plane_scheduler> ${test_data_plane_scheduler_config_path}"
)

# === DiskModel ===
add_executable(test_disk_model
  test_disk_model.cc
)
target_link_libraries(test_disk_model
  config
  mem
  sandook_base
  sandook_bindings
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,gtest_main>"
)
target_link_options(test_disk_model PUBLIC
  ${WRAP_MAIN}
)

set(test_disk_model_config
"host_addr 192.168.127.25
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
set(test_disk_model_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/test_disk_model.config
)
file(WRITE ${test_disk_model_config_path} ${test_disk_model_config})

add_test(NAME test_disk_model
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMAND sh -c "sudo SANDOOK_CONFIG=${sandook_config_path} $<TARGET_FILE:test_disk_model> ${test_disk_model_config_path}"
)

# === ControllerAgent ===
add_executable(test_controller_agent
  ${CMAKE_SOURCE_DIR}/sandook/controller/controller_agent.cc
  ${CMAKE_SOURCE_DIR}/sandook/controller/controller_conn_handler.cc
  test_controller_agent.cc
)

target_link_libraries(test_controller_agent
  config
  rpc
  mem
  sandook_base
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,gtest_main>"
)

target_link_options(test_controller_agent PUBLIC
  ${WRAP_MAIN}
)

set(test_controller_agent_config
"host_addr 192.168.127.26
host_netmask 255.255.255.0
host_gateway 192.168.127.1
host_mtu 9000
runtime_kthreads 4
runtime_spinning_kthreads 4
runtime_guaranteed_kthreads 0
runtime_priority lc
runtime_quantum_us 0
enable_directpath 1"
)
set(test_controller_agent_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/test_controller_agent.config
)
file(WRITE ${test_controller_agent_config_path} ${test_controller_agent_config})

add_test(NAME test_controller_agent
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMAND sh -c "sudo SANDOOK_CONFIG=${sandook_config_path} $<TARGET_FILE:test_controller_agent> ${test_controller_agent_config_path}"
)

# === Controller ===
add_executable(bench_controller
  ${CMAKE_SOURCE_DIR}/sandook/controller/controller_agent.cc
  ${CMAKE_SOURCE_DIR}/sandook/controller/controller_conn_handler.cc
  bench_controller.cc
)

target_link_libraries(bench_controller
  config
  rpc
  mem
  sandook_base
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,gtest_main>"
)

target_link_options(bench_controller PUBLIC
  ${WRAP_MAIN}
)

set(bench_controller_config
"host_addr 192.168.127.26
host_netmask 255.255.255.0
host_gateway 192.168.127.1
host_mtu 9000
runtime_kthreads 26
runtime_spinning_kthreads 26
runtime_guaranteed_kthreads 0
runtime_priority lc
runtime_quantum_us 0
enable_directpath 1"
)
set(bench_controller_config_path
  ${CMAKE_CURRENT_BINARY_DIR}/bench_controller.config
)
file(WRITE ${bench_controller_config_path} ${bench_controller_config})

add_test(NAME bench_controller
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMAND sh -c "sudo SANDOOK_CONFIG=${sandook_config_path} $<TARGET_FILE:bench_controller> ${bench_controller_config_path}"
)
