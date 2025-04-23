cmake_minimum_required(VERSION 3.10)
project(sysTrace)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(ASCEND_TOOLKIT_HOME $ENV{ASCEND_TOOLKIT_HOME})
set(ASCEND_HOME /usr/local/Ascend)
include_directories(
    ${ASCEND_TOOLKIT_HOME}/include
    ${ASCEND_TOOLKIT_HOME}/runtime/include
    ${ASCEND_TOOLKIT_HOME}/tools/mspti/include
    ${ASCEND_HOME}/driver/include
)

find_package(Python3 REQUIRED COMPONENTS Development)
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)

find_library(ASCEND_MSPTI 
    NAMES mspti
    HINTS ${ASCEND_TOOLKIT_HOME}/lib64
)
if(NOT ASCEND_MSPTI)
    message(FATAL_ERROR "Ascend mspti library not found!")
endif()

add_library(common STATIC
    ${PROJECT_SOURCE_DIR}/include/common/logging.cc
    ${PROJECT_SOURCE_DIR}/include/common/util.cc
)
target_include_directories(common PUBLIC ${PROJECT_SOURCE_DIR}/include ${Python3_INCLUDE_DIRS})


add_subdirectory(protos)

add_library(sysTrace_hook SHARED
    ${PROJECT_SOURCE_DIR}/src/trace/systrace_manager.cc
    ${PROJECT_SOURCE_DIR}/src/trace/library_loader.cc
    ${PROJECT_SOURCE_DIR}/src/trace/python/pytorch_tracing_loader.cc
    ${PROJECT_SOURCE_DIR}/src/trace/python/pytorch_tracing_manager.cc
    ${PROJECT_SOURCE_DIR}/src/trace/python/pytorch_tracing.c
    ${PROJECT_SOURCE_DIR}/src/ascend/hook.cc
    ${PROJECT_SOURCE_DIR}/src/mspti/mspti_tracker.cpp
)

set_target_properties(sysTrace_hook PROPERTIES OUTPUT_NAME "sysTrace")

target_link_libraries(sysTrace_hook
    common
    general_pb2
    ${Python3_LIBRARIES}
    protobuf::libprotobuf
    ${CMAKE_THREAD_LIBS}
    ${ASCEND_MSPTI}
    jsoncpp
    -ldl
)