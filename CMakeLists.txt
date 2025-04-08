cmake_minimum_required(VERSION 3.10)
project(sysTrace)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(absl REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Development)
find_package(Protobuf REQUIRED)

add_library(common STATIC
    ${PROJECT_SOURCE_DIR}/include/common/logging.cc
    ${PROJECT_SOURCE_DIR}/include/common/util.cc
)

target_include_directories(common PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${Python3_INCLUDE_DIRS}
)

target_link_libraries(common
    absl::base
    protobuf::libprotobuf
)

add_subdirectory(protos)
add_subdirectory(src/trace)

add_library(sysTrace_hook SHARED
    ${PROJECT_SOURCE_DIR}/src/trace/systrace_manager.cc
    ${PROJECT_SOURCE_DIR}/src/trace/library_loader.cc
    ${PROJECT_SOURCE_DIR}/src/trace/python/pytorch_tracing_loader.cc
    ${PROJECT_SOURCE_DIR}/src/trace/python/py_syshook.c
    ${PROJECT_SOURCE_DIR}/src/trace/python/pytorch_tracing_manager.cc
    ${PROJECT_SOURCE_DIR}/src/trace/python/pytorch_tracing.c
)

set_target_properties(sysTrace_hook PROPERTIES
    OUTPUT_NAME "sysTrace"
)

target_link_libraries(sysTrace_hook
    common
    general_pb2
    ${Python3_LIBRARIES}
    protobuf::libprotobuf
    -ldl
)

add_executable(sysTrace_main
    ${PROJECT_SOURCE_DIR}/main.cpp
)

target_include_directories(sysTrace_main PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(sysTrace_main
    sysTrace_hook
    common
    ${Python3_LIBRARIES}
    protobuf::libprotobuf
    -ldl
)