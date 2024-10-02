cmake_minimum_required(VERSION 3.10)
project(VoxyRecovery)

# Set C++ standard (optional, depending on your project)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

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

set(FAIL_ON_WARNINGS OFF)  # Disable warnings as errors
set(DEBUG_LEVEL 0)                  # Set debug level
set(ROCKSDB_BUILD_SHARED OFF)              # Library mode (shared or static)
set(WITH_TESTS OFF)
add_subdirectory(rocksdb)

option(NBT_BUILD_SHARED "Build shared libraries" OFF)
option(NBT_USE_ZLIB "Build additional zlib stream functionality" ON)
option(NBT_BUILD_TESTS "Build the unit tests. Requires CxxTest." OFF)

add_subdirectory(libnbtplusplus)

# add_definitions(-w) # ignore the YAP

# Add the executable (main.cpp)
add_executable(VoxyRecovery main.cpp)
add_executable(VoxyRecoveryTesting testing.cpp)

# Include RocksDB, ZSTD, and Snappy directories
target_include_directories(VoxyRecovery PRIVATE
    ${rocksdb_SOURCE_DIR}/include
    ${ZSTD_INCLUDE_DIRS}
    ${Snappy_INCLUDE_DIRS}
    ${libnbt++_SOURCE_DIR}/include
    ${libnbt++_BINARY_DIR}
)

# Link RocksDB, ZSTD, and Snappy libraries
target_link_libraries(VoxyRecovery PRIVATE
    ${ZSTD_LIBRARIES}
    ${Snappy_LIBRARIES}
    rocksdb
    nbt++
)

target_include_directories(VoxyRecoveryTesting PRIVATE
    ${ZSTD_INCLUDE_DIRS}
    ${libnbt++_SOURCE_DIR}/include
    ${libnbt++_BINARY_DIR}
)

# Link RocksDB, ZSTD, and Snappy libraries
target_link_libraries(VoxyRecoveryTesting PRIVATE
    ${ZSTD_LIBRARIES}
    nbt++
)

# Apply the configuration options
if(DISABLE_WARNING_AS_ERROR)
    message(STATUS "Disabling warnings as errors")
    add_compile_options(-Wno-error)
endif()

# Set debug level
message(STATUS "Setting debug level to ${DEBUG_LEVEL}")
add_compile_options(-DDEBUG_LEVEL=${DEBUG_LEVEL})

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
