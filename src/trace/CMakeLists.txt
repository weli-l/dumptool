add_library(trace STATIC
    systrace_manager.cc
    library_loader.cc
    python/pytorch_tracing_loader.cc
    python/pytorch_tracing_manager.cc
)

target_include_directories(trace PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include
    ${Python3_INCLUDE_DIRS}
)

target_link_libraries(trace
    common
    general_pb2
    ${Python3_LIBRARIES}
    absl::base
)
