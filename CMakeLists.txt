cmake_minimum_required(VERSION 3.25)
cmake_policy(SET CMP0091 NEW) # Use the new MSVC runtime library flag behavior
set(CMAKE_CROSS_CONFIGS "all")
project(SimpleSG LANGUAGES C CXX)

if(CMAKE_TOOLCHAIN_FILE MATCHES "Emscripten\\.cmake$" AND NOT EMSCRIPTEN)
  message(FATAL_ERROR
    "The Emscripten toolchain was requested, but CMake configured a native compiler.\n"
    "In VS Code, run 'CMake: Select a Kit' and select '[Unspecified]', then\n"
    "delete build/emscripten and configure again.\n"
    "C compiler: ${CMAKE_C_COMPILER}\n"
    "C++ compiler: ${CMAKE_CXX_COMPILER}\n"
    "Toolchain: ${CMAKE_TOOLCHAIN_FILE}"
  )
endif()

# ============================================================
# Global settings
# ============================================================

set(CMAKE_CONFIGURATION_TYPES
    Debug
    Release
    Sanitize
    CACHE STRING "Available build configurations" FORCE
)

add_compile_options(
  # Clang/GCC common
  $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:C,Clang,AppleClang,GNU>>:-O1>
  $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>>:-O1>
  $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:C,Clang,AppleClang,GNU>>:-g>
  $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>>:-g>
  $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:C,Clang,AppleClang,GNU>>:-fno-omit-frame-pointer>
  $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:C,Clang,AppleClang,GNU>>:-fno-sanitize-merge>
  $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>>:-fno-omit-frame-pointer>
  $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>>:-fno-sanitize-merge>

  # AppleClang: leak sanitizer is not generally available
  $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:C,AppleClang>>:-fsanitize=address,undefined>
  $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:CXX,AppleClang>>:-fsanitize=address,undefined>

  # Clang/GNU: include leak sanitizer
  $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:C,Clang,GNU>>:-fsanitize=address,undefined,leak>
  $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>>:-fsanitize=address,undefined,leak>

  # MSVC sanitizer
  $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:C,MSVC>>:/fsanitize=address>
  $<$<AND:$<CONFIG:Sanitize>,$<COMPILE_LANG_AND_ID:CXX,MSVC>>:/fsanitize=address>
)

add_link_options(
  # AppleClang
  $<$<AND:$<CONFIG:Sanitize>,$<C_COMPILER_ID:AppleClang>>:-fsanitize=address,undefined>
  $<$<AND:$<CONFIG:Sanitize>,$<CXX_COMPILER_ID:AppleClang>>:-fsanitize=address,undefined>

  # Clang/GNU
  $<$<AND:$<CONFIG:Sanitize>,$<C_COMPILER_ID:Clang,GNU>>:-fsanitize=address,undefined,leak>
  $<$<AND:$<CONFIG:Sanitize>,$<CXX_COMPILER_ID:Clang,GNU>>:-fsanitize=address,undefined,leak>

  # MSVC
  $<$<AND:$<CONFIG:Sanitize>,$<C_COMPILER_ID:MSVC>>:/fsanitize=address>
  $<$<AND:$<CONFIG:Sanitize>,$<CXX_COMPILER_ID:MSVC>>:/fsanitize=address>
)

if(EMSCRIPTEN)
  add_compile_options(-pthread)
  add_link_options(-pthread)
endif()

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

  # Disable exceptions in the MSVC STL
  add_compile_definitions(_HAS_EXCEPTIONS=0)

  # Enable MSVC STL hardening
  add_compile_definitions($<$<OR:$<CONFIG:Debug>,$<CONFIG:Sanitize>>:_MSVC_STL_HARDENING=2> $<$<OR:$<CONFIG:Debug>,$<CONFIG:Sanitize>>:_ITERATOR_DEBUG_LEVEL=2>)
endif()

add_library(project_warnings INTERFACE)

