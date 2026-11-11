cmake_minimum_required(VERSION 3.21)
project(hyperagent VERSION 0.1.0 LANGUAGES C CXX)

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
target_compile_definitions(hyperagent PRIVATE HYPERAGENT_VERSION="${PROJECT_VERSION}")
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
target_compile_options(hyperagent PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()

add_subdirectory(airc)
set_target_properties(ccbridge PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_dependencies(hyperagent ccbridge)
target_compile_definitions(hyperagent PRIVATE HYPERAGENT_CCBRIDGE="$<TARGET_FILE:ccbridge>")

add_subdirectory(tests/fake_claude)

add_test(NAME orch_launch
    COMMAND ${CMAKE_COMMAND} -E env HYPERAGENT_BUILD=${CMAKE_BINARY_DIR}
        bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/orch_launch.sh)
set_tests_properties(orch_launch PROPERTIES TIMEOUT 600)

add_test(NAME idle_reap
    COMMAND ${CMAKE_COMMAND} -E env HYPERAGENT_BUILD=${CMAKE_BINARY_DIR}
        bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/idle_reap.sh)
set_tests_properties(idle_reap PROPERTIES TIMEOUT 180)

install(TARGETS hyperagent RUNTIME DESTINATION bin)
