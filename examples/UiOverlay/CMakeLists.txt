add_executable(minvex_ui_overlay Main.cpp)
target_link_libraries(minvex_ui_overlay PRIVATE minvex::viewer)
set_target_properties(minvex_ui_overlay PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
minvex_configure_target(minvex_ui_overlay)
