add_executable(minvex_web bindings/WebHost.cpp)
target_link_libraries(minvex_web PRIVATE minvex::renderer minvex_camera minvex_sokol_headers)
target_include_directories(minvex_web PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/runtime/standalone")
target_include_directories(minvex_web SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party/json")
minvex_configure_target(minvex_web)
set_target_properties(minvex_web PROPERTIES OUTPUT_NAME minvex-native SUFFIX .js)
target_link_options(minvex_web PRIVATE --bind --no-entry -fexceptions
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createMinvexModule
    -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=1073741824
    -sABORTING_MALLOC=0 -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=FS
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sASSERTIONS=1)
foreach(file minvex.js index.html)
    configure_file("${file}" "${CMAKE_CURRENT_BINARY_DIR}/${file}" COPYONLY)
endforeach()
install(TARGETS minvex_web RUNTIME DESTINATION web COMPONENT web)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/minvex-native.wasm" minvex.js index.html DESTINATION web COMPONENT web)
install(FILES "${sokol_SOURCE_DIR}/LICENSE" DESTINATION web/licenses/sokol COMPONENT web)
install(FILES "${PROJECT_SOURCE_DIR}/third_party/stb/stb_image.h" DESTINATION web/licenses/stb COMPONENT web)
foreach(dependency cgltf json meshoptimizer tinyobjloader ufbx)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/third_party/${dependency}/" DESTINATION "web/licenses/${dependency}"
        COMPONENT web FILES_MATCHING PATTERN "LICENSE*")
endforeach()
install(FILES "${EMSCRIPTEN_ROOT_PATH}/LICENSE" DESTINATION web/licenses/emscripten COMPONENT web)
install(FILES "${EMSCRIPTEN_ROOT_PATH}/system/lib/libc/musl/COPYRIGHT" DESTINATION web/licenses/musl COMPONENT web)
install(FILES "${EMSCRIPTEN_ROOT_PATH}/system/lib/libcxx/LICENSE.TXT" DESTINATION web/licenses/libcxx COMPONENT web)
