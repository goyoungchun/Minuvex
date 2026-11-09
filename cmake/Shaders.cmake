function(minuvex_compile_shader target source)
    get_filename_component(shader_name "${source}" NAME)
    set(output_directory "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}")
    set(output "${output_directory}/${shader_name}.h")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_directory}"
        COMMAND "${MINUVEX_SOKOL_SHDC}" --input "${source}" --output "${output}"
            --slang hlsl5:metal_macos --ifdef
        DEPENDS "${source}" "${MINUVEX_SOKOL_SHDC}"
        COMMENT "Compiling ${shader_name} to HLSL5 and Metal"
        VERBATIM
    )
    target_sources(${target} PRIVATE "${output}")
    target_include_directories(${target} PRIVATE "${output_directory}")
endfunction()
