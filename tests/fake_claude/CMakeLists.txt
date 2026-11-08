add_executable(fake_claude main.cpp)
set_target_properties(fake_claude PROPERTIES
    OUTPUT_NAME claude
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fake_claude/bin)
target_link_libraries(fake_claude PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(fake_claude PRIVATE -Wall -Wextra -Wpedantic)
