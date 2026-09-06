# istate daemon (REQ §7)
set(DAEMON_SRC
  src/config_service.cpp
  src/core.cpp
  src/hubs.cpp
  src/northbound.cpp
  src/orchestrator.cpp
  src/provisioning.cpp
  src/pst.cpp
  src/table.cpp)
add_library(instr_istate_lib STATIC ${DAEMON_SRC})
target_include_directories(instr_istate_lib PUBLIC include)
target_link_libraries(instr_istate_lib PUBLIC instr_common instr_platform instr_transport instr_protos instr_ctrl)

add_executable(istate src/main.cpp)
target_link_libraries(istate PRIVATE instr_istate_lib instr_protos)