target_compile_options(project_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU>:
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wformat=2
        -Wundef
        -Wcast-align
        -Wcast-qual
        -Wold-style-cast
        -Woverloaded-virtual
        -Wnon-virtual-dtor
        -Wnull-dereference
    >

    $<$<CXX_COMPILER_ID:Clang,AppleClang>:
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow-all
        -Wformat=2
        -Wundef
        -Wcast-align
        -Wcast-qual
        -Wold-style-cast
        -Woverloaded-virtual
        -Wnon-virtual-dtor
        -Wnull-dereference
        -Wextra-semi
        -Wimplicit-fallthrough
        -Wunreachable-code
        -Wno-c2y-extensions
        -Wno-missing-designated-field-initializers
#        -Werror=pass-failed
    >

    $<$<CXX_COMPILER_ID:MSVC>:
        /W4
        /permissive-
    >
)

add_library(project_options INTERFACE)

target_compile_options(project_options INTERFACE
  # Disable RTTI and exceptions.
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fno-rtti -fno-exceptions>

  # Disable RTTI and exceptions.
  $<$<CXX_COMPILER_ID:MSVC>:/GR- /EHs-c- /Zc:preprocessor>

  # Loop vectorization optimization report for Clang/GCC/MSVC in Release builds
  #"$<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:C,Clang,AppleClang>>:-Rpass=(loop-vectorize|slp-vectorizer)>"
  #"$<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>>:-Rpass=(loop-vectorize|slp-vectorizer)>"
  #"$<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:C,Clang,AppleClang>>:-Rpass-missed=(loop-vectorize|slp-vectorizer)>"
  #"$<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>>:-Rpass-missed=(loop-vectorize|slp-vectorizer)>"
  #"$<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:C,Clang,AppleClang>>:-Rpass-analysis=(loop-vectorize|slp-vectorizer)>"
  #"$<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>>:-Rpass-analysis=(loop-vectorize|slp-vectorizer)>"
  $<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:C,GNU>>:-fopt-info-vec>
  $<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,GNU>>:-fopt-info-vec>
  $<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:C,MSVC>>:/Qvec-report:1>
  $<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,MSVC>>:/Qvec-report:1>
)

# Helper function to set compiler flags for a target
function(compiler_flags target_name)
  target_link_libraries(${target_name} PRIVATE project_warnings project_options)
  set_property(TARGET ${target_name} PROPERTY COMPILE_WARNING_AS_ERROR ON)
endfunction()

# ============================================================
# Dependencies (SDL3, spdlog, stb, etc.)
# ============================================================

# --- Google Test
# --- add google test before anything that might use it (like Dawn)
add_subdirectory(thirdparty/googletest)

