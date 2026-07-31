add_library(unigui_implot)
add_library(UniGUI::implot ALIAS unigui_implot)

set(unigui_implot_dir "${CMAKE_CURRENT_LIST_DIR}/implot")

target_sources(unigui_implot
    PRIVATE
        "${unigui_implot_dir}/implot.cpp"
        "${unigui_implot_dir}/implot_demo.cpp"
        "${unigui_implot_dir}/implot_items.cpp"
    PUBLIC
        FILE_SET public_headers TYPE HEADERS
        BASE_DIRS "${unigui_implot_dir}"
        FILES
            "${unigui_implot_dir}/implot.h"
            "${unigui_implot_dir}/implot_internal.h"
)

target_include_directories(unigui_implot PUBLIC
    "$<BUILD_INTERFACE:${unigui_implot_dir}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/uni/gui/thirdparty/implot>"
)
target_link_libraries(unigui_implot PUBLIC unigui_imgui)

if(WIN32 AND BUILD_SHARED_LIBS)
    target_compile_definitions(unigui_implot
        PRIVATE "IMPLOT_API=__declspec(dllexport)"
        INTERFACE "IMPLOT_API=__declspec(dllimport)"
    )
endif()

set_target_properties(unigui_implot PROPERTIES
    EXPORT_NAME implot
    OUTPUT_NAME UniGUI-implot
    POSITION_INDEPENDENT_CODE ON
    VERSION "${UNIGUI_VERSION_CORE}"
    SOVERSION "${UNIGUI_ABI_VERSION}"
)
if(WIN32 AND BUILD_SHARED_LIBS)
    set_property(TARGET unigui_implot PROPERTY OUTPUT_NAME "UniGUI-implot-${UNIGUI_ABI_VERSION}")
endif()

if(APPLE)
    set_property(TARGET unigui_implot PROPERTY INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_property(TARGET unigui_implot PROPERTY INSTALL_RPATH "$ORIGIN")
endif()

if(UNIGUI_INSTALL)
    install(TARGETS unigui_implot
        EXPORT UniGUITargets
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/uni/gui/thirdparty/implot"
    )
endif()
