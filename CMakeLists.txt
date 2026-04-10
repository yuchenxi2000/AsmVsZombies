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
target_include_directories(avzloader PRIVATE inc)
target_link_libraries(avzloader PRIVATE DbgHelp c++experimental)
target_link_options(avzloader PRIVATE -static)
set_target_properties(avzloader PROPERTIES
    PREFIX ""
    OUTPUT_NAME "avzloader"
)

# build mod loader installer
add_executable(avzinstaller loader/avzinstaller.cpp)
target_link_options(avzinstaller PRIVATE -static)

# build example mod: lazy script
add_library(lazy SHARED ${avz_srcs} ${minhook_srcs} mod/lazy.cpp)
target_compile_definitions(lazy PRIVATE COMPILE_MOD)
target_compile_options(lazy PRIVATE -fexperimental-library)
target_include_directories(lazy PRIVATE inc)
target_link_libraries(lazy PRIVATE DbgHelp c++experimental)
target_link_options(lazy PRIVATE -static)
set_target_properties(lazy PROPERTIES
    PREFIX ""
    OUTPUT_NAME "lazy"
)

# build example mod: auto-garden
add_library(garden SHARED ${avz_srcs} ${minhook_srcs} mod/autogarden.cpp)
target_compile_definitions(garden PRIVATE COMPILE_MOD)
target_compile_options(garden PRIVATE -fexperimental-library)
target_include_directories(garden PRIVATE inc)
target_link_libraries(garden PRIVATE DbgHelp c++experimental)
target_link_options(garden PRIVATE -static)
set_target_properties(garden PROPERTIES
    PREFIX ""
    OUTPUT_NAME "garden"
)

# 此条命令仅用于作者打包 avz，仅编译 avz 请注释这条命令
add_custom_target(PackageAvz ALL
    COMMAND powershell -c "\
        cd ${PROJECT_SOURCE_DIR}; \
        rm bin/*.dll; \
        Compress-Archive -Path bin/,inc/,src/,metadata.json -Force -DestinationPath release/env2/nightly.zip"
    DEPENDS avz injector
)
