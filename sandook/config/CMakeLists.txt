message(STATUS "Building config")

add_library(config STATIC
  config.cc
)

target_link_libraries(config
  jsoncpp
)

set(ssd_models_dir_path
  "${CMAKE_SOURCE_DIR}/data/ssd_models"
)

set(sandook_config
"{
    \"kSSDModelsDirPath\": \"${ssd_models_dir_path}\",
    \"kControlPlaneSchedulerType\": \"NoOp\",
    \"kDataPlaneSchedulerType\": \"RandomReadWrite\",
    \"kDiskServerBackend\": \"SPDK\",
    \"kVirtualDiskType\": \"Remote\",
    \"kVirtualDiskIP\": \"192.168.127.7\",
    \"kVirtualDiskPort\": 5002,
    \"kVirtualDiskServerAffinity\": 0,
    \"kDiskServerRejections\": 0,
    \"kControllerIP\": \"192.168.127.8\",
    \"kControllerPort\": 5002,
    \"kStorageServerIP\": \"192.168.127.9\",
    \"kStorageServerPort\": 5002
}"
)
set(sandook_config_path
  ${CMAKE_BINARY_DIR}/config.json
  CACHE INTERNAL ""
)
file(WRITE ${sandook_config_path} ${sandook_config})
