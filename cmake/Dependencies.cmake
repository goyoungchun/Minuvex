include(FetchContent)

# Update Sokol and shdc together, then rebuild both HLSL and Metal shaders.
set(MINVEX_SOKOL_REVISION f3a42297dfe64edb390deaa5883728a558499eb5)
set(MINVEX_SHDC_REVISION f32f75b0809f08e7cf9f5f456bc16b3f3576e981)

FetchContent_Declare(sokol
    URL "https://codeload.github.com/floooh/sokol/tar.gz/${MINVEX_SOKOL_REVISION}"
    URL_HASH SHA256=95570a918b2d84793447827894a945e02892a40cb6531b7f3885725f1f789dd8
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sokol)

add_library(minvex_sokol_headers INTERFACE)
target_include_directories(minvex_sokol_headers SYSTEM INTERFACE "$<BUILD_INTERFACE:${sokol_SOURCE_DIR}>")
target_compile_definitions(minvex_sokol_headers INTERFACE "$<BUILD_INTERFACE:${MINVEX_SOKOL_BACKEND}>")
if(WIN32)
    target_compile_definitions(minvex_sokol_headers INTERFACE "$<BUILD_INTERFACE:NOMINMAX;WIN32_LEAN_AND_MEAN>")
endif()

set(MINVEX_SOKOL_SHDC "" CACHE FILEPATH "Override the pinned sokol-shdc executable (for offline builds)")
if(MINVEX_SOKOL_SHDC)
    if(NOT EXISTS "${MINVEX_SOKOL_SHDC}")
        message(FATAL_ERROR "MINVEX_SOKOL_SHDC does not exist: ${MINVEX_SOKOL_SHDC}")
    endif()
else()
    # Select by build HOST, not target: shdc must run during the build.
    if(CMAKE_HOST_WIN32)
        set(shdc_platform win32)
        set(shdc_name sokol-shdc.exe)
        set(shdc_hash e727f4d2fe0e7ee595f16f68188e7350009ca2e35bf2eeeedea93278bf74d4e4)
    elseif(CMAKE_HOST_APPLE)
        set(shdc_name sokol-shdc)
        if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
            set(shdc_platform osx_arm64)
            set(shdc_hash 2a807d62dffcc67a50a6ab51df57cde2fc8b23a3daf6207177d79e00fbd4086b)
        else()
            set(shdc_platform osx)
            set(shdc_hash 67e5c841a4ca58488e6592560dd8795c7b232a8805d2350ca346028541716252)
        endif()
    else()
        message(FATAL_ERROR "Provide MINVEX_SOKOL_SHDC when building on this host.")
    endif()

    set(shdc_directory "${CMAKE_BINARY_DIR}/_deps/sokol-shdc/${MINVEX_SHDC_REVISION}/${shdc_platform}")
    file(MAKE_DIRECTORY "${shdc_directory}")
    set(MINVEX_SOKOL_SHDC "${shdc_directory}/${shdc_name}")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/floooh/sokol-tools-bin/${MINVEX_SHDC_REVISION}/bin/${shdc_platform}/${shdc_name}"
        "${MINVEX_SOKOL_SHDC}"
        EXPECTED_HASH "SHA256=${shdc_hash}"
        TLS_VERIFY ON STATUS shdc_status
    )
    list(GET shdc_status 0 shdc_status_code)
    if(NOT shdc_status_code EQUAL 0)
        message(FATAL_ERROR "sokol-shdc download failed: ${shdc_status}. For offline builds, set MINVEX_SOKOL_SHDC.")
    endif()
    if(CMAKE_HOST_UNIX)
        file(CHMOD "${MINVEX_SOKOL_SHDC}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    endif()
endif()
