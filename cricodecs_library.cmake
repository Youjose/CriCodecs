function(cricodecs_add_library target_name)
    if(NOT DEFINED CRICODECS_REPO_ROOT)
        message(FATAL_ERROR "CRICODECS_REPO_ROOT must be set before including cricodecs_library.cmake")
    endif()

    if(NOT DEFINED CRICODECS_VERSION)
        set(CRICODECS_VERSION "0.0.1b1")
    endif()
    set(CRICODECS_GIT_HASH "unknown")
    find_package(Git QUIET)
    if(Git_FOUND AND EXISTS "${CRICODECS_REPO_ROOT}/.git")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
            WORKING_DIRECTORY "${CRICODECS_REPO_ROOT}"
            RESULT_VARIABLE CRICODECS_GIT_RESULT
            OUTPUT_VARIABLE CRICODECS_GIT_OUTPUT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(CRICODECS_GIT_RESULT EQUAL 0 AND NOT CRICODECS_GIT_OUTPUT STREQUAL "")
            set(CRICODECS_GIT_HASH "${CRICODECS_GIT_OUTPUT}")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" diff-index --quiet HEAD --
                WORKING_DIRECTORY "${CRICODECS_REPO_ROOT}"
                RESULT_VARIABLE CRICODECS_GIT_DIRTY_RESULT
                ERROR_QUIET
            )
            if(NOT CRICODECS_GIT_DIRTY_RESULT EQUAL 0)
                string(APPEND CRICODECS_GIT_HASH "-dirty")
            endif()
        endif()
    endif()

    set(cricodecs_sources
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/aax/aax_container.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acx/acx_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acx/acx_container.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acx/acx_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/adx/adx_decoder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/adx/adx_encoder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acb/acb_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acb/acb_commands.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acb/acb_container.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acb/acb_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/ahx/ahx_decoder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/ahx/ahx_encoder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/afs/afs_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/afs/afs_container.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/afs/afs_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/aix/aix_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/aix/aix_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cli/cli.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cli/cli_maker.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cli/cli_common.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cli/cli_export.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cli/cli_metadata.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cli/cli_parse.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cli/cli_probe.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cvm/cvm_build_script.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cvm/cvm_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cvm/cvm_container.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cvm/cvm_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cvm/cvm_volume_set.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/sfd/sfd_container.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/sfd/sfd_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/sfd/sfd_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/csb/csb_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/csb/csb_container.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/csb/csb_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cpk/cpk_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cpk/cpk_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cpk/crilayla.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/hca/hca_crypto.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/hca/hca_decoder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/hca/hca_encoder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/hca/hca_packing.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/hca/hca_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/hca/hca_transform.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/usm/usm_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/usm/usm_crypto.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/usm/usm_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/video/h264.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/video/ivf.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/video/mpeg.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/wav/wav_container.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/utf/utf_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/utf/utf_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/utf/utf_table.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/utilities/io_reader_platform.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/utilities/io_writer_platform.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/utilities/text_encoding.cpp
    )

    add_library(${target_name} STATIC ${cricodecs_sources})
    target_compile_features(${target_name} PUBLIC cxx_std_23)
    target_compile_definitions(${target_name} PRIVATE
        CRICODECS_VERSION="${CRICODECS_VERSION}"
        CRICODECS_GIT_HASH="${CRICODECS_GIT_HASH}"
    )
    set_target_properties(${target_name} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )

    target_include_directories(${target_name} PUBLIC
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/aax
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acx
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/utilities
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acb
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/adx
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/ahx
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/afs
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/aix
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/awb
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cli
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/csb
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cpk
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cvm
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/hca
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/sfd
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/usm
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/video
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/wav
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/utf
        ${CRICODECS_REPO_ROOT}/include
    )

    if(NOT MSVC)
        find_package(Iconv QUIET)
        if(Iconv_FOUND)
            if(TARGET Iconv::Iconv)
                target_link_libraries(${target_name} PUBLIC Iconv::Iconv)
            elseif(Iconv_LIBRARIES)
                target_link_libraries(${target_name} PUBLIC ${Iconv_LIBRARIES})
            endif()
        endif()
    endif()

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra)
    endif()
endfunction()
