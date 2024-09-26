cmake_minimum_required(VERSION 3.10)
project(VoxyRecovery)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Specify the path to RocksDB
set(ROCKSDB_ROOT "${CMAKE_SOURCE_DIR}/rocksdb")

# Enable RocksDB Snappy compression
set(WITH_SNAPPY ON CACHE BOOL "Enable Snappy compression support in RocksDB" FORCE)

# Disable RocksDB tests
set(WITH_TESTS OFF CACHE BOOL "Disable RocksDB tests" FORCE)
set(WITH_BENCHMARK_TOOLS OFF CACHE BOOL "Disable RocksDB benchmark tools" FORCE)

# Find the Snappy library
find_package(Snappy REQUIRED)

# Find the Zstandard library (ZSTD)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED libzstd)

# Additional configuration options
option(FAIL_ON_WARNINGS OFF) # Disable warnings as errors
option(DEBUG_LEVEL 0) # Set debug level
option(ROCKSDB_BUILD_SHARED OFF) # Library mode (shared or static)
option(WITH_TESTS OFF)
add_subdirectory(rocksdb)

# Add the executable (main.cpp)
add_executable(VoxyRecovery main.cpp)

target_include_directories(VoxyRecovery PRIVATE
    ${ROCKSDB_ROOT}/include
    ${ZSTD_INCLUDE_DIRS}
    ${Snappy_INCLUDE_DIRS}
)

target_link_libraries(VoxyRecovery PRIVATE
    ${ZSTD_LIBRARIES}
    ${Snappy_LIBRARIES}
    rocksdb
)

# Apply the configuration options
if(DISABLE_WARNING_AS_ERROR)
    message(STATUS "Disabling warnings as errors")
    add_compile_options(-Wno-error)
endif()

# Set debug level
message(STATUS "Setting debug level to ${DEBUG_LEVEL}")
add_compile_options(-DDEBUG_LEVEL=${DEBUG_LEVEL})

# Set library mode (shared or static)
if(LIB_MODE STREQUAL "shared")
    message(STATUS "Building in shared library mode")
    set(BUILD_SHARED_LIBS ON)
else()
    message(STATUS "Building in static library mode")
    set(BUILD_SHARED_LIBS OFF)
endif()

# Enable LTO if specified
if(USE_LTO)
    message(STATUS "Enabling LTO (Link-Time Optimization)")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# Enable RTTI if specified
if(USE_RTTI)
    message(STATUS "Enabling RTTI (Run-Time Type Information)")
    add_compile_options(-frtti)
endif()
