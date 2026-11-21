cmake_minimum_required(VERSION 3.16)
project(Forge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

# kmodel loading uses the engine's own reader so the preview matches the game.
set(KES_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../engine" CACHE PATH "KES engine source folder")
if(NOT EXISTS "${KES_ENGINE_DIR}/kmodel.h")
    message(FATAL_ERROR "KES engine sources not found at ${KES_ENGINE_DIR}; set KES_ENGINE_DIR")
endif()
add_library(forge_engine STATIC ${KES_ENGINE_DIR}/kmodel.cpp ${KES_ENGINE_DIR}/animation_pose.cpp
    ${KES_ENGINE_DIR}/animation_clip.cpp engine/EngineFileIo.cpp)
target_include_directories(forge_engine PUBLIC ${KES_ENGINE_DIR})
target_compile_definitions(forge_engine PUBLIC KES_PROFILING=0 _CRT_SECURE_NO_WARNINGS NOMINMAX)
set_target_properties(forge_engine PROPERTIES CXX_STANDARD 20)

file(GLOB_RECURSE FORGE_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)

if(WIN32)
    add_executable(forge WIN32 ${FORGE_SOURCES})
else()
    add_executable(forge ${FORGE_SOURCES})
endif()

target_include_directories(forge PRIVATE src)
target_link_libraries(forge PRIVATE forge_engine Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
target_compile_definitions(forge PRIVATE FORGE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

if(WIN32)
    target_sources(forge PRIVATE ./resource/app.rc)
endif()


add_custom_command(TARGET forge POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/entities $<TARGET_FILE_DIR:forge>/entities)