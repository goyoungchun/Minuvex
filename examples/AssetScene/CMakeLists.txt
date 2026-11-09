add_executable(minuvex_asset_scene Main.cpp)
target_link_libraries(minuvex_asset_scene PRIVATE minuvex::renderer minuvex_sokol_app)
target_include_directories(minuvex_asset_scene PRIVATE "${PROJECT_SOURCE_DIR}/src")
set_target_properties(minuvex_asset_scene PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
minuvex_configure_target(minuvex_asset_scene)
add_custom_target(minuvex_example_assets
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:minuvex_asset_scene>/assets"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PROJECT_SOURCE_DIR}/examples/assets/SharedBox.glb" "$<TARGET_FILE_DIR:minuvex_asset_scene>/assets/SharedBox.glb"
    VERBATIM)
add_dependencies(minuvex_asset_scene minuvex_example_assets)
