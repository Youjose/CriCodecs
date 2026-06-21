if(NOT TARGET CriCodecs)
    message(FATAL_ERROR "PyCriCodecs/CMakeLists.txt expects the parent project to define the CriCodecs target")
endif()

set(cricodecs_python_root_dir "${PROJECT_BINARY_DIR}/python")
set(cricodecs_python_package_dir "${cricodecs_python_root_dir}/cricodecs")

find_package(Python 3.9
    REQUIRED COMPONENTS Interpreter Development.Module
    OPTIONAL_COMPONENTS Development.SABIModule
)

execute_process(
    COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    RESULT_VARIABLE cricodecs_nanobind_status
    OUTPUT_STRIP_TRAILING_WHITESPACE
    OUTPUT_VARIABLE cricodecs_nanobind_root
    ERROR_QUIET
)

if(cricodecs_nanobind_status EQUAL 0)
    list(APPEND CMAKE_PREFIX_PATH "${cricodecs_nanobind_root}")
endif()

find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(cricodecs
    NB_STATIC
    NB_SUPPRESS_WARNINGS
    src/module.cpp
    src/adx_bindings.cpp
    src/ahx_bindings.cpp
    src/aax_bindings.cpp
    src/aix_bindings.cpp
    src/acx_bindings.cpp
    src/acb_bindings.cpp
    src/hca_bindings.cpp
    src/awb_bindings.cpp
    src/afs_bindings.cpp
    src/cpk_bindings.cpp
    src/csb_bindings.cpp
    src/cvm_bindings.cpp
    src/sfd_bindings.cpp
    src/usm_bindings.cpp
    src/utf_bindings.cpp
    src/video_bindings.cpp
    src/wav_bindings.cpp
)

target_link_libraries(cricodecs PRIVATE CriCodecs)

set_target_properties(cricodecs PROPERTIES
    OUTPUT_NAME "__init__"
    LIBRARY_OUTPUT_DIRECTORY "${cricodecs_python_package_dir}"
    RUNTIME_OUTPUT_DIRECTORY "${cricodecs_python_package_dir}"
)

set(cricodecs_python_stub_files
    package/cricodecs/__init__.pyi
    package/cricodecs/aax.pyi
    package/cricodecs/acb.pyi
    package/cricodecs/acx.pyi
    package/cricodecs/adx.pyi
    package/cricodecs/afs.pyi
    package/cricodecs/ahx.pyi
    package/cricodecs/aix.pyi
    package/cricodecs/awb.pyi
    package/cricodecs/cpk.pyi
    package/cricodecs/csb.pyi
    package/cricodecs/cvm.pyi
    package/cricodecs/hca.pyi
    package/cricodecs/sfd.pyi
    package/cricodecs/usm.pyi
    package/cricodecs/utf.pyi
    package/cricodecs/video.pyi
    package/cricodecs/wav.pyi
    package/cricodecs/py.typed
)

foreach(cricodecs_python_stub_file IN LISTS cricodecs_python_stub_files)
    get_filename_component(cricodecs_python_stub_name "${cricodecs_python_stub_file}" NAME)
    configure_file(
        "${cricodecs_python_stub_file}"
        "${cricodecs_python_package_dir}/${cricodecs_python_stub_name}"
        COPYONLY
    )
endforeach()

install(
    TARGETS cricodecs
    LIBRARY DESTINATION cricodecs
    RUNTIME DESTINATION cricodecs
)
if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
    install(
        FILES $<TARGET_RUNTIME_DLLS:cricodecs>
        DESTINATION cricodecs
        OPTIONAL
    )
endif()
if(MINGW)
    get_filename_component(cricodecs_mingw_runtime_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    install(
        FILES
            "${cricodecs_mingw_runtime_dir}/libgcc_s_seh-1.dll"
            "${cricodecs_mingw_runtime_dir}/libstdc++-6.dll"
            "${cricodecs_mingw_runtime_dir}/libwinpthread-1.dll"
            "${cricodecs_mingw_runtime_dir}/libiconv-2.dll"
            "${cricodecs_mingw_runtime_dir}/libcharset-1.dll"
        DESTINATION cricodecs
        OPTIONAL
    )
endif()
install(
    FILES ${cricodecs_python_stub_files}
    DESTINATION cricodecs
)
