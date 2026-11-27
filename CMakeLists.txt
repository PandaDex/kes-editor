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

# RmlUi + FreeType for the .rml preview, the same submodules and options as cmake/KesUi.cmake,
# always static here.
get_filename_component(KES_ROOT_DIR "${KES_ENGINE_DIR}/.." ABSOLUTE)
if(NOT TARGET rmlui_core)
    foreach(_forge_ui_dep IN ITEMS RmlUi freetype)
        if(NOT EXISTS "${KES_ROOT_DIR}/external/${_forge_ui_dep}/CMakeLists.txt")
            message(FATAL_ERROR "Forge: ${KES_ROOT_DIR}/external/${_forge_ui_dep} missing; git submodule update --init external/RmlUi external/freetype")
        endif()
    endforeach()
    set(_forge_saved_shared "${BUILD_SHARED_LIBS}")
    set(BUILD_SHARED_LIBS OFF)
    set(FT_DISABLE_ZLIB ON)
    set(FT_DISABLE_BZIP2 ON)
    set(FT_DISABLE_PNG ON)
    set(FT_DISABLE_HARFBUZZ ON)
    set(FT_DISABLE_BROTLI ON)
    set(SKIP_INSTALL_ALL ON)
    add_subdirectory("${KES_ROOT_DIR}/external/freetype" "${CMAKE_BINARY_DIR}/external/freetype" EXCLUDE_FROM_ALL)
    if(NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
    set(CMAKE_DISABLE_FIND_PACKAGE_Freetype ON)
    set(RMLUI_FONT_ENGINE "freetype")
    set(RMLUI_SAMPLES OFF)
    set(RMLUI_LUA_BINDINGS OFF)
    set(RMLUI_SVG_PLUGIN OFF)
    set(RMLUI_LOTTIE_PLUGIN OFF)
    set(RMLUI_TRACY_PROFILING OFF)
    set(RMLUI_WARNINGS_AS_ERRORS OFF)
    add_subdirectory("${KES_ROOT_DIR}/external/RmlUi" "${CMAKE_BINARY_DIR}/external/RmlUi" EXCLUDE_FROM_ALL)
    unset(CMAKE_DISABLE_FIND_PACKAGE_Freetype)
    set(BUILD_SHARED_LIBS "${_forge_saved_shared}")
endif()

# Everything but main(): the editor shell, the framework (src/framework) and the editor
# modules (src/modules). Tests link it too.
file(GLOB_RECURSE FORGE_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
list(REMOVE_ITEM FORGE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
add_library(forge_core STATIC ${FORGE_SOURCES})
target_include_directories(forge_core PUBLIC src)
target_link_libraries(forge_core PUBLIC forge_engine Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets RmlUi::Core)
target_compile_definitions(forge_core PUBLIC FORGE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    KES_ENGINE_ASSETS_DIR="${KES_ENGINE_DIR}/assets" FORGE_VERSION="${PROJECT_VERSION}")

if(WIN32)
    add_executable(forge WIN32 src/main.cpp ./resource/app.rc)
else()
    add_executable(forge src/main.cpp)
endif()
target_link_libraries(forge PRIVATE forge_core)
set_target_properties(forge PROPERTIES OUTPUT_NAME "kes-editor")

qt_add_resources(forge "forge_brand"
    PREFIX "/brand"
    BASE resource/brand
    FILES resource/brand/icon-light.png resource/brand/logo-light.png
)

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

# Tracked per file so editing a .dec updates the exe folder without relinking forge.
# The folder is spelled out because $<TARGET_FILE_DIR:forge> would make this depend on forge.
get_property(forge_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(forge_multi_config)
    set(forge_exe_dir "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
else()
    set(forge_exe_dir "${CMAKE_CURRENT_BINARY_DIR}")
endif()
file(GLOB forge_entity_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/entities/*.dec")
set(forge_entity_outputs)
foreach(src IN LISTS forge_entity_files)
    get_filename_component(name "${src}" NAME)
    set(dst "${forge_exe_dir}/entities/${name}")
    add_custom_command(OUTPUT "${dst}"
        COMMAND ${CMAKE_COMMAND} -E copy "${src}" "${dst}"
        DEPENDS "${src}"
        VERBATIM)
    list(APPEND forge_entity_outputs "${dst}")
endforeach()
add_custom_target(forge_entities DEPENDS ${forge_entity_outputs})
add_dependencies(forge forge_entities)
# Engine UI assets (/engine/ui/...) for the .rml preview when Forge runs away from the source tree
add_custom_command(TARGET forge POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${KES_ENGINE_DIR}/assets/engine/ui $<TARGET_FILE_DIR:forge>/engine-assets/engine/ui)

include(CTest)
if(BUILD_TESTING)
    foreach(test shadergraph_test modeleditor_test animstatemachine_test localization_test project_test particles_test rmlpreview_test particleview_test assetbrowser_test layout_test assetdrop_test)
        add_executable(forge_${test} tests/${test}.cpp)
        target_link_libraries(forge_${test} PRIVATE forge_core)
        target_compile_definitions(forge_${test} PRIVATE KES_GAME_CONTENT="${CMAKE_CURRENT_SOURCE_DIR}/../../game/content")
        if(MSVC)
            target_compile_options(forge_${test} PRIVATE /utf-8)
        endif()
        add_test(NAME forge_${test} COMMAND forge_${test})
        if(WIN32)
            set_tests_properties(forge_${test} PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${Qt6_DIR}/../../../bin")
        endif()
    endforeach()
endif()
