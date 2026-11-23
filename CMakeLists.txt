# TcWlxMarkdownViewAir - Markdown viewer, Total Commander lister (WLX) plugin
#   Visual Studio:  build_msvc.bat   (or: cmake -S . -B build-x64 -A x64 && cmake --build build-x64 --config Release)
#   MinGW-w64:      make -f Makefile.mingw
cmake_minimum_required(VERSION 3.15)
project(TcWlxMarkdownViewAir CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
cmake_policy(SET CMP0091 NEW)   # MSVC_RUNTIME_LIBRARY

add_library(MarkdownView SHARED
    src/listplug.cpp
    src/lister.cpp
    src/richedit_view.cpp
    src/text_load.cpp
    src/md_parser.cpp
    src/md_rtf.cpp
    src/md_html.cpp
    src/MarkdownView.rc
    src/listplug.def)

# the resource script embeds README.md (help page, H / ?) from the project root
target_include_directories(MarkdownView PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
set_property(SOURCE src/MarkdownView.rc APPEND PROPERTY OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/README.md)

target_compile_definitions(MarkdownView PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601 _CRT_SECURE_NO_WARNINGS NOMINMAX)
target_link_libraries(MarkdownView PRIVATE comdlg32 shell32 gdi32 user32)

if(MSVC)
    # static CRT: no VC++ redistributable needed on the target machine
    set_property(TARGET MarkdownView PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(MarkdownView PRIVATE /W3 /utf-8)
else()
    target_link_options(MarkdownView PRIVATE -static -static-libgcc -static-libstdc++)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PLUGIN_SUFFIX ".wlx64")
else()
    set(PLUGIN_SUFFIX ".wlx")
endif()
# output file name = TC plugin name and default install directory (see pluginst.inf)
set_target_properties(MarkdownView PROPERTIES OUTPUT_NAME TcWlxMarkdownViewAir PREFIX "" SUFFIX ${PLUGIN_SUFFIX})

# optional: test host (fake Lister) for smoke tests on Windows
add_executable(lister_test_host tests/host.cpp)
target_compile_definitions(lister_test_host PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(lister_test_host PRIVATE user32 gdi32)
if(MSVC)
    set_property(TARGET lister_test_host PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_link_options(lister_test_host PRIVATE -municode -static)
endif()
