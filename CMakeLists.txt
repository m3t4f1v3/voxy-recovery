cmake_minimum_required(VERSION 3.10)
project(VoxyRecovery)

# Set C++ standard (optional, depending on your project)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Specify the path to RocksDB
set(ROCKSDB_ROOT "${CMAKE_SOURCE_DIR}/rocksdb")

# Find the Zstandard library (ZSTD)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED libzstd)
find_package(ZLIB)

# Add the executable (main.cpp)
add_executable(VoxyRecovery main.cpp)

# Include RocksDB and ZSTD directories
target_include_directories(VoxyRecovery PRIVATE
    ${ROCKSDB_ROOT}/include
    ${ZSTD_INCLUDE_DIRS}
)

# Link RocksDB and ZSTD libraries
target_link_libraries(VoxyRecovery PRIVATE
    ${ROCKSDB_ROOT}/librocksdb.so   # Use static library of RocksDB
    ${ZSTD_LIBRARIES}
    ZLIB::ZLIB
)

# Optionally, specify RocksDB compilation flags
# target_compile_options(VoxyRecovery PRIVATE -pthread -lz -lbz2 -lsnappy)
