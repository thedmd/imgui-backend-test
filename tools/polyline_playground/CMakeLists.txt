cmake_minimum_required(VERSION 3.5)
project(polyline_playground VERSION 0.1.0)

set(ENABLE_TRACY NO)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    *.cpp
    *.h
    *.hpp
)
add_executable(polyline_playground
    ${SOURCES}
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_win32.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_win32.h
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/${BACKENDS_DIR}/imgui_impl_dx11.h
)
if (HAVE_IMGUI_PCH)
    target_precompile_headers(polyline_playground REUSE_FROM imgui)
endif()
target_include_directories(polyline_playground PRIVATE
    third_party/Clipper2Lib/include
    third_party/earcut/include
    third_party/poly2d/include
    third_party/imgui_canvas/include
)
target_link_libraries(polyline_playground PRIVATE imgui d3d11 d3dcompiler)
flatten_filters(polyline_playground)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
set_target_properties(polyline_playground PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${EXAMPLES_WORKING_DIR}
    VS_DEBUGGER_ENVIRONMENT "${CMAKE_VS_DEBUGGER_ENVIRONMENT}"
    VS_DPI_AWARE "PerMonitor"
)
set_property(TARGET polyline_playground PROPERTY FOLDER "tools")

target_compile_definitions(polyline_playground PRIVATE ENABLE_TRACY=$<IF:$<BOOL:${ENABLE_TRACY}>,1,0>)

if (ENABLE_TRACY)
    function(register_auto_filter)
    endfunction()
    function(spark_win_add_post_build_copy_target_to_bin)
    endfunction()
    add_subdirectory(V:/Work/Spark/ThirdParty/tracy ${CMAKE_CURRENT_BINARY_DIR}/tracy)
    target_link_libraries(polyline_playground PRIVATE tracy-client)
endif()