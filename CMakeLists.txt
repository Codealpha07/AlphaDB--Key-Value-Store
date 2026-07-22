cmake_minimum_required(VERSION 3.10)
project(ledgerdb CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Threads (needed for std::thread / std::mutex).
find_package(Threads REQUIRED)

# The reusable storage-engine code (no networking) lives here so it can be
# tested on its own.
add_library(ledgerdb_core src/record.cpp src/engine.cpp)
target_link_libraries(ledgerdb_core PUBLIC Threads::Threads)

# The server executable.
add_executable(ledgerdb src/main.cpp src/server.cpp)
target_link_libraries(ledgerdb PRIVATE ledgerdb_core)
if(WIN32)
    target_link_libraries(ledgerdb PRIVATE ws2_32)
endif()

# The test executable.
add_executable(ledgerdb_tests tests/test_engine.cpp)
target_link_libraries(ledgerdb_tests PRIVATE ledgerdb_core)

enable_testing()
add_test(NAME engine_tests COMMAND ledgerdb_tests)
