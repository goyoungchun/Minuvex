# Dear ImGui docking release 1.92.5; immutable annotated-tag object.
FetchContent_Declare(imgui
    URL "https://codeload.github.com/ocornut/imgui/tar.gz/af84778eeaab75bee0b54420f3157cf3ee00618f"
    URL_HASH SHA256=60f709af3e53ef89874eba3162f4426f87beb5fd0682a681cc3189f0d7d246ad
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(imgui)
add_library(minuvex_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp" "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp" "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp" editor/SokolImGui.cpp)
target_include_directories(minuvex_imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}")
target_link_libraries(minuvex_imgui PRIVATE minuvex_sokol_headers)
target_compile_features(minuvex_imgui PRIVATE cxx_std_20)

add_library(minuvex_editor_ui STATIC editor/EditorUI.cpp editor/SceneViewport.cpp)
target_include_directories(minuvex_editor_ui PUBLIC editor PRIVATE src runtime/standalone)
target_link_libraries(minuvex_editor_ui PUBLIC minuvex_editor_document PRIVATE minuvex_renderer minuvex_imgui minuvex_camera minuvex_sokol_headers)
minuvex_configure_target(minuvex_editor_ui)
add_executable(minuvex_editor editor/Main.cpp)
set_target_properties(minuvex_editor PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
target_include_directories(minuvex_editor PRIVATE src runtime/standalone)
target_link_libraries(minuvex_editor PRIVATE minuvex_editor_ui minuvex_renderer minuvex_imgui minuvex_sokol_app)
if(WIN32)
    target_link_libraries(minuvex_editor PRIVATE d3d11)
endif()
target_compile_definitions(minuvex_editor PRIVATE
    MINUVEX_EDITOR_SAMPLE="${PROJECT_SOURCE_DIR}/examples/assets/SharedBox.glb"
    MINUVEX_EDITOR_TEST_OUTPUT="${PROJECT_BINARY_DIR}/test-output/editor")
minuvex_configure_target(minuvex_editor)
