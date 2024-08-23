cmake_minimum_required(VERSION 3.5)
project(imgui-backend-test VERSION 0.1.0)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 23)

if (FALSE)
    set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} /fsanitize=address")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /fsanitize=address")
    set(CMAKE_VS_DEBUGGER_ENVIRONMENT "PATH=%PATH%;$(ExecutablePath)")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

find_package(glfw)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/../imgui-dev/imgui)
set(IMGUI_EXT_DIR ${CMAKE_SOURCE_DIR}/imgui-ext)
set(IMGUI_NODE_EDITOR_DIR ${CMAKE_SOURCE_DIR}/../imgui-node-editor)
set(MINIAUDIO ${CMAKE_SOURCE_DIR}/../miniaudio)
set(GL3W_DIR ${CMAKE_SOURCE_DIR}/third-party/gl3w)
set(GLUT_DIR ${CMAKE_SOURCE_DIR}/third-party/freeglut-3.0.0)
set(SDL2_DIR ${CMAKE_SOURCE_DIR}/third-party/SDL2-2.0.14)
set(ALLEGRO5_DIR ${CMAKE_SOURCE_DIR}/third-party/allegro-5.2.6)
if (NOT DEFINED $ENV{VULKAN_SDK})
    set(ENV{VULKAN_SDK} ${CMAKE_SOURCE_DIR}/third-party/vulkan-1.2.162.1)
endif()
find_package(Vulkan)
set(FREETYPE_DIR ${CMAKE_SOURCE_DIR}/third-party/freetype-2.10.4)

set(BACKENDS_DIR backends)
#set(BACKENDS_DIR examples)

# Examples has font paths hardcoded to '../../' so we need to start
# apropriate directory.
set(EXAMPLES_WORKING_DIR ${IMGUI_DIR}/examples/example_null)

function(FLATTEN_FILTERS target)
    get_target_property(sources ${target} SOURCES)
    source_group("" FILES ${sources})
endfunction()

add_library(freetype2 SHARED IMPORTED)
target_include_directories(freetype2 INTERFACE ${FREETYPE_DIR}/include)
set_target_properties(freetype2 PROPERTIES IMPORTED_IMPLIB ${FREETYPE_DIR}/win64/freetype.lib)

