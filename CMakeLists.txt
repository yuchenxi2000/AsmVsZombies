cmake_minimum_required(VERSION 3.8)
project(AsmVsZombies)

# set global options
add_compile_options(-m32 -std=c++20 -Wall)
add_link_options(-m32)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# build libavz
aux_source_directory(src avz_srcs)
aux_source_directory(src/minhook minhook_srcs)
add_library(avz ${avz_srcs} ${minhook_srcs})
target_compile_options(avz PRIVATE -fexperimental-library)
target_include_directories(avz PRIVATE inc)

# build injector
add_subdirectory(tools/injector)

# build mod loader
add_library(avzloader SHARED src/avz_asm.cpp loader/avzloader.cpp)
target_compile_definitions(avzloader PRIVATE COMPILE_LOADER)
target_compile_options(avzloader PRIVATE -fexperimental-library)
target_include_directories(avzloader PRIVATE inc loader/tomlplusplus/include)
target_link_libraries(avzloader PRIVATE DbgHelp c++experimental)
target_link_options(avzloader PRIVATE -static)
set_target_properties(avzloader PROPERTIES
    PREFIX ""
    OUTPUT_NAME "avzloader"
)

# build mod loader installer
add_executable(avzinstaller loader/avzinstaller.cpp)
target_link_options(avzinstaller PRIVATE -static)

# build example mods
set(mod_srcs mod/lazy.cpp mod/autogarden.cpp mod/pe12.cpp)
foreach(mod_src IN LISTS mod_srcs)
    get_filename_component(mod_name ${mod_src} NAME_WE)
    add_library(${mod_name} SHARED ${mod_src})
    target_compile_options(${mod_name} PRIVATE -fexperimental-library)
    target_include_directories(${mod_name} PRIVATE inc)
    target_link_libraries(${mod_name} PRIVATE avz gdi32 dbghelp c++experimental)
    target_link_options(${mod_name} PRIVATE -static)
    set_target_properties(${mod_name} PROPERTIES
        PREFIX ""
        OUTPUT_NAME ${mod_name}
    )
endforeach()

# 此条命令仅用于作者打包 avz，仅编译 avz 请注释这条命令
add_custom_target(PackageAvz ALL
    COMMAND powershell -c "\
        cd ${PROJECT_SOURCE_DIR}; \
        rm bin/*.dll; \
        Compress-Archive -Path bin/,inc/,src/,metadata.json -Force -DestinationPath release/env2/nightly.zip"
    DEPENDS avz injector
)
