# Dear ImGui docking release 1.92.5; immutable annotated-tag object.
FetchContent_Declare(imgui
    URL "https://codeload.github.com/ocornut/imgui/tar.gz/af84778eeaab75bee0b54420f3157cf3ee00618f"
    URL_HASH SHA256=60f709af3e53ef89874eba3162f4426f87beb5fd0682a681cc3189f0d7d246ad
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(imgui)

# Native dialogs are Editor-only; no NFDe headers/libs enter the public SDK.
FetchContent_Declare(nfde
    URL "https://codeload.github.com/btzy/nativefiledialog-extended/tar.gz/v1.4.1"
    URL_HASH SHA256=4a01eafa921169f009f3e83bea8e287d9e5d0b94ca9064cf7f0a546652d7c884
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
function(minvex_prepare_native_dialogs)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(BUILD_SHARED_LIBS OFF)
    set(NFD_BUILD_TESTS OFF)
    set(NFD_INSTALL OFF)
    set(CMAKE_CXX_STANDARD 20)
    FetchContent_MakeAvailable(nfde)
endfunction()
minvex_prepare_native_dialogs()
add_library(minvex_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp" "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp" "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp" editor/SokolImGui.cpp)
target_sources(minvex_imgui PRIVATE third_party/imguizmo/ImGuizmo.cpp)
target_include_directories(minvex_imgui SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/third_party/imguizmo")
target_include_directories(minvex_imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}")
target_link_libraries(minvex_imgui PRIVATE minvex_sokol_headers)
target_compile_features(minvex_imgui PRIVATE cxx_std_20)

add_library(minvex_editor_ui STATIC editor/EditorUI.cpp editor/AssetPanels.cpp editor/PerformancePanel.cpp editor/DebugPanels.cpp editor/SceneViewport.cpp editor/NativeFiles.cpp editor/EditorPreferences.cpp)
target_sources(minvex_editor_ui PRIVATE editor/AdditivePanel.cpp)
target_sources(minvex_editor_ui PRIVATE editor/AssetJobs.cpp)
target_sources(minvex_editor_ui PRIVATE editor/TransformTools.cpp)
target_include_directories(minvex_editor_ui PUBLIC editor PRIVATE src runtime/standalone)
target_link_libraries(minvex_editor_ui PUBLIC minvex_editor_document PRIVATE minvex_renderer minvex_imgui minvex_camera minvex_sokol_headers minvex_json_io nfd::nfd)
target_include_directories(minvex_editor_ui SYSTEM PRIVATE third_party/json)
if(APPLE)
    set_source_files_properties(editor/NativeFiles.cpp PROPERTIES LANGUAGE OBJCXX)
    set_target_properties(minvex_editor_ui PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES)
    target_compile_options(minvex_editor_ui PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
    target_link_libraries(minvex_editor_ui PRIVATE "-framework AppKit")
elseif(WIN32)
    target_link_libraries(minvex_editor_ui PRIVATE shell32)
endif()
minvex_configure_target(minvex_editor_ui)
add_executable(minvex_editor editor/Main.cpp)
set_target_properties(minvex_editor PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
target_include_directories(minvex_editor PRIVATE src runtime/standalone)
target_link_libraries(minvex_editor PRIVATE minvex_editor_ui minvex_renderer minvex_imgui minvex_sokol_app)
if(WIN32)
    target_link_libraries(minvex_editor PRIVATE d3d11)
endif()
target_compile_definitions(minvex_editor PRIVATE
    MINVEX_EDITOR_SAMPLE="${PROJECT_SOURCE_DIR}/examples/assets/SharedBox.glb"
    MINVEX_EDITOR_ANIMATION_SAMPLE="${PROJECT_SOURCE_DIR}/examples/assets/AnimatedRibbon.gltf"
    MINVEX_EDITOR_DOCUMENTATION="${PROJECT_SOURCE_DIR}/README.md"
    MINVEX_EDITOR_ASSET_FIXTURES="${PROJECT_SOURCE_DIR}/tests/Fixtures"
    MINVEX_EDITOR_TEST_OUTPUT="${PROJECT_BINARY_DIR}/test-output/editor")
minvex_configure_target(minvex_editor)
