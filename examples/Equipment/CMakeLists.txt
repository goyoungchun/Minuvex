add_executable(minvex_equipment Main.cpp)
target_link_libraries(minvex_equipment PRIVATE minvex::viewer)
set_target_properties(minvex_equipment PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
minvex_configure_target(minvex_equipment)
