# Some localized MSVC/CMake combinations decode /showIncludes with a different
# code page than Ninja receives, silently dropping all header dependencies.
# Probe the raw output bytes so Ninja's prefix matches the compiler exactly.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND CMAKE_GENERATOR MATCHES "^Ninja")
    set(probe_directory "${CMAKE_BINARY_DIR}/CMakeFiles/MinvexShowIncludes")
    file(MAKE_DIRECTORY "${probe_directory}")
    file(WRITE "${probe_directory}/probe.h" "\n")
    file(WRITE "${probe_directory}/probe.cpp" "#include \"probe.h\"\n")
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /c /TP probe.cpp /Foprobe.obj
        WORKING_DIRECTORY "${probe_directory}"
        OUTPUT_VARIABLE probe_output
        ERROR_VARIABLE probe_error
        RESULT_VARIABLE probe_result
        ENCODING NONE
    )
    string(REGEX MATCH "[^\r\n]*MinvexShowIncludes[/\\\\]probe\\.h" probe_line "${probe_output}")
    string(REGEX REPLACE "[A-Za-z]:[/\\\\].*$" "" probe_prefix "${probe_line}")
    if(NOT probe_result EQUAL 0 OR NOT probe_prefix)
        message(FATAL_ERROR "Could not detect the MSVC include prefix for Ninja: ${probe_output}${probe_error}")
    endif()
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${probe_prefix}")
endif()
