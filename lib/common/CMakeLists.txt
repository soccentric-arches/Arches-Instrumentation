# lib/common — shared core library (REQ §4)
add_library(instr_common STATIC
  src/timestamps.cpp
  src/json.cpp
  src/log.cpp
  src/alarm_codec.cpp
  src/crypto.cpp
  src/filesystem.cpp
  src/atomic_file.cpp
  src/spool.cpp
  src/config.cpp
  src/command_table.cpp
  src/tar.cpp
  src/daemon_skeleton.cpp
)
target_include_directories(instr_common PUBLIC include)
target_link_libraries(instr_common PUBLIC yaml-cpp)
target_compile_features(instr_common PUBLIC cxx_std_17)
# Warnings as errors: consistent, diagnosable code across the suite.
if(NOT MSVC)
  target_compile_options(instr_common PRIVATE -Wall -Wextra -Wshadow)
endif()