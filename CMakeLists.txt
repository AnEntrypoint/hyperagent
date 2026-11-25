cmake_minimum_required(VERSION 3.21)
if(CMAKE_HOST_WIN32 AND VCPKG_TARGET_TRIPLET MATCHES "-static$")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

project(hyperagent VERSION 0.1.0 LANGUAGES C CXX)

set(HYPERAGENT_VERSION "0.1.0" CACHE STRING "version reported by hyperagent --version")
option(HYPERAGENT_STATIC_RUNTIME "link the C++ runtime statically on Linux" OFF)
if(HYPERAGENT_STATIC_RUNTIME AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_link_options(-static-libstdc++ -static-libgcc)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

cmake_policy(SET CMP0167 NEW)

find_package(Threads REQUIRED)
find_package(Boost 1.84 REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)

file(GLOB_RECURSE HYPERAGENT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_executable(hyperagent ${HYPERAGENT_SOURCES})
target_include_directories(hyperagent PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${SQLite3_INCLUDE_DIR})
target_compile_definitions(hyperagent PRIVATE HYPERAGENT_VERSION="${HYPERAGENT_VERSION}")
target_link_libraries(hyperagent PRIVATE
    Boost::headers
    nlohmann_json::nlohmann_json
    CLI11::CLI11
    ${SQLite3_LIBRARY}
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads)
if(TARGET Boost::system)
    target_link_libraries(hyperagent PRIVATE Boost::system)
endif()
if(MSVC)
    target_compile_options(hyperagent PRIVATE /W4 /utf-8 /permissive- /bigobj)
    target_compile_definitions(hyperagent PRIVATE _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(hyperagent PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(install_unit
    tests/install_unit.cpp
    src/core/install.cpp
    src/core/paths.cpp
    src/core/ports.cpp
    src/core/files.cpp
    src/core/json.cpp
    src/core/text.cpp)
target_include_directories(install_unit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(install_unit PRIVATE Boost::headers nlohmann_json::nlohmann_json Threads::Threads)
if(TARGET Boost::system)
    target_link_libraries(install_unit PRIVATE Boost::system)
endif()
if(WIN32)
    target_link_libraries(install_unit PRIVATE ws2_32)
endif()
add_test(NAME install_unit COMMAND install_unit)
set_tests_properties(install_unit PROPERTIES TIMEOUT 60)

add_subdirectory(airc)
set_target_properties(ccbridge PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_dependencies(hyperagent ccbridge)
target_compile_definitions(hyperagent PRIVATE HYPERAGENT_CCBRIDGE="$<TARGET_FILE:ccbridge>")

if(NOT WIN32)
add_subdirectory(tests/fake_claude)

add_test(NAME orch_launch
    COMMAND ${CMAKE_COMMAND} -E env HYPERAGENT_BUILD=${CMAKE_BINARY_DIR}
        bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/orch_launch.sh)
set_tests_properties(orch_launch PROPERTIES TIMEOUT 600)

add_test(NAME idle_reap
    COMMAND ${CMAKE_COMMAND} -E env HYPERAGENT_BUILD=${CMAKE_BINARY_DIR}
        bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/idle_reap.sh)
set_tests_properties(idle_reap PROPERTIES TIMEOUT 180)

add_test(NAME follow_transcript
    COMMAND ${CMAKE_COMMAND} -E env HYPERAGENT_BUILD=${CMAKE_BINARY_DIR}
        bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/follow_transcript.sh)
set_tests_properties(follow_transcript PROPERTIES TIMEOUT 180)
endif()

install(TARGETS hyperagent RUNTIME DESTINATION bin)
install(DIRECTORY gui/public DESTINATION share/hyperagent/gui)
install(FILES .mcp.json AGENTS.md CLAUDE.md README.md DESTINATION .)
install(DIRECTORY docs scripts tools DESTINATION .)
