cmake_minimum_required(VERSION 3.10.0)
project(OynonTools VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SIZEOF_VOID_P AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "OynonTools must be built as Win32/x86 to hook Pathologic Game.exe.")
endif()

add_library(OynonTools SHARED
    src/console_execute_hook.cpp
    src/console_read_hook.cpp
    src/debug.cpp
    src/inline_hook_utils.cpp
    src/movement_hooks.cpp
    src/OynonToolsApi.cpp
    src/oynontools_state.cpp
    src/ui_daychange_hook.cpp
)

target_compile_features(OynonTools PRIVATE cxx_std_17)
target_compile_definitions(OynonTools PRIVATE OYNONTOOLS_EXPORTS WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(OynonTools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

set_target_properties(OynonTools PROPERTIES
    PREFIX ""
    OUTPUT_NAME "OynonTools"
    SUFFIX ".dll"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib/Win32/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/Win32/$<CONFIG>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/Win32/$<CONFIG>"
)

if(MSVC)
    target_compile_options(OynonTools PRIVATE /W4 /EHsc)
    target_link_options(OynonTools PRIVATE /SUBSYSTEM:WINDOWS)
endif()