# --- SDL3
set(SDL_SHARED OFF CACHE BOOL "Build shared SDL library" FORCE)
set(SDL_PTHREADS ON CACHE BOOL "Build SDL with pthreads" FORCE) # Required to use AsyncIO on emscripten
set(SDL_STATIC ON CACHE BOOL "Build static SDL library" FORCE)
set(SDL_TEST OFF CACHE BOOL "Build SDL test programs" FORCE)
set(SDL_TESTS OFF CACHE BOOL "Build SDL tests" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL test library" FORCE)

add_subdirectory(thirdparty/SDL EXCLUDE_FROM_ALL SYSTEM)

# --- Dawn
set(DAWN_FETCH_DEPENDENCIES ON CACHE BOOL "Fetch Dawn dependencies" FORCE)
set(DAWN_BUILD_SAMPLES OFF CACHE BOOL "Disable samples" FORCE)
set(DAWN_USE_GLFW OFF CACHE BOOL "Disable GLFW" FORCE)
set(DAWN_BUILD_TESTS OFF CACHE BOOL "Disable tests" FORCE)
set(DAWN_ENABLE_INSTALL OFF CACHE BOOL "Disable Dawn install" FORCE)
set(DAWN_BUILD_MONOLITHIC_LIBRARY STATIC CACHE STRING "Build Dawn monolithic library as static" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "Use static MSVC runtime in GLFW" FORCE)
set(protobuf_MSVC_STATIC_RUNTIME ON CACHE BOOL "Use static MSVC runtime in protobuf" FORCE)
set(ABSL_MSVC_STATIC_RUNTIME ON CACHE BOOL "Use static MSVC runtime in abseil" FORCE)
set(TINT_ENABLE_INSTALL OFF CACHE BOOL "Disable Tint install" FORCE)
set(TINT_BUILD_TESTS OFF CACHE BOOL "Disable Tint tests" FORCE)
if(WIN32)
  set(DAWN_USE_WAYLAND OFF CACHE BOOL "Enable Wayland support" FORCE)
  set(DAWN_USE_X11 OFF CACHE BOOL "Enable X11 support" FORCE)
  set(DAWN_USE_WINDOWS_UI ON CACHE BOOL "Enable Windows UI support" FORCE)
  set(DAWN_ENABLE_METAL OFF CACHE BOOL "Enable Metal backend" FORCE)
elseif(APPLE)
  set(DAWN_USE_WAYLAND OFF CACHE BOOL "Enable Wayland support" FORCE)
  set(DAWN_USE_X11 OFF CACHE BOOL "Enable X11 support" FORCE)
  set(DAWN_USE_WINDOWS_UI OFF CACHE BOOL "Enable Windows UI support" FORCE)
  set(DAWN_ENABLE_METAL ON CACHE BOOL "Enable Metal backend" FORCE)
elseif(EMSCRIPTEN)
  set(DAWN_USE_WAYLAND OFF CACHE BOOL "Enable Wayland support" FORCE)
  set(DAWN_USE_X11 OFF CACHE BOOL "Enable X11 support" FORCE)
  set(DAWN_USE_WINDOWS_UI OFF CACHE BOOL "Enable Windows UI support" FORCE)
  set(DAWN_ENABLE_METAL OFF CACHE BOOL "Enable Metal backend" FORCE)
  set(DAWN_ENABLE_WEBGPU_ON_WEBGPU ON CACHE BOOL "Enable WebGPU backend" FORCE)
elseif(UNIX)
  set(DAWN_USE_WAYLAND ON CACHE BOOL "Enable Wayland support" FORCE)
  set(DAWN_USE_X11 ON CACHE BOOL "Enable X11 support" FORCE)
  set(DAWN_USE_WINDOWS_UI OFF CACHE BOOL "Enable Windows UI support" FORCE)
  set(DAWN_ENABLE_METAL OFF CACHE BOOL "Enable Metal backend" FORCE)
endif()

add_subdirectory(thirdparty/dawn EXCLUDE_FROM_ALL SYSTEM)

# --- spdlog
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "Build as shared library" FORCE)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "Use external fmt library" FORCE)
#set(SPDLOG_USE_STD_FORMAT ON CACHE BOOL "Use C++20 std::format" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Disable tests" FORCE)
set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "Disable examples" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "Disable install target" FORCE)
set(SPDLOG_HEADER_ONLY OFF CACHE BOOL "Disable header-only mode" FORCE)

add_subdirectory(thirdparty/spdlog EXCLUDE_FROM_ALL SYSTEM)

# --- lua
add_library(lua STATIC
  thirdparty/lua/lapi.c
  thirdparty/lua/lauxlib.c
  thirdparty/lua/lbaselib.c
  thirdparty/lua/lcode.c
  thirdparty/lua/lcorolib.c
  thirdparty/lua/lctype.c
  thirdparty/lua/ldblib.c
  thirdparty/lua/ldebug.c
  thirdparty/lua/ldo.c
  thirdparty/lua/ldump.c
  thirdparty/lua/lfunc.c
  thirdparty/lua/lgc.c
  thirdparty/lua/linit.c
  thirdparty/lua/liolib.c
  thirdparty/lua/llex.c
  thirdparty/lua/lmathlib.c
  thirdparty/lua/lmem.c
  thirdparty/lua/loadlib.c
  thirdparty/lua/lobject.c
  thirdparty/lua/lopcodes.c
  thirdparty/lua/loslib.c
  thirdparty/lua/lparser.c
  thirdparty/lua/lstate.c
  thirdparty/lua/lstring.c
  thirdparty/lua/lstrlib.c
  thirdparty/lua/ltable.c
  thirdparty/lua/ltablib.c
  thirdparty/lua/ltests.c
  thirdparty/lua/ltm.c
  thirdparty/lua/lundump.c
  thirdparty/lua/lutf8lib.c
  thirdparty/lua/lvm.c
  thirdparty/lua/lzio.c
)

