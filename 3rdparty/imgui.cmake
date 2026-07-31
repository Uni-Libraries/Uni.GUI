add_library(unigui_imgui)
add_library(UniGUI::imgui ALIAS unigui_imgui)

set(unigui_imgui_dir "${CMAKE_CURRENT_LIST_DIR}/imgui")

target_sources(unigui_imgui
    PRIVATE
        "${unigui_imgui_dir}/imgui.cpp"
        "${unigui_imgui_dir}/imgui_demo.cpp"
        "${unigui_imgui_dir}/imgui_draw.cpp"
        "${unigui_imgui_dir}/imgui_tables.cpp"
        "${unigui_imgui_dir}/imgui_widgets.cpp"
        "${unigui_imgui_dir}/misc/cpp/imgui_stdlib.cpp"
        "${unigui_imgui_dir}/backends/imgui_impl_sdl3.cpp"
        "${unigui_imgui_dir}/backends/imgui_impl_sdlrenderer3.cpp"
        "${unigui_imgui_dir}/backends/imgui_impl_sdlgpu3.cpp"
    PUBLIC
        FILE_SET core_headers TYPE HEADERS
        BASE_DIRS "${unigui_imgui_dir}"
        FILES
            "${unigui_imgui_dir}/imgui.h"
            "${unigui_imgui_dir}/imconfig.h"
            "${unigui_imgui_dir}/imgui_internal.h"
            "${unigui_imgui_dir}/imstb_rectpack.h"
            "${unigui_imgui_dir}/imstb_textedit.h"
            "${unigui_imgui_dir}/imstb_truetype.h"
        FILE_SET backend_headers TYPE HEADERS
        BASE_DIRS "${unigui_imgui_dir}/backends"
        FILES
            "${unigui_imgui_dir}/backends/imgui_impl_sdl3.h"
            "${unigui_imgui_dir}/backends/imgui_impl_sdlrenderer3.h"
            "${unigui_imgui_dir}/backends/imgui_impl_sdlgpu3.h"
        FILE_SET stdlib_headers TYPE HEADERS
        BASE_DIRS "${unigui_imgui_dir}/misc/cpp"
        FILES "${unigui_imgui_dir}/misc/cpp/imgui_stdlib.h"
)

target_include_directories(unigui_imgui PUBLIC
    "$<BUILD_INTERFACE:${unigui_imgui_dir}>"
    "$<BUILD_INTERFACE:${unigui_imgui_dir}/backends>"
    "$<BUILD_INTERFACE:${unigui_imgui_dir}/misc/cpp>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/uni/gui/thirdparty/imgui>"
)
target_compile_definitions(unigui_imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(unigui_imgui PUBLIC SDL3::SDL3)

if(WIN32 AND BUILD_SHARED_LIBS)
    target_compile_definitions(unigui_imgui
        PRIVATE "IMGUI_API=__declspec(dllexport)"
        INTERFACE "IMGUI_API=__declspec(dllimport)"
    )
endif()

set_target_properties(unigui_imgui PROPERTIES
    EXPORT_NAME imgui
    OUTPUT_NAME UniGUI-imgui
    POSITION_INDEPENDENT_CODE ON
    VERSION "${UNIGUI_VERSION_CORE}"
    SOVERSION "${UNIGUI_ABI_VERSION}"
)
if(WIN32 AND BUILD_SHARED_LIBS)
    set_property(TARGET unigui_imgui PROPERTY OUTPUT_NAME "UniGUI-imgui-${UNIGUI_ABI_VERSION}")
endif()

if(APPLE)
    set_property(TARGET unigui_imgui PROPERTY INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_property(TARGET unigui_imgui PROPERTY INSTALL_RPATH "$ORIGIN")
endif()

if(UNIGUI_INSTALL)
    install(TARGETS unigui_imgui
        EXPORT UniGUITargets
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        FILE_SET core_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/uni/gui/thirdparty/imgui"
        FILE_SET backend_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/uni/gui/thirdparty/imgui"
        FILE_SET stdlib_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/uni/gui/thirdparty/imgui"
    )
endif()
