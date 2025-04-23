cmake_minimum_required(VERSION 3.20)  # Or higher, as needed

project(mspti_tracker)

set(PROJECT_NAME mspti_tracker)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

set(ASCEND_TOOLKIT_HOME $ENV{ASCEND_TOOLKIT_HOME})
set(ASCEND_HOME /usr/local/Ascend)

include_directories(
    ${ASCEND_TOOLKIT_HOME}/include
    ${ASCEND_TOOLKIT_HOME}/runtime/include
    ${ASCEND_TOOLKIT_HOME}/tools/mspti/include
    ${ASCEND_HOME}/driver/include
)

find_package(Threads REQUIRED)

set(SOURCES
    mspti_tracker.cpp
)

find_library(ASCEND_MSPTI 
    NAMES mspti
    HINTS
        ${ASCEND_TOOLKIT_HOME}/lib64
)
if(ASCEND_MSPTI)
    message(STATUS "Found Ascend mspti: ${ASCEND_MSPTI}")
else()
    message(FATAL_ERROR "Ascend mspti not found!")
endif()

add_library(mspti_tracker SHARED ${SOURCES})

target_link_libraries(${PROJECT_NAME}
    ${CMAKE_THREAD_LIBS} 
    ${ASCEND_MSPTI}
    jsoncpp
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

set(CMAKE_BUILD_DIR "${CMAKE_BINARY_DIR}/build")