target_include_directories(lua SYSTEM PUBLIC
  thirdparty/lua
)

if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
  target_link_libraries(lua PRIVATE m dl)
endif()

# --- imgui
add_library(imgui STATIC
    thirdparty/imgui/imgui.cpp
    thirdparty/imgui/imgui_draw.cpp
    thirdparty/imgui/imgui_tables.cpp
    thirdparty/imgui/imgui_widgets.cpp
    thirdparty/imgui/backends/imgui_impl_sdl3.cpp
    thirdparty/imgui/backends/imgui_impl_wgpu.cpp
)

if(APPLE)
  set_source_files_properties(thirdparty/imgui/backends/imgui_impl_wgpu.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
endif()

target_include_directories(imgui SYSTEM PUBLIC
    thirdparty/imgui
    thirdparty/imgui/backends
)

if(EMSCRIPTEN)
  target_link_libraries(imgui PUBLIC SDL3::SDL3 emdawnwebgpu_cpp)
else()
  target_link_libraries(imgui PUBLIC SDL3::SDL3 dawn_headers)
endif()
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_WEBGPU_BACKEND_DAWN)

# --- box3d
add_subdirectory(thirdparty/box3d EXCLUDE_FROM_ALL SYSTEM)

# --- libktx
set(LIBKTX_FEATURE_GL_UPLOAD OFF CACHE BOOL "Enable OpenGL upload" FORCE)
set(LIBKTX_FEATURE_VK_UPLOAD OFF CACHE BOOL "Enable Vulkan upload" FORCE)
add_subdirectory(thirdparty/KTX-Software/lib EXCLUDE_FROM_ALL SYSTEM)

# ============================================================
# Enable clang-tidy only after all thirdparty targets exist
# ============================================================

option(ENABLE_CLANG_TIDY "Enable clang-tidy analysis" OFF)

if(ENABLE_CLANG_TIDY)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    find_program(CLANG_TIDY_EXE NAMES clang-tidy-22 clang-tidy)
    if(CLANG_TIDY_EXE)
      set(CMAKE_CXX_CLANG_TIDY
        ${CLANG_TIDY_EXE}
        --use-color
        "--exclude-header-filter=(thirdparty|build)[/\\]*")
    else()
      message(FATAL_ERROR "ENABLE_CLANG_TIDY is ON and the compiler is Clang, but clang-tidy was not found")
    endif()
  else()
    message(STATUS "ENABLE_CLANG_TIDY is ON, but the compiler is not Clang; skipping clang-tidy")
  endif()
endif()

# ============================================================
# Shaders / Assets
# ============================================================
set(RESOURCE_ROOT "${CMAKE_SOURCE_DIR}/resources")
add_subdirectory(src/shaders)

add_custom_target(copy_images
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          "${CMAKE_SOURCE_DIR}/images" "${RESOURCE_ROOT}/images"
  COMMENT "Copying image files"
  VERBATIM
)

add_custom_target(copy_sponza
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          "${CMAKE_SOURCE_DIR}/assets/main_sponza" "${RESOURCE_ROOT}/main_sponza"
  COMMENT "Copying Sponza files"
  VERBATIM
)

# ============================================================
# Library: ssg
# ============================================================
set(SSG_SOURCES
  src/AssertHelper.cpp
  src/BoundingVolumes.cpp
  src/Camera.cpp
  src/Cullers.cpp
  src/cgltf.cpp
  src/CoopTask.cpp
  src/DevUi.cpp
  src/FileFetcher.cpp
  src/FilePath.cpp
  src/GltfLoader.cpp
  src/GpuColorPass.cpp
  src/GpuCompositorPass.cpp
  src/GpuTransformPass.cpp
  src/GpuHelper.cpp
  src/ImGuiRenderer.cpp
  src/InputMapper.cpp
  src/Level.cpp
  src/Log.cpp
  src/PerfMetrics.cpp
  src/ResourceBundle.cpp
  src/Scene.cpp
  src/SceneTypes.cpp
  src/ShaderFetcher.cpp
  src/ShapeDefs.cpp
  src/Shell.cpp
  src/stb_image.cpp
  src/System.cpp
  src/TextureFetcher.cpp
  src/TextureHelper.cpp
  src/ThreadPool.cpp
  src/Timer.cpp
  src/VecMath.cpp
  src/View.cpp
)

