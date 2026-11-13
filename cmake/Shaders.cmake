function(minvex_compile_shader target source)
    get_filename_component(shader_name "${source}" NAME)
    set(output_directory "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}")
    set(output "${output_directory}/${shader_name}.h")
    set(shader_input "${source}")
    set(slang hlsl5:metal_macos)
    if(EMSCRIPTEN)
        set(slang glsl300es)
        set(shader_input "${output_directory}/${shader_name}")
        add_custom_command(OUTPUT "${shader_input}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_directory}"
            COMMAND "${CMAKE_COMMAND}" "-DINPUT=${source}" "-DOUTPUT=${shader_input}" -P "${PROJECT_SOURCE_DIR}/cmake/WebShader.cmake"
            DEPENDS "${source}" "${PROJECT_SOURCE_DIR}/cmake/WebShader.cmake" VERBATIM)
    endif()
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_directory}"
        COMMAND "${MINVEX_SOKOL_SHDC}" --input "${shader_input}" --output "${output}"
            --slang "${slang}" --ifdef
        DEPENDS "${shader_input}" "${MINVEX_SOKOL_SHDC}"
        COMMENT "Compiling ${shader_name} to ${slang}"
        VERBATIM
    )
    target_sources(${target} PRIVATE "${output}")
    target_include_directories(${target} PRIVATE "${output_directory}")
endfunction()