file(GLOB IMGUI_SOURCES CONFIGURE_DEPENDS ${IMGUI_DIR}/*.cpp ${IMGUI_DIR}/*.h)
add_library(imgui STATIC
    ${IMGUI_SOURCES}
    ${IMGUI_DIR}/misc/debuggers/imgui.natvis
    ${IMGUI_DIR}/misc/freetype/imgui_freetype.cpp
    ${IMGUI_DIR}/misc/freetype/imgui_freetype.h
)
if (FALSE)
    target_precompile_headers(imgui PRIVATE ${IMGUI_DIR}/imgui.h)
    set(HAVE_IMGUI_PCH YES)
else()
    set(HAVE_IMGUI_PCH NO)
endif()
target_compile_definitions(imgui PUBLIC
    #IMGUI_DEFINE_MATH_OPERATORS
)
if (TRUE)
    target_sources(imgui PRIVATE
        ${IMGUI_EXT_DIR}/backent-test-imconfig.h
        ${IMGUI_EXT_DIR}/imgui_user.cpp
        ${IMGUI_EXT_DIR}/imgui_user.h
    )
    target_compile_definitions(imgui PUBLIC
        IMGUI_USER_CONFIG="backent-test-imconfig.h"
        IMGUI_INCLUDE_IMGUI_USER_H
    )
    target_include_directories(imgui PUBLIC
        ${IMGUI_EXT_DIR}
    )
endif()
target_compile_definitions(imgui PUBLIC
    #IMGUI_ENABLE_FREETYPE
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    #IMGUI_DISABLE_OBSOLETE_KEYIO
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/${BACKENDS_DIR}
)
target_link_libraries(imgui PRIVATE freetype2)
flatten_filters(imgui)
set_property(TARGET imgui PROPERTY FOLDER "lib")
add_custom_command(TARGET imgui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ARGS ${FREETYPE_DIR}/win64/freetype.dll $<TARGET_FILE_DIR:imgui>/
)

add_library(imgui_node_editor STATIC
    ${IMGUI_NODE_EDITOR_DIR}/crude_json.cpp
    ${IMGUI_NODE_EDITOR_DIR}/imgui_canvas.cpp
    ${IMGUI_NODE_EDITOR_DIR}/imgui_node_editor.cpp
    ${IMGUI_NODE_EDITOR_DIR}/imgui_node_editor_api.cpp
    ${IMGUI_NODE_EDITOR_DIR}/crude_json.h
    ${IMGUI_NODE_EDITOR_DIR}/imgui_bezier_math.h
    ${IMGUI_NODE_EDITOR_DIR}/imgui_canvas.h
    ${IMGUI_NODE_EDITOR_DIR}/imgui_extra_math.h
    ${IMGUI_NODE_EDITOR_DIR}/imgui_node_editor.h
    ${IMGUI_NODE_EDITOR_DIR}/imgui_node_editor_internal.h
    ${IMGUI_NODE_EDITOR_DIR}/imgui_bezier_math.inl
    ${IMGUI_NODE_EDITOR_DIR}/imgui_extra_math.inl
    ${IMGUI_NODE_EDITOR_DIR}/imgui_node_editor_internal.inl
)
target_compile_definitions(imgui_node_editor INTERFACE IMGUI_HAS_NODE_EDITOR)
target_include_directories(imgui_node_editor PUBLIC
    ${IMGUI_NODE_EDITOR_DIR}
)
target_link_libraries(imgui_node_editor PRIVATE imgui)
flatten_filters(imgui_node_editor)
set_property(TARGET imgui_node_editor PROPERTY FOLDER "lib")

add_library(gl3w STATIC
    ${GL3W_DIR}/include/GL/gl3w.h
    ${GL3W_DIR}/include/GL/glcorearb.h
    ${GL3W_DIR}/src/gl3w.c
)
target_include_directories(gl3w PUBLIC ${GL3W_DIR}/include)
target_link_libraries(gl3w PRIVATE opengl32)
flatten_filters(gl3w)
set_property(TARGET gl3w PROPERTY FOLDER "lib")


add_library(miniaudio STATIC
    ${MINIAUDIO}/miniaudio.h
    #${MINIAUDIO}/research/miniaudio_engine.h
    ${CMAKE_CURRENT_BINARY_DIR}/miniaudio/miniaudio.c
)
file(GENERATE
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/miniaudio/miniaudio.c
    CONTENT [=[
#define MA_DEBUG_OUTPUT
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
//#include "miniaudio_engine.h"
]=]
)
target_include_directories(miniaudio
    PUBLIC
        ${MINIAUDIO}
        ${MINIAUDIO}/research
)
flatten_filters(miniaudio)
set_property(TARGET miniaudio PROPERTY FOLDER "lib")



add_executable(example_glut_opengl2
    ${IMGUI_DIR}/examples/example_glut_opengl2/main.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_glut.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_glut.h
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_opengl2.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_opengl2.h
)
if (HAVE_IMGUI_PCH)
    target_precompile_headers(example_glut_opengl2 REUSE_FROM imgui)
endif()
target_include_directories(example_glut_opengl2 PRIVATE ${GLUT_DIR}/include)
target_link_directories(example_glut_opengl2 PRIVATE ${GLUT_DIR}/lib/x64)
target_link_libraries(example_glut_opengl2 PRIVATE imgui freeglut)
flatten_filters(example_glut_opengl2)
set_target_properties(example_glut_opengl2 PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${EXAMPLES_WORKING_DIR}
    VS_DEBUGGER_ENVIRONMENT "${CMAKE_VS_DEBUGGER_ENVIRONMENT}"
    VS_DPI_AWARE "PerMonitor"
)
add_custom_command(TARGET example_glut_opengl2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ARGS ${GLUT_DIR}/bin/x64/freeglut.dll $<TARGET_FILE_DIR:example_glut_opengl2>/
)


add_executable(example_sdl2_opengl2
    ${IMGUI_DIR}/examples/example_sdl2_opengl2/main.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_sdl2.h
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_opengl2.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_opengl2.h
)
if (HAVE_IMGUI_PCH)
    target_precompile_headers(example_sdl2_opengl2 REUSE_FROM imgui)
endif()
target_include_directories(example_sdl2_opengl2 PRIVATE ${SDL2_DIR}/include)
target_link_directories(example_sdl2_opengl2 PRIVATE ${SDL2_DIR}/lib/x64)
target_link_libraries(example_sdl2_opengl2 PRIVATE imgui SDL2 SDL2main opengl32)
flatten_filters(example_sdl2_opengl2)
set_target_properties(example_sdl2_opengl2 PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${EXAMPLES_WORKING_DIR}
    VS_DEBUGGER_ENVIRONMENT "${CMAKE_VS_DEBUGGER_ENVIRONMENT}"
    VS_DPI_AWARE "PerMonitor"
)
add_custom_command(TARGET example_sdl2_opengl2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ARGS ${SDL2_DIR}/lib/x64/SDL2.dll $<TARGET_FILE_DIR:example_sdl2_opengl2>/
)


add_executable(example_sdl2_opengl3
    ${IMGUI_DIR}/examples/example_sdl2_opengl3/main.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_sdl2.h
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_opengl3.h
)
if (HAVE_IMGUI_PCH)
    target_precompile_headers(example_sdl2_opengl3 REUSE_FROM imgui)
endif()
target_include_directories(example_sdl2_opengl3 PRIVATE ${SDL2_DIR}/include)
target_link_directories(example_sdl2_opengl3 PRIVATE ${SDL2_DIR}/lib/x64)
target_link_libraries(example_sdl2_opengl3 PRIVATE imgui SDL2 SDL2main gl3w opengl32)
flatten_filters(example_sdl2_opengl3)
set_target_properties(example_sdl2_opengl3 PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${EXAMPLES_WORKING_DIR}
    VS_DEBUGGER_ENVIRONMENT "${CMAKE_VS_DEBUGGER_ENVIRONMENT}"
    VS_DPI_AWARE "PerMonitor"
)
add_custom_command(TARGET example_sdl2_opengl3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ARGS ${SDL2_DIR}/lib/x64/SDL2.dll $<TARGET_FILE_DIR:example_sdl2_opengl3>/
)


if (Vulkan_FOUND)
add_executable(example_sdl2_vulkan
    ${IMGUI_DIR}/examples/example_sdl2_vulkan/main.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_sdl2.h
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_vulkan.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_vulkan.h
)
if (HAVE_IMGUI_PCH)
    target_precompile_headers(example_sdl2_vulkan REUSE_FROM imgui)
endif()
target_include_directories(example_sdl2_vulkan PRIVATE ${SDL2_DIR}/include)
target_link_directories(example_sdl2_vulkan PRIVATE ${SDL2_DIR}/lib/x64)
target_link_libraries(example_sdl2_vulkan PRIVATE imgui SDL2 SDL2main Vulkan::Vulkan)
flatten_filters(example_sdl2_vulkan)
set_target_properties(example_sdl2_vulkan PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${EXAMPLES_WORKING_DIR}
    VS_DEBUGGER_ENVIRONMENT "${CMAKE_VS_DEBUGGER_ENVIRONMENT}"
    VS_DPI_AWARE "PerMonitor"
)
add_custom_command(TARGET example_sdl2_vulkan POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ARGS ${SDL2_DIR}/lib/x64/SDL2.dll $<TARGET_FILE_DIR:example_sdl2_vulkan>/
    #COMMAND ${CMAKE_COMMAND} -E copy_if_different ARGS ${VULKAN_DIR}/bin/vulkan-1.dll $<TARGET_FILE_DIR:example_sdl2_vulkan>/
    #COMMAND ${CMAKE_COMMAND} -E copy_if_different ARGS ${VULKAN_DIR}/bin/VkLayer_khronos_validation.dll $<TARGET_FILE_DIR:example_sdl2_vulkan>/
    #COMMAND ${CMAKE_COMMAND} -E copy_if_different ARGS ${VULKAN_DIR}/bin/VkLayer_khronos_validation.json $<TARGET_FILE_DIR:example_sdl2_vulkan>/
)
endif()


add_executable(example_sdl2_directx11
    ${IMGUI_DIR}/examples/example_sdl2_directx11/main.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_sdl2.h
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_dx11.h
)
if (HAVE_IMGUI_PCH)
    target_precompile_headers(example_sdl2_directx11 REUSE_FROM imgui)
endif()
target_include_directories(example_sdl2_directx11 PRIVATE ${SDL2_DIR}/include)
target_link_directories(example_sdl2_directx11 PRIVATE ${SDL2_DIR}/lib/x64)
target_link_libraries(example_sdl2_directx11 PRIVATE imgui SDL2 SDL2main d3d11 d3dcompiler)
flatten_filters(example_sdl2_directx11)
set_target_properties(example_sdl2_directx11 PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${EXAMPLES_WORKING_DIR}
    VS_DEBUGGER_ENVIRONMENT "${CMAKE_VS_DEBUGGER_ENVIRONMENT}"
    VS_DPI_AWARE "PerMonitor"
)
add_custom_command(TARGET example_sdl2_directx11 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ARGS ${SDL2_DIR}/lib/x64/SDL2.dll $<TARGET_FILE_DIR:example_sdl2_directx11>/
)


add_executable(example_allegro5
    ${IMGUI_DIR}/examples/example_allegro5/main.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_allegro5.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_allegro5.h
)
target_include_directories(example_allegro5 PRIVATE
    ${IMGUI_DIR}/examples/example_allegro5
    ${ALLEGRO5_DIR}/include
)
if (HAVE_IMGUI_PCH)
    target_precompile_headers(example_allegro5 REUSE_FROM imgui)
endif()
target_compile_definitions(example_allegro5 PRIVATE ALLEGRO_UNSTABLE)
target_link_directories(example_allegro5 PRIVATE ${ALLEGRO5_DIR}/lib)
target_link_libraries(example_allegro5 PRIVATE imgui allegro allegro_main allegro_primitives)
flatten_filters(example_allegro5)
set_target_properties(example_allegro5 PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${EXAMPLES_WORKING_DIR}
    VS_DEBUGGER_ENVIRONMENT "${CMAKE_VS_DEBUGGER_ENVIRONMENT}"
    VS_DPI_AWARE "PerMonitor"
)
add_custom_command(TARGET example_allegro5 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ARGS ${ALLEGRO5_DIR}/bin/allegro-5.2.dll $<TARGET_FILE_DIR:example_allegro5>/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ARGS ${ALLEGRO5_DIR}/bin/allegro_main-5.2.dll $<TARGET_FILE_DIR:example_allegro5>/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ARGS ${ALLEGRO5_DIR}/bin/allegro_primitives-5.2.dll $<TARGET_FILE_DIR:example_allegro5>/
)

if (glfw_FOUND)
    add_executable(example_glfw_opengl3
        ${IMGUI_DIR}/examples/example_glfw_opengl3/main.cpp
        ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_glfw.h
        ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_opengl3.cpp
        ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_opengl3.h
    )
    if (HAVE_IMGUI_PCH)
        target_precompile_headers(example_glfw_opengl3 REUSE_FROM imgui)
    endif()
    target_include_directories(example_glfw_opengl3 PRIVATE ${GLFW_DIR}/include)
    target_link_directories(example_glfw_opengl3 PRIVATE ${GLFW_DIR}/lib-vc2019)
    target_link_libraries(example_glfw_opengl3 PRIVATE imgui glfw3dll gl3w)
    flatten_filters(example_glfw_opengl3)
    set_target_properties(example_glfw_opengl3 PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY ${EXAMPLES_WORKING_DIR}
        VS_DEBUGGER_ENVIRONMENT "${CMAKE_VS_DEBUGGER_ENVIRONMENT}"
        VS_DPI_AWARE "PerMonitor"
    )
    add_custom_command(TARGET example_glfw_opengl3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ARGS ${GLFW_DIR}/lib-vc2019/glfw3.dll $<TARGET_FILE_DIR:example_glfw_opengl3>/
    )
endif()


if (Vulkan_FOUND AND glfw_FOUND)
    add_executable(example_glfw_vulkan
        ${IMGUI_DIR}/examples/example_glfw_vulkan/main.cpp
        ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_glfw.h
        ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_vulkan.cpp
        ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_vulkan.h
    )
    if (HAVE_IMGUI_PCH)
        target_precompile_headers(example_glfw_vulkan REUSE_FROM imgui)
    endif()
    target_include_directories(example_glfw_vulkan PRIVATE ${GLFW_DIR}/include)
    target_link_directories(example_glfw_vulkan PRIVATE ${GLFW_DIR}/lib-vc2019)
    target_link_libraries(example_glfw_vulkan PRIVATE imgui glfw3dll Vulkan::Vulkan)
    flatten_filters(example_glfw_vulkan)
    set_target_properties(example_glfw_vulkan PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY ${EXAMPLES_WORKING_DIR}
        VS_DEBUGGER_ENVIRONMENT "${CMAKE_VS_DEBUGGER_ENVIRONMENT}"
        VS_DPI_AWARE "PerMonitor"
    )
    add_custom_command(TARGET example_glfw_vulkan POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ARGS ${GLFW_DIR}/lib-vc2019/glfw3.dll $<TARGET_FILE_DIR:example_glfw_vulkan>/
    )
endif()


add_executable(example_win32_directx9
    ${IMGUI_DIR}/examples/example_win32_directx9/main.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_win32.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_win32.h
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_dx9.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_dx9.h
)
if (HAVE_IMGUI_PCH)
    target_precompile_headers(example_win32_directx9 REUSE_FROM imgui)
endif()
target_link_libraries(example_win32_directx9 PRIVATE imgui d3d9 imgui_node_editor miniaudio)
flatten_filters(example_win32_directx9)
set_target_properties(example_win32_directx9 PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${EXAMPLES_WORKING_DIR}
    VS_DEBUGGER_ENVIRONMENT "${CMAKE_VS_DEBUGGER_ENVIRONMENT}"
    VS_DPI_AWARE "PerMonitor"
)


add_executable(example_win32_directx10
    ${IMGUI_DIR}/examples/example_win32_directx10/main.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_win32.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_win32.h
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_dx10.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_dx10.h
)
if (HAVE_IMGUI_PCH)
    target_precompile_headers(example_win32_directx10 REUSE_FROM imgui)
endif()
target_link_libraries(example_win32_directx10 PRIVATE imgui d3d10 d3dcompiler)
flatten_filters(example_win32_directx10)
set_target_properties(example_win32_directx10 PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${EXAMPLES_WORKING_DIR}
    VS_DEBUGGER_ENVIRONMENT "${CMAKE_VS_DEBUGGER_ENVIRONMENT}"
    VS_DPI_AWARE "PerMonitor"
)


add_executable(example_win32_directx11
    ${IMGUI_DIR}/examples/example_win32_directx11/main.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_win32.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_win32.h
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_dx11.h
)
if (HAVE_IMGUI_PCH)
    target_precompile_headers(example_win32_directx11 REUSE_FROM imgui)
endif()
target_link_libraries(example_win32_directx11 PRIVATE imgui d3d11 d3dcompiler)
flatten_filters(example_win32_directx11)
set_target_properties(example_win32_directx11 PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${EXAMPLES_WORKING_DIR}
    VS_DEBUGGER_ENVIRONMENT "${CMAKE_VS_DEBUGGER_ENVIRONMENT}"
    VS_DPI_AWARE "PerMonitor"
)


add_executable(example_win32_directx12
    ${IMGUI_DIR}/examples/example_win32_directx12/main.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_win32.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_win32.h
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_dx12.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_dx12.h
)
if (HAVE_IMGUI_PCH)
    target_precompile_headers(example_win32_directx12 REUSE_FROM imgui)
endif()
target_link_libraries(example_win32_directx12 PRIVATE imgui d3d12 d3dcompiler dxgi)
flatten_filters(example_win32_directx12)
set_target_properties(example_win32_directx12 PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${EXAMPLES_WORKING_DIR}
    VS_DEBUGGER_ENVIRONMENT "${CMAKE_VS_DEBUGGER_ENVIRONMENT}"
    VS_DPI_AWARE "PerMonitor"
)


add_executable(example_null
    ${IMGUI_DIR}/examples/example_null/main.cpp
)
if (HAVE_IMGUI_PCH)
    target_precompile_headers(example_null REUSE_FROM imgui)
endif()
target_link_libraries(example_null PRIVATE imgui)
flatten_filters(example_null)
set_target_properties(example_null PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${EXAMPLES_WORKING_DIR}
    VS_DEBUGGER_ENVIRONMENT "${CMAKE_VS_DEBUGGER_ENVIRONMENT}"
    VS_DPI_AWARE "PerMonitor"
)

add_subdirectory(tools)