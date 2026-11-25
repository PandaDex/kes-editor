cmake_minimum_required(VERSION 3.16)
project(Forge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

# Engine loaders are compiled in so previews and saved files match the game exactly.
set(KES_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../engine" CACHE PATH "KES engine source folder")
if(NOT EXISTS "${KES_ENGINE_DIR}/kmodel.h")
    message(FATAL_ERROR "KES engine sources not found at ${KES_ENGINE_DIR}; set KES_ENGINE_DIR")
endif()
add_library(forge_engine STATIC
    ${KES_ENGINE_DIR}/kmodel.cpp
    ${KES_ENGINE_DIR}/animation_pose.cpp
    ${KES_ENGINE_DIR}/animation_clip.cpp
    ${KES_ENGINE_DIR}/anim_blend_tree.cpp
    ${KES_ENGINE_DIR}/text_block_file.cpp
    ${KES_ENGINE_DIR}/po_catalog.cpp
    ${KES_ENGINE_DIR}/particle_asset.cpp
    ${KES_ENGINE_DIR}/particle_graph.cpp
    ${KES_ENGINE_DIR}/particle_sim.cpp
    engine/EngineFileIo.cpp)
target_include_directories(forge_engine PUBLIC ${KES_ENGINE_DIR})
target_compile_definitions(forge_engine PUBLIC KES_PROFILING=0 _CRT_SECURE_NO_WARNINGS NOMINMAX)

# Everything but main(): the editor shell, the framework (src/framework) and the editor
# modules (src/modules). Tests link it too.
file(GLOB_RECURSE FORGE_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
list(REMOVE_ITEM FORGE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
add_library(forge_core STATIC ${FORGE_SOURCES})
target_include_directories(forge_core PUBLIC src)
target_link_libraries(forge_core PUBLIC forge_engine Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
target_compile_definitions(forge_core PUBLIC FORGE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

if(WIN32)
    add_executable(forge WIN32 src/main.cpp ./resource/app.rc)
else()
    add_executable(forge src/main.cpp)
endif()
target_link_libraries(forge PRIVATE forge_core)

# Available as :/preview/kes_common.slang
qt_add_resources(forge "shadergraph_preview"
    PREFIX "/"
    BASE src/modules/shaderGraph/resource
    FILES src/modules/shaderGraph/resource/preview/kes_common.slang
)

if(MSVC)
    foreach(target forge_engine forge_core forge)
        target_compile_options(${target} PRIVATE /utf-8)
    endforeach()
endif()

add_custom_command(TARGET forge POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/entities $<TARGET_FILE_DIR:forge>/entities)

include(CTest)
if(BUILD_TESTING)
    foreach(test shadergraph_test modeleditor_test localization_test project_test particles_test)
        add_executable(forge_${test} tests/${test}.cpp)
        target_link_libraries(forge_${test} PRIVATE forge_core)
        if(MSVC)
            target_compile_options(forge_${test} PRIVATE /utf-8)
        endif()
        add_test(NAME forge_${test} COMMAND forge_${test})
        if(WIN32)
            set_tests_properties(forge_${test} PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${Qt6_DIR}/../../../bin")
        endif()
    endforeach()
endif()