set(SSG_HEADERS
  src/AssertHelper.h
  src/BoundingVolumes.h
  src/Camera.h
  src/Containers.h
  src/CoopTask.h
  src/Cullers.h
  src/Defer.h
  src/DevUi.h
  src/FileFetcher.h
  src/FilePath.h
  src/GltfLoader.h
  src/GpuColorPass.h
  src/GpuCompositorPass.h
  src/GpuTransformPass.h
  src/GpuHelper.h
  src/GpuTypes.h
  src/ImGuiRenderer.h
  src/inlist.h
  src/InplaceString.h
  src/InputMapper.h
  src/Level.h
  src/Log.h
  src/PhysicsTypes.h
  src/PerfMetrics.h
  src/ResourceBundle.h
  src/Result.h
  src/Scene.h
  src/SceneTypes.h
  src/SemanticIdentifier.h
  src/shaders/ShaderInterop.h
  src/ShaderFetcher.h
  src/ShapeDefs.h
  src/Shell.h
  src/System.h
  src/TextureFetcher.h
  src/TextureHelper.h
  src/ThreadPool.h
  src/Timer.h
  src/VecMath.h
  src/Vertex.h
  src/View.h
)

add_library(ssg)
add_library(ssg::ssg ALIAS ssg)
target_sources(ssg
  PRIVATE ${SSG_SOURCES}
  PUBLIC FILE_SET HEADERS BASE_DIRS src FILES ${SSG_HEADERS}
)

# Keep third-party implementations out of clang-tidy noise.
set_source_files_properties(src/cgltf.cpp PROPERTIES SKIP_LINTING ON)
set_source_files_properties(src/stb_image.cpp PROPERTIES SKIP_LINTING ON)

if(EMSCRIPTEN)
  target_link_libraries(ssg PUBLIC emdawnwebgpu_cpp)
else()
  target_link_libraries(ssg PUBLIC webgpu_dawn)
endif()
target_link_libraries(ssg PUBLIC spdlog::spdlog SDL3::SDL3 imgui lua box3d::box3d)
target_include_directories(ssg PUBLIC src)
target_include_directories(ssg SYSTEM PRIVATE thirdparty/cgltf thirdparty/stb)

compiler_flags(ssg)

# ============================================================
# Helper functions for executables
# ============================================================
set(SIGN_EXECUTABLE ${APPLE})

function(sign_executable target_name)
  if(SIGN_EXECUTABLE)
    set(entitlements_file "${CMAKE_CURRENT_SOURCE_DIR}/samples/debug.entitlements")
    set_property(TARGET ${target_name} APPEND PROPERTY LINK_DEPENDS
                 "$<$<CONFIG:Debug>:${entitlements_file}>")
    add_custom_command(
      TARGET ${target_name} POST_BUILD
      COMMAND "$<OUTPUT_CONFIG:$<$<CONFIG:Debug>:/usr/bin/codesign;--verbose;--force;--sign;-;--entitlements;${entitlements_file};$<TARGET_FILE:${target_name}>>>"
      COMMAND_EXPAND_LISTS
      VERBATIM
    )
  endif()
endfunction()

function(add_project_executable target_name)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES;DEPENDENCIES" ${ARGN})

  add_executable(${target_name} ${ARG_SOURCES})
  if(ARG_LIBRARIES)
    target_link_libraries(${target_name} PRIVATE ${ARG_LIBRARIES})
  endif()
  if(ARG_DEPENDENCIES)
    add_dependencies(${target_name} ${ARG_DEPENDENCIES})
  endif()

  compiler_flags(${target_name})
  sign_executable(${target_name})
endfunction()

function(add_sample_executable target_name source_file)
  add_project_executable(${target_name}
    SOURCES ${source_file} samples/CameraActor.cpp
    LIBRARIES ssg::ssg
    DEPENDENCIES shaders)
endfunction()

function(add_tool_executable target_name source_file)
  add_project_executable(${target_name}
    SOURCES ${source_file}
    LIBRARIES ssg::ssg)
endfunction()

# ============================================================
# Tool: Cook
# ============================================================
add_tool_executable(Cook tools/Cook/Cook.cpp)
target_link_libraries(Cook PRIVATE ktx)
target_include_directories(Cook SYSTEM PRIVATE thirdparty/stb)

# ============================================================
# Sample: Triangle
# ============================================================
add_sample_executable(Triangle samples/Triangle.cpp)
add_dependencies(Triangle copy_images)
if(EMSCRIPTEN)
  set_target_properties(Triangle PROPERTIES SUFFIX ".html")

  set(browser_shell "${CMAKE_CURRENT_SOURCE_DIR}/samples/browser-shell.html")
  file(GLOB_RECURSE _image_sources CONFIGURE_DEPENDS LIST_DIRECTORIES false
       "${CMAKE_SOURCE_DIR}/images/*")
  set_property(TARGET Triangle APPEND PROPERTY LINK_DEPENDS
               "${browser_shell}" ${SSG_WGSL_SOURCES} ${_image_sources})

  target_link_options(Triangle PRIVATE
    "--shell-file"
    "${browser_shell}"
    "-sPTHREAD_POOL_SIZE=8"
    "-sALLOW_MEMORY_GROWTH=1"
#    "-sJSPI"
    "--preload-file=${RESOURCE_ROOT}/images@images"
    "--preload-file=${RESOURCE_ROOT}/shaders@shaders"
  )
endif()

# ============================================================
# Sample: Translucent
# ============================================================
add_sample_executable(Translucent samples/Translucent.cpp)
add_dependencies(Translucent copy_images)
if(EMSCRIPTEN)
  set_target_properties(Translucent PROPERTIES SUFFIX ".html")

  set(browser_shell "${CMAKE_CURRENT_SOURCE_DIR}/samples/browser-shell.html")
  file(GLOB_RECURSE _image_sources CONFIGURE_DEPENDS LIST_DIRECTORIES false
       "${CMAKE_SOURCE_DIR}/images/*")
  set_property(TARGET Translucent APPEND PROPERTY LINK_DEPENDS
               "${browser_shell}" ${SSG_WGSL_SOURCES} ${_image_sources})

  target_link_options(Translucent PRIVATE
    "--shell-file"
    "${browser_shell}"
    "-sPTHREAD_POOL_SIZE=8"
    "-sALLOW_MEMORY_GROWTH=1"
#    "-sJSPI"
    "--preload-file=${RESOURCE_ROOT}/images@images"
    "--preload-file=${RESOURCE_ROOT}/shaders@shaders"
  )
endif()

# ============================================================
# Sample: Viewer
# ============================================================
add_sample_executable(Viewer samples/Viewer.cpp)
add_dependencies(Viewer copy_sponza)

# ============================================================
# Sample: Orbit
# ============================================================
add_sample_executable(Orbit samples/Orbit.cpp)

# ============================================================
# Executable: Tests
# ============================================================
add_executable(Tests
  "tests/BoundingBox.unit.cpp"
  "tests/BoundingSphere.unit.cpp"
  "tests/Camera.unit.cpp"
  "tests/Containers.unit.cpp"
  "tests/Defer.unit.cpp"
  "tests/FilePath.unit.cpp"
  "tests/FixedString.unit.cpp"
  "tests/inlist.unit.cpp"
  "tests/Mat44.unit.cpp"
  "tests/Quat.unit.cpp"
  "tests/Radians.unit.cpp"
  "tests/ShapeDefs.unit.cpp"
  "tests/TrsTransform.unit.cpp"
  "tests/Vec2.unit.cpp"
  "tests/Vec3.unit.cpp"
  "tests/Vec4.unit.cpp")

target_link_libraries(Tests PRIVATE gtest_main ssg::ssg)
#add_dependencies(RunAllTests shaders copy_images)

compiler_flags(Tests)
sign_executable(Tests)

# Enable testing and register tests
# In Visual Studio tests should show up in Test Explorer.
# In VS Code install the CMake Test Explorer extension and open the testing view.
include(GoogleTest)
enable_testing()

if(NOT EMSCRIPTEN)
  gtest_discover_tests(Tests)
endif()

