add_executable(unigui_texture_tests ui_texture_tests.cpp)
target_compile_features(unigui_texture_tests PRIVATE cxx_std_23)
target_include_directories(unigui_texture_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(unigui_texture_tests PRIVATE UniGUI::UniGUI)
set_target_properties(unigui_texture_tests PROPERTIES CXX_EXTENSIONS OFF)

add_test(NAME unigui.texture COMMAND unigui_texture_tests)

add_executable(unigui_app_tests ui_app_tests.cpp)
target_compile_features(unigui_app_tests PRIVATE cxx_std_23)
target_link_libraries(unigui_app_tests PRIVATE UniGUI::UniGUI)
set_target_properties(unigui_app_tests PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME unigui.app COMMAND unigui_app_tests)

add_executable(unigui_element_registry_tests
    ui_element_registry_tests.cpp
    "${PROJECT_SOURCE_DIR}/src/ui_element_registry.cpp"
)
target_compile_features(unigui_element_registry_tests PRIVATE cxx_std_23)
target_include_directories(unigui_element_registry_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(unigui_element_registry_tests PRIVATE UniGUI::UniGUI)
set_target_properties(unigui_element_registry_tests PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME unigui.element-registry COMMAND unigui_element_registry_tests)

add_executable(unigui_dock_layout_tests
    ui_dock_layout_tests.cpp
    "${PROJECT_SOURCE_DIR}/src/ui_dock_layout.cpp"
)
target_compile_features(unigui_dock_layout_tests PRIVATE cxx_std_23)
target_include_directories(unigui_dock_layout_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(unigui_dock_layout_tests PRIVATE UniGUI::UniGUI)
set_target_properties(unigui_dock_layout_tests PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME unigui.dock-layout COMMAND unigui_dock_layout_tests)

add_executable(unigui_nodes_tests ui_nodes_tests.cpp)
target_compile_features(unigui_nodes_tests PRIVATE cxx_std_23)
target_include_directories(unigui_nodes_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(unigui_nodes_tests PRIVATE UniGUI::UniGUI)
set_target_properties(unigui_nodes_tests PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME unigui.nodes COMMAND unigui_nodes_tests)

add_executable(unigui_nodes_type_compatibility_tests
    ui_nodes_type_compatibility_tests.cpp
)
target_compile_features(unigui_nodes_type_compatibility_tests PRIVATE cxx_std_23)
target_include_directories(unigui_nodes_type_compatibility_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(unigui_nodes_type_compatibility_tests PRIVATE UniGUI::UniGUI)
set_target_properties(unigui_nodes_type_compatibility_tests PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME unigui.nodes-type-compatibility COMMAND unigui_nodes_type_compatibility_tests)

add_executable(unigui_nodes_state_machine_tests ui_nodes_state_machine_tests.cpp)
target_compile_features(unigui_nodes_state_machine_tests PRIVATE cxx_std_23)
target_link_libraries(unigui_nodes_state_machine_tests PRIVATE UniGUI::UniGUI)
set_target_properties(unigui_nodes_state_machine_tests PROPERTIES CXX_EXTENSIONS OFF)
foreach(seed IN ITEMS 0xC0FFEE 0x5EED1234 0xA11CE55 0xDEADBEEF)
    add_test(
        NAME "unigui.nodes-state-machine.${seed}"
        COMMAND unigui_nodes_state_machine_tests --seed "${seed}" --steps 5000
    )
    set_tests_properties("unigui.nodes-state-machine.${seed}" PROPERTIES
        LABELS "nodes-state-machine"
        TIMEOUT 600
    )
endforeach()

add_executable(unigui_nodes_io_tests ui_nodes_io_tests.cpp)
target_compile_features(unigui_nodes_io_tests PRIVATE cxx_std_23)
target_link_libraries(unigui_nodes_io_tests PRIVATE UniGUI::UniGUI)
set_target_properties(unigui_nodes_io_tests PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME unigui.nodes-io COMMAND unigui_nodes_io_tests)

add_executable(unigui_nodes_graph_asset_tests ui_nodes_graph_asset_tests.cpp)
target_compile_features(unigui_nodes_graph_asset_tests PRIVATE cxx_std_23)
target_link_libraries(unigui_nodes_graph_asset_tests PRIVATE UniGUI::UniGUI)
set_target_properties(unigui_nodes_graph_asset_tests PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME unigui.nodes-graph-assets COMMAND unigui_nodes_graph_asset_tests)

add_executable(unigui_nodes_editor_geometry_tests
    ui_nodes_editor_geometry_tests.cpp
    "${PROJECT_SOURCE_DIR}/src/nodes/editor/geometry.cpp"
)
target_compile_features(unigui_nodes_editor_geometry_tests PRIVATE cxx_std_23)
target_include_directories(unigui_nodes_editor_geometry_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(unigui_nodes_editor_geometry_tests PRIVATE UniGUI::UniGUI)
set_target_properties(unigui_nodes_editor_geometry_tests PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME unigui.nodes-editor-geometry COMMAND unigui_nodes_editor_geometry_tests)

add_executable(unigui_nodes_snapshot_tests ui_nodes_snapshot_tests.cpp)
target_compile_features(unigui_nodes_snapshot_tests PRIVATE cxx_std_23)
target_link_libraries(unigui_nodes_snapshot_tests PRIVATE UniGUI::UniGUI)
set_target_properties(unigui_nodes_snapshot_tests PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME unigui.nodes-snapshot COMMAND unigui_nodes_snapshot_tests)

add_executable(unigui_nodes_stage_five_tests ui_nodes_stage_five_tests.cpp)
target_compile_features(unigui_nodes_stage_five_tests PRIVATE cxx_std_23)
target_link_libraries(unigui_nodes_stage_five_tests PRIVATE UniGUI::UniGUI)
set_target_properties(unigui_nodes_stage_five_tests PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME unigui.nodes-stage-five COMMAND unigui_nodes_stage_five_tests)

add_test(
    NAME unigui.repository-package-files
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
        -P "${PROJECT_SOURCE_DIR}/cmake/VerifyRepositoryFiles.cmake"
)

add_test(
    NAME unigui.version-metadata
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
        -P "${PROJECT_SOURCE_DIR}/cmake/VerifyVersionMetadata.cmake"
)

add_test(
    NAME unigui.documentation-links
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
        -P "${PROJECT_SOURCE_DIR}/cmake/VerifyDocumentationLinks.cmake"
)

add_test(
    NAME unigui.package-version-release-metadata
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DEXPECTED_TAG=v${UNIGUI_VERSION}"
        -P "${PROJECT_SOURCE_DIR}/cmake/VerifyReleaseVersion.cmake"
)

if(UNIGUI_INSTALL)
    function(unigui_add_package_version_test test_name version_file request_version
             expected_version expect_compatible expect_exact)
        add_test(
            NAME "${test_name}"
            COMMAND "${CMAKE_COMMAND}"
                "-DVERSION_FILE=${version_file}"
                "-DREQUEST_VERSION=${request_version}"
                "-DEXPECTED_VERSION=${expected_version}"
                "-DEXPECT_COMPATIBLE=${expect_compatible}"
                "-DEXPECT_EXACT=${expect_exact}"
                -P "${PROJECT_SOURCE_DIR}/cmake/VerifyPackageVersion.cmake"
        )
    endfunction()

    set(unigui_current_version_file "${PROJECT_BINARY_DIR}/UniGUIConfigVersion.cmake")
    unigui_add_package_version_test(
        unigui.package-version.minimum-1.0
        "${unigui_current_version_file}" 1.0 "${UNIGUI_VERSION}" ON OFF)
    unigui_add_package_version_test(
        unigui.package-version.exact-1.0.0
        "${unigui_current_version_file}" 1.0.0 "${UNIGUI_VERSION}" ON ON)
    unigui_add_package_version_test(
        unigui.package-version.reject-newer-1.0.1
        "${unigui_current_version_file}" 1.0.1 "${UNIGUI_VERSION}" OFF OFF)
    unigui_add_package_version_test(
        unigui.package-version.reject-major-2
        "${unigui_current_version_file}" 2.0.0 "${UNIGUI_VERSION}" OFF OFF)

    foreach(synthetic_version IN ITEMS 1.0.1 1.1.0 2.0.0)
        set(synthetic_version_file
            "${CMAKE_CURRENT_BINARY_DIR}/UniGUIConfigVersion-${synthetic_version}.cmake")
        write_basic_package_version_file(
            "${synthetic_version_file}"
            VERSION "${synthetic_version}"
            COMPATIBILITY SameMajorVersion
        )
        if(synthetic_version MATCHES "^1\\.")
            set(expect_compatible ON)
        else()
            set(expect_compatible OFF)
        endif()
        unigui_add_package_version_test(
            "unigui.package-version.synthetic-${synthetic_version}"
            "${synthetic_version_file}" 1.0.0 "${synthetic_version}"
            "${expect_compatible}" OFF)
    endforeach()
endif()

if(BUILD_SHARED_LIBS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(
        NAME unigui.elf-abi-built
        COMMAND "${CMAKE_COMMAND}"
            "-DCORE_LIBRARY=$<TARGET_FILE:unigui>"
            "-DIMGUI_LIBRARY=$<TARGET_FILE:unigui_imgui>"
            "-DIMPLOT_LIBRARY=$<TARGET_FILE:unigui_implot>"
            "-DEXPECTED_ABI_VERSION=${UNIGUI_ABI_VERSION}"
            "-DREADELF_EXECUTABLE=${CMAKE_READELF}"
            -P "${PROJECT_SOURCE_DIR}/cmake/VerifyElfAbi.cmake"
    )
    set_tests_properties(unigui.elf-abi-built PROPERTIES LABELS abi)
endif()

if(UNIGUI_ENABLE_ABI_BASELINE_CHECK)
    foreach(abi_target IN ITEMS unigui unigui_imgui unigui_implot)
        if(abi_target STREQUAL "unigui")
            set(abi_name UniGUI)
        elseif(abi_target STREQUAL "unigui_imgui")
            set(abi_name UniGUI-imgui)
        else()
            set(abi_name UniGUI-implot)
        endif()
        add_test(
            NAME "unigui.abi-baseline.${abi_name}"
            COMMAND "${CMAKE_COMMAND}"
                "-DABIDIFF_EXECUTABLE=${UNIGUI_ABIDIFF_EXECUTABLE}"
                "-DNM_EXECUTABLE=${UNIGUI_NM_EXECUTABLE}"
                "-DREADELF_EXECUTABLE=${UNIGUI_READELF_EXECUTABLE}"
                "-DBASELINE_ABI=${UNIGUI_ABI_BASELINE_DIR}/${abi_name}.abi"
                "-DBASELINE_SYMBOLS=${UNIGUI_ABI_BASELINE_DIR}/${abi_name}.symbols"
                "-DCURRENT_LIBRARY=$<TARGET_FILE:${abi_target}>"
                -P "${PROJECT_SOURCE_DIR}/cmake/VerifyAbiBaseline.cmake"
        )
        set_tests_properties("unigui.abi-baseline.${abi_name}" PROPERTIES LABELS abi)
    endforeach()
endif()

if(UNIGUI_BUILD_BENCHMARKS)
    add_executable(unigui_nodes_benchmarks
        ui_nodes_benchmarks.cpp
        ui_nodes_benchmark_support.cpp
        ui_nodes_benchmark_editor.cpp
        ui_nodes_benchmark_assets.cpp
        ui_nodes_benchmark_io.cpp
        ui_nodes_benchmark_mutations.cpp
        ui_nodes_benchmark_registry.cpp
    )
    target_compile_features(unigui_nodes_benchmarks PRIVATE cxx_std_23)
    target_link_libraries(unigui_nodes_benchmarks PRIVATE UniGUI::UniGUI)
    if(WIN32)
        target_link_libraries(unigui_nodes_benchmarks PRIVATE Psapi)
    endif()
    set_target_properties(unigui_nodes_benchmarks PROPERTIES CXX_EXTENSIONS OFF)
    foreach(suite IN ITEMS frames-10k property-10k property-1m sparse-100k dense-links graph-assets io migrations mutations conversion-catalog)
        add_test(
            NAME unigui.nodes-performance.${suite}
            COMMAND unigui_nodes_benchmarks --suite ${suite}
        )
        set_tests_properties(unigui.nodes-performance.${suite} PROPERTIES
            LABELS "performance;nodes-performance;${suite}"
            RUN_SERIAL TRUE
            TIMEOUT 300
        )
    endforeach()
endif()

add_library(unigui_public_header_tests OBJECT
    package/header_ui_app.cpp
    package/header_ui_asset.cpp
    package/header_ui_callbacks.cpp
    package/header_ui_config.cpp
    package/header_ui_dispatcher.cpp
    package/header_ui_element.cpp
    package/header_ui_error.cpp
    package/header_ui_event.cpp
    package/header_ui_export.cpp
    package/header_ui_frame.cpp
    package/header_ui_layout.cpp
    package/header_ui_nodes.cpp
    package/header_ui_nodes_graph_asset.cpp
    package/header_ui_nodes_io.cpp
    package/header_ui_nodes_routing.cpp
    package/header_ui_nodes_snapshot.cpp
    package/header_ui_nodes_workspace.cpp
    package/header_ui_state.cpp
    package/header_ui_texture.cpp
)
target_compile_features(unigui_public_header_tests PRIVATE cxx_std_23)
target_link_libraries(unigui_public_header_tests PRIVATE UniGUI::UniGUI)
set_target_properties(unigui_public_header_tests PROPERTIES CXX_EXTENSIONS OFF)

if(UNIGUI_ENABLE_RUNTIME_TESTS)
    add_executable(unigui_runtime_tests ui_runtime_tests.cpp)
    target_compile_features(unigui_runtime_tests PRIVATE cxx_std_23)
    target_link_libraries(unigui_runtime_tests PRIVATE UniGUI::UniGUI)
    set_target_properties(unigui_runtime_tests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME unigui.runtime COMMAND unigui_runtime_tests)
    set_tests_properties(unigui.runtime PROPERTIES LABELS runtime)

    add_executable(unigui_callback_loop_tests ui_callback_loop_tests.cpp)
    target_compile_features(unigui_callback_loop_tests PRIVATE cxx_std_23)
    target_link_libraries(unigui_callback_loop_tests PRIVATE UniGUI::Main)
    set_target_properties(unigui_callback_loop_tests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME unigui.callback-loop COMMAND unigui_callback_loop_tests)
    set_tests_properties(unigui.callback-loop PROPERTIES LABELS "runtime;sdl-callback-loop" TIMEOUT 10)

    add_executable(unigui_dispatcher_fault_tests ui_dispatcher_fault_tests.cpp)
    target_compile_features(unigui_dispatcher_fault_tests PRIVATE cxx_std_23)
    target_link_libraries(unigui_dispatcher_fault_tests PRIVATE UniGUI::UniGUI)
    set_target_properties(unigui_dispatcher_fault_tests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME unigui.dispatcher-fault COMMAND unigui_dispatcher_fault_tests)
    set_tests_properties(unigui.dispatcher-fault PROPERTIES
        LABELS "runtime;sdl-event-queue-fault"
        TIMEOUT 15
    )

    add_executable(unigui_sdlgpu_smoke_tests ui_sdlgpu_smoke_tests.cpp)
    target_compile_features(unigui_sdlgpu_smoke_tests PRIVATE cxx_std_23)
    target_link_libraries(unigui_sdlgpu_smoke_tests PRIVATE UniGUI::UniGUI)
    set_target_properties(unigui_sdlgpu_smoke_tests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME unigui.sdlgpu-smoke COMMAND unigui_sdlgpu_smoke_tests)
    set_tests_properties(unigui.sdlgpu-smoke PROPERTIES
        LABELS "runtime;gpu"
        SKIP_RETURN_CODE 77
        TIMEOUT 15
    )
endif()

if(UNIGUI_BUILD_TEST_ENGINE_TESTS)
    find_package(Threads REQUIRED)

    set(imgui_dir "${PROJECT_SOURCE_DIR}/3rdparty/imgui")
    set(implot_dir "${PROJECT_SOURCE_DIR}/3rdparty/implot")
    set(test_engine_root "${imgui_test_engine_SOURCE_DIR}")
    set(test_engine_dir "${test_engine_root}/imgui_test_engine")

    add_executable(unigui_imgui_test_engine_tests
        ui_test_engine_main.cpp
        ui_test_engine_cases.cpp
        ui_test_engine_nodes.cpp

        "${PROJECT_SOURCE_DIR}/src/ui_app.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_asset_manager.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_dispatcher.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_dock_layout.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_element_registry.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_font.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/commands/clipboard.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/commands/command_stack.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/commands/graph.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/commands/group.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/commands/hierarchy.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/commands/link.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/commands/node.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/commands/pin.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/commands/protection.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/commands/transaction.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/commands/transaction_commit.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/commands/transaction_journal.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/commands/transaction_presentation.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/commands/transaction_protection.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/commands/transaction_semantic.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/clipboard.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/context_menu.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/editor.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/editor_context.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/frame.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/frame_helpers.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/geometry.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/geometry_cache.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/hit_testing.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/input_state.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/minimap.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/navigation.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/rendering.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/rendering_overlays.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/selection.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/shortcuts.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_nodes_graph.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_nodes_graph_validation.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_nodes_graph_asset.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_nodes_io.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_nodes_json.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_nodes_layout.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_nodes_node_ui.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_nodes_policy.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_nodes_presentation.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_nodes_registry.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_nodes_routing.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_nodes_snapshot.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_texture.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_winsys_sdl.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_renderer_sdl.cpp"
        "${PROJECT_SOURCE_DIR}/src/ui_renderer_sdlgpu.cpp"

        "${imgui_dir}/imgui.cpp"
        "${imgui_dir}/imgui_demo.cpp"
        "${imgui_dir}/imgui_draw.cpp"
        "${imgui_dir}/imgui_tables.cpp"
        "${imgui_dir}/imgui_widgets.cpp"
        "${imgui_dir}/misc/cpp/imgui_stdlib.cpp"
        "${imgui_dir}/backends/imgui_impl_sdl3.cpp"
        "${imgui_dir}/backends/imgui_impl_sdlrenderer3.cpp"
        "${imgui_dir}/backends/imgui_impl_sdlgpu3.cpp"

        "${implot_dir}/implot.cpp"
        "${implot_dir}/implot_demo.cpp"
        "${implot_dir}/implot_items.cpp"

        "${test_engine_dir}/imgui_capture_tool.cpp"
        "${test_engine_dir}/imgui_te_context.cpp"
        "${test_engine_dir}/imgui_te_coroutine.cpp"
        "${test_engine_dir}/imgui_te_engine.cpp"
        "${test_engine_dir}/imgui_te_exporters.cpp"
        "${test_engine_dir}/imgui_te_perftool.cpp"
        "${test_engine_dir}/imgui_te_ui.cpp"
        "${test_engine_dir}/imgui_te_utils.cpp"
    )

    target_compile_features(unigui_imgui_test_engine_tests PRIVATE cxx_std_23)
    set_source_files_properties(
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/clipboard.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/context_menu.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/editor.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/editor_context.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/frame.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/frame_helpers.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/geometry.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/geometry_cache.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/hit_testing.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/input_state.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/minimap.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/navigation.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/rendering.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/rendering_overlays.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/selection.cpp"
        "${PROJECT_SOURCE_DIR}/src/nodes/editor/shortcuts.cpp"
        PROPERTIES COMPILE_DEFINITIONS IMGUI_DEFINE_MATH_OPERATORS
    )
    target_compile_definitions(unigui_imgui_test_engine_tests PRIVATE
        UNI_GUI_STATIC_DEFINE
        UNIGUI_TEST_ENGINE_TEST_BUILD
        IMGUI_ENABLE_TEST_ENGINE
        IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1
        IMGUI_TEST_ENGINE_ENABLE_IMPLOT=0
        IMGUI_TEST_ENGINE_ENABLE_CAPTURE=0
        IMGUI_TEST_ENGINE_ENABLE_STD_FUNCTION=0
    )
    target_include_directories(unigui_imgui_test_engine_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_BINARY_DIR}/src/generated"
        "${PROJECT_SOURCE_DIR}/src"
        "${PROJECT_SOURCE_DIR}/tests"
        "${imgui_dir}"
        "${imgui_dir}/backends"
        "${imgui_dir}/misc/cpp"
        "${implot_dir}"
        "${test_engine_root}"
        "${test_engine_dir}"
    )
    target_link_libraries(unigui_imgui_test_engine_tests PRIVATE
        SDL3::SDL3
        Threads::Threads
        unigui_nlohmann_json
        "${CMAKE_DL_LIBS}"
    )
    if(WIN32)
        target_link_libraries(unigui_imgui_test_engine_tests PRIVATE advapi32)
    endif()
    set_target_properties(unigui_imgui_test_engine_tests PROPERTIES CXX_EXTENSIONS OFF)

    foreach(test_case IN ITEMS
        docking_layout
        element_registry
        textures
        nodes_link_creation
        nodes_converter_insertion
        nodes_reconnect
        nodes_context_menus
        nodes_minimap
        nodes_breadcrumbs
        nodes_custom_body
        nodes_groups
        nodes_clipboard
        nodes_policy_rejection
    )
        add_test(
            NAME "unigui.imgui.${test_case}"
            COMMAND unigui_imgui_test_engine_tests "--filter=${test_case}"
        )
        set_tests_properties("unigui.imgui.${test_case}" PROPERTIES
            LABELS "runtime;imgui-test-engine"
            TIMEOUT 60
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        )
    endforeach()
endif()
