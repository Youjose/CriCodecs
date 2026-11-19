add_executable(usm_cue_test usm_cue_test.cpp)
target_link_libraries(usm_cue_test PRIVATE CriCodecs)
target_compile_definitions(usm_cue_test PRIVATE
    CUE_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/usm_cue_example.hex"
    CUE_HEADER_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/usm_cue_header_example.hex"
)
add_test(NAME usm_cue_test COMMAND usm_cue_test)

if(CRICODECS_BUILD_CLI OR CRICODECS_BUILD_PYTHON OR CRICODECS_INCLUDE_CLI_API)
    add_executable(usm_cli_cue_test usm_cli_cue_test.cpp)
    target_link_libraries(usm_cli_cue_test PRIVATE CriCodecs)
    target_compile_definitions(usm_cli_cue_test PRIVATE
        CUE_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/usm_cue_example.hex"
        CUE_HEADER_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/usm_cue_header_example.hex"
    )
    add_test(NAME usm_cli_cue_test COMMAND usm_cli_cue_test)
endif()
