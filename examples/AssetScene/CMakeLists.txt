add_executable(minvex_asset_scene Main.cpp)
target_link_libraries(minvex_asset_scene PRIVATE minvex::renderer minvex_sokol_app)
target_include_directories(minvex_asset_scene PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_include_directories(minvex_asset_scene PRIVATE "${PROJECT_SOURCE_DIR}/runtime/standalone")
set_target_properties(minvex_asset_scene PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
minvex_configure_target(minvex_asset_scene)
add_custom_target(minvex_example_assets
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:minvex_asset_scene>/assets"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PROJECT_SOURCE_DIR}/examples/assets/SharedBox.glb" "$<TARGET_FILE_DIR:minvex_asset_scene>/assets/SharedBox.glb"
    VERBATIM)
add_dependencies(minvex_asset_scene minvex_example_assets)
