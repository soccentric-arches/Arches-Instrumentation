# Arches Instrumentation — top-level build (REQ §14).
# One CMake project, one version for the whole suite (from VERSION).
cmake_minimum_required(VERSION 3.16)
project(instrumentation VERSION 0.0.0 LANGUAGES CXX)

# ---- version from VERSION file ----
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" INSTR_VERSION_RAW)
string(STRIP "${INSTR_VERSION_RAW}" INSTR_VERSION)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/generated/instr/common/version_gen.h" @ONLY)

# ---- compiler baseline ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_compile_options(-Wall -Wextra -Wshadow)

# ---- dependencies ----
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG QUIET)
if(NOT gRPC_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GRPC REQUIRED grpc++)
  set(GRPCPP_LIBRARIES ${GRPC_LINK_LIBRARIES})
  set(GRPCPP_INCLUDE_DIRS ${GRPC_INCLUDE_DIRS})
else()
  set(GRPCPP_LIBRARIES gRPC::grpc++ protobuf::libprotobuf)
endif()
find_package(yaml-cpp REQUIRED)
find_package(GTest QUIET)

# ---- proto generation (R3: one IDL, one generator) ----
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(PROTO_FILES_ALL
  ${PROTO_DIR}/common.proto ${PROTO_DIR}/istate.proto ${PROTO_DIR}/networkd.proto
  ${PROTO_DIR}/monitord.proto ${PROTO_DIR}/hardware.proto ${PROTO_DIR}/faults.proto
  ${PROTO_DIR}/alarms.proto ${PROTO_DIR}/logmanager.proto ${PROTO_DIR}/telemetry.proto
  ${PROTO_DIR}/ota.proto ${PROTO_DIR}/backup.proto)
set(PROTO_GEN "${CMAKE_CURRENT_BINARY_DIR}/protos_gen")
file(MAKE_DIRECTORY "${PROTO_GEN}")
set(PROTO_SRCS)
set(PROTO_HDRS)
set(GRPC_PLUGIN "")
if(TARGET gRPC::grpc_cpp_plugin)
  get_target_property(GRPC_PLUGIN gRPC::grpc_cpp_plugin LOCATION)
else()
  find_program(GRPC_PLUGIN grpc_cpp_plugin REQUIRED)
endif()
foreach(P common istate networkd monitord hardware faults alarms logmanager telemetry ota backup)
  list(APPEND PROTO_SRCS "${PROTO_GEN}/${P}.pb.cc" "${PROTO_GEN}/${P}.grpc.pb.cc")
  list(APPEND PROTO_HDRS "${PROTO_GEN}/${P}.pb.h" "${PROTO_GEN}/${P}.grpc.pb.h")
  add_custom_command(
    OUTPUT "${PROTO_GEN}/${P}.pb.cc" "${PROTO_GEN}/${P}.pb.h"
           "${PROTO_GEN}/${P}.grpc.pb.cc" "${PROTO_GEN}/${P}.grpc.pb.h"
    COMMAND protobuf::protoc
    ARGS "--cpp_out=${PROTO_GEN}"
         "--grpc_out=${PROTO_GEN}"
         "--plugin=protoc-gen-grpc=${GRPC_PLUGIN}"
         "-I${PROTO_DIR}" "${PROTO_DIR}/${P}.proto"
    DEPENDS "${PROTO_DIR}/${P}.proto" "${PROTO_DIR}/common.proto"
    COMMENT "protoc ${P}.proto")
endforeach()
add_library(instr_protos STATIC ${PROTO_SRCS})
target_include_directories(instr_protos PUBLIC "${PROTO_GEN}" "${PROTO_DIR}")
target_link_libraries(instr_protos PUBLIC protobuf::libprotobuf)
if(TARGET gRPC::grpc++)
  target_link_libraries(instr_protos PUBLIC gRPC::grpc++)
else()
  target_link_libraries(instr_protos PUBLIC ${GRPCPP_LIBRARIES})
  target_include_directories(instr_protos SYSTEM PUBLIC ${GRPCPP_INCLUDE_DIRS})
endif()
target_compile_options(instr_protos PRIVATE -w)

include_directories("${CMAKE_CURRENT_BINARY_DIR}/generated")

add_subdirectory(lib/common)
add_subdirectory(lib/platform)
add_subdirectory(lib/ctrl)
add_subdirectory(lib/transport)
target_link_libraries(instr_transport PUBLIC instr_protos)

# ---- catalog code generator (REQ §8) ----
add_subdirectory(catalog)

# ---- daemons (§7) ----
add_subdirectory(daemons/istate)
add_subdirectory(daemons/networkd)
add_subdirectory(daemons/monitord)
add_subdirectory(daemons/hardware)
add_subdirectory(daemons/faults)
add_subdirectory(daemons/alarms)
add_subdirectory(daemons/logmanager)
add_subdirectory(daemons/telemetry)
add_subdirectory(daemons/ota)
add_subdirectory(daemons/backup)

# ---- tools: one _ctrl per daemon (§3.2) ----
add_subdirectory(tools)

# ---- tests (§12) ----
enable_testing()
# ---- docs (§14: generated so it cannot drift) ----
add_subdirectory(docs)
add_subdirectory(test)