# Generator
halide_project(camera_pipe "apps" camera_pipe.cpp)

set(camera_pipe_h "${CMAKE_CURRENT_BINARY_DIR}/camera_pipe.h")
set(camera_pipe_lib "${CMAKE_CURRENT_BINARY_DIR}/camera_pipe${CMAKE_STATIC_LIBRARY_SUFFIX}")

# FIXME: Set -O3 here
add_library(fcam fcam/Demosaic.cpp fcam/Demosaic_ARM.cpp)
FIND_PACKAGE(OpenMP)
if (OPENMP_FOUND)
  target_compile_options(fcam PUBLIC "-fopenmp")
endif()
if (NOT MSVC)
  target_compile_options(fcam PRIVATE "-std=c++11")
endif()

# Final executable
add_executable(process process.cpp ${camera_pipe_h})
target_link_libraries(process PRIVATE ${camera_pipe_lib} fcam ${PNG_LIBRARIES})
target_include_directories(process PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
                           ${PNG_INCLUDE_DIRS})
target_compile_definitions(process PRIVATE ${PNG_DEFINITIONS})
if (NOT WIN32)
  target_link_libraries(process PRIVATE dl pthread)
endif()
if (NOT MSVC)
  target_compile_options(process PRIVATE "-std=c++11")
endif()

# FIXME: Cannot use halide_add_generator_dependency()
add_custom_command(OUTPUT "${camera_pipe_h}" "${camera_pipe_lib}"
                   COMMAND camera_pipe 8 "${CMAKE_CURRENT_BINARY_DIR}/"
                   COMMENT "Generating camera_pipe header and object file")
