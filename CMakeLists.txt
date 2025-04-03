cmake_minimum_required(VERSION 3.10)
project(sysTrace)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

add_subdirectory(src/trace)
add_subdirectory(protos)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME}
    trace
    common
    general_pb2
    ${Python3_LIBRARIES}
   protobuf::libprotobuf
)