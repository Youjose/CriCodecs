find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Concurrent)
include(GNUInstallDirs)

set(CRISTUDIO_VERSION "1.0.0" CACHE STRING "CriStudio release version")
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" cristudio_numeric_version "${CRISTUDIO_VERSION}")
if(NOT cristudio_numeric_version)
    message(FATAL_ERROR "CRISTUDIO_VERSION must begin with major.minor.patch")
endif()
set(CRISTUDIO_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(CRISTUDIO_VERSION_MINOR "${CMAKE_MATCH_2}")
set(CRISTUDIO_VERSION_PATCH "${CMAKE_MATCH_3}")

qt_add_executable(CriStudio
    src/browser/browser_delegates.cpp
    src/document_loader.cpp
    src/editor/archive_editor_helpers.cpp
    src/editor/editor_document_ui.cpp
    src/editor/editor_document_widget.cpp
    src/editor/editor_helpers.cpp
    src/editor/hex_patterns.cpp
    src/editor/hex_patterns/aax_patterns.cpp
    src/editor/hex_patterns/acx_patterns.cpp
    src/editor/hex_patterns/adx_patterns.cpp
    src/editor/hex_patterns/afs_patterns.cpp
    src/editor/hex_patterns/aix_patterns.cpp
    src/editor/hex_patterns/awb_patterns.cpp
    src/editor/hex_patterns/common_patterns.cpp
    src/editor/hex_patterns/cpk_patterns.cpp
    src/editor/hex_patterns/cvm_patterns.cpp
    src/editor/hex_patterns/hca_patterns.cpp
    src/editor/hex_patterns/sfd_patterns.cpp
    src/editor/hex_patterns/usm_patterns.cpp
    src/editor/hex_patterns/utf_patterns.cpp
    src/editor/hex_patterns/wav_patterns.cpp
    src/editor/hex_preview_widget.cpp
    src/editor/editor_session_loaders.cpp
    src/editor/scratch_editor_session.cpp
    src/editor/transform_editor_helpers.cpp
    src/editor/transform_detail_model.cpp
    src/editor/table_item_helpers.cpp
    src/editor/editor_widgets.cpp
    src/editor_workspace.cpp
    src/entry_table_model.cpp
    src/file_list_model.cpp
    src/format_icon.cpp
    src/main.cpp
    src/main_window/background.cpp
    src/main_window/browser_view.cpp
    src/main_window/chrome.cpp
    src/main_window/events.cpp
    src/main_window/hca_key_recovery.cpp
    src/main_window/usm_key_recovery.cpp
    src/main_window/adx_key_recovery.cpp
    src/main_window/aac_key_recovery.cpp
    src/main_window.cpp
    src/main_window/editor_actions.cpp
    src/main_window/key_panel.cpp
    src/main_window/preview_audio.cpp
    src/main_window/preview_helpers.cpp
    src/main_window/preview_mux.cpp
    src/main_window/preview_panel.cpp
    src/main_window/preview_video.cpp
    src/main_window/ui_helpers.cpp
    src/modules/aax/aax_browse.cpp
    src/modules/aax/aax_edit.cpp
    src/modules/aax/aax_preview.cpp
    src/modules/acb/acb_browse.cpp
    src/modules/acb/acb_edit.cpp
    src/modules/acb/acb_edit_ui.cpp
    src/modules/acx/acx_browse.cpp
    src/modules/acx/acx_edit.cpp
    src/modules/acx/acx_edit_ui.cpp
    src/modules/afs/afs_browse.cpp
    src/modules/afs/afs_edit.cpp
    src/modules/afs/afs_edit_ui.cpp
    src/modules/audio/audio_encode.cpp
    src/modules/audio/audio_encode_ui.cpp
    src/modules/adx/adx_browse.cpp
    src/modules/adx/adx_container_build_ui.cpp
    src/modules/adx/adx_common.cpp
    src/modules/adx/adx_edit.cpp
    src/modules/adx/adx_edit_ui.cpp
    src/modules/adx/adx_preview.cpp
    src/modules/aix/aix_browse.cpp
    src/modules/aix/aix_edit.cpp
    src/modules/awb/awb_browse.cpp
    src/modules/awb/awb_edit.cpp
    src/modules/awb/awb_edit_ui.cpp
    src/modules/csb/csb_browse.cpp
    src/modules/csb/csb_edit.cpp
    src/modules/csb/csb_edit_ui.cpp
    src/modules/cpk/cpk_browse.cpp
    src/modules/cpk/cpk_edit.cpp
    src/modules/cpk/cpk_edit_ui.cpp
    src/modules/cvm/cvm_browse.cpp
    src/modules/cvm/cvm_edit.cpp
    src/modules/cvm/cvm_edit_ui.cpp
    src/modules/hca/hca_browse.cpp
    src/modules/hca/hca_common.cpp
    src/modules/hca/hca_edit.cpp
    src/modules/hca/hca_edit_ui.cpp
    src/modules/hca/hca_preview.cpp
    src/modules/sfd/sfd_browse.cpp
    src/modules/sfd/sfd_edit.cpp
    src/modules/utf/utf_browse.cpp
    src/modules/utf/utf_common.cpp
    src/modules/utf/utf_edit.cpp
    src/modules/utf/utf_edit_ui.cpp
    src/modules/usm/usm_browse.cpp
    src/modules/usm/usm_edit.cpp
    src/modules/usm/usm_extract.cpp
    src/modules/usm/media_build.cpp
    src/modules/usm/media_build_validation.cpp
    src/modules/usm/media_build_ui.cpp
    src/modules/wav/wav_browse.cpp
    src/modules/wav/wav_preview.cpp
    src/shared/audio_preview_helpers.cpp
    src/shared/document_extract_helpers.cpp
    src/shared/document_extraction_report.cpp
    src/shared/document_helpers.cpp
    src/shared/document_preview_router.cpp
    src/shared/document_sniffer.cpp
    src/shared/embedded_document_loader.cpp
    src/shared/embedded_entry_extractor.cpp
    src/shared/embedded_preview_helpers.cpp
    src/shared/ffmpeg_audio_preview.cpp
    src/shared/hca_key_recovery.cpp
    src/shared/usm_key_recovery.cpp
    src/shared/adx_key_recovery.cpp
    src/shared/aac_key_recovery.cpp
    src/shared/mux_export_helpers.cpp
    src/shared/raw_extract_helpers.cpp
    src/shared/video_probe.cpp
)

qt_add_resources(CriStudio cristudio_branding
    PREFIX "/branding"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/packaging"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/cristudio.png"
)

target_link_libraries(CriStudio PRIVATE CriCodecs Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets Qt6::Concurrent)
target_compile_definitions(CriStudio PRIVATE CRISTUDIO_VERSION="${CRISTUDIO_VERSION}")
if(WIN32)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/cristudio.ico" CRISTUDIO_WINDOWS_ICON)
    set(CRISTUDIO_WINDOWS_RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/cristudio.rc")
    configure_file(
        packaging/windows/cristudio.rc.in
        "${CRISTUDIO_WINDOWS_RESOURCE}"
        @ONLY
    )
    target_sources(CriStudio PRIVATE "${CRISTUDIO_WINDOWS_RESOURCE}")
    target_link_libraries(CriStudio PRIVATE psapi)
endif()
target_include_directories(CriStudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(CriStudio PROPERTIES
    OUTPUT_NAME "CriStudio"
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    CXX_SCAN_FOR_MODULES OFF
)

if(WIN32)
    add_executable(CriStudioLauncher WIN32
        "${CRISTUDIO_WINDOWS_RESOURCE}"
        packaging/windows/cristudio_launcher.cpp
    )
    target_compile_features(CriStudioLauncher PRIVATE cxx_std_23)
    target_compile_definitions(CriStudioLauncher PRIVATE UNICODE _UNICODE)
    target_link_libraries(CriStudioLauncher PRIVATE user32)
    if(MSVC)
        set_property(
            TARGET CriStudioLauncher
            PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    else()
        target_link_options(CriStudioLauncher PRIVATE -municode -static -static-libgcc -static-libstdc++)
    endif()
    set_target_properties(CriStudioLauncher PROPERTIES
        OUTPUT_NAME "CriStudioLauncher"
        CXX_SCAN_FOR_MODULES OFF
    )
endif()

if(APPLE)
    set(CRISTUDIO_MACOS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/CriStudio.icns")
    set_source_files_properties(
        "${CRISTUDIO_MACOS_ICON}"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources
    )
    target_sources(CriStudio PRIVATE "${CRISTUDIO_MACOS_ICON}")
    set_target_properties(CriStudio PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "CriStudio"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.Youjose.CriStudio"
        MACOSX_BUNDLE_ICON_FILE "CriStudio.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${CRISTUDIO_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${CRISTUDIO_VERSION}"
    )
endif()

install(
    TARGETS CriStudio
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

option(
    CRISTUDIO_DEPLOY_QT
    "Install the Qt runtime and plugins required by CriStudio"
    OFF
)
if(CRISTUDIO_DEPLOY_QT)
    qt_generate_deploy_app_script(
        TARGET CriStudio
        OUTPUT_SCRIPT cristudio_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
        NO_TRANSLATIONS
    )
    install(SCRIPT "${cristudio_deploy_script}")
endif()
