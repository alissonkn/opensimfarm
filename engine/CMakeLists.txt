cmake_minimum_required(VERSION 3.16)
project(OpenSimFarm VERSION 0.6.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# SDL2: use an installed package (find_package) or point SDL2_ROOT at an unpacked SDL2 development tree
# (include/SDL2 + lib), e.g. -DSDL2_ROOT=$HOME/tools/sdl2/usr on Linux or the SDL2-devel-VC / mingw archive on Windows.
set(SDL2_ROOT "" CACHE PATH "Optional root of an SDL2 development tree")
if(EMSCRIPTEN)
    # the browser version: Emscripten's own SDL2 port (see tools/build_web.sh and site/play/)
    add_library(SDL2::SDL2 INTERFACE IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES INTERFACE_COMPILE_OPTIONS "-sUSE_SDL=2" INTERFACE_LINK_OPTIONS "-sUSE_SDL=2")
elseif(SDL2_ROOT)
    find_path(SDL2_INCLUDE_DIR SDL.h PATHS "${SDL2_ROOT}/include/SDL2" "${SDL2_ROOT}/include" NO_DEFAULT_PATH)
    find_library(SDL2_LIBRARY NAMES SDL2 SDL2-2.0 PATHS "${SDL2_ROOT}/lib64" "${SDL2_ROOT}/lib" "${SDL2_ROOT}/lib/x64" NO_DEFAULT_PATH)
    if(NOT SDL2_INCLUDE_DIR OR NOT SDL2_LIBRARY)
        message(FATAL_ERROR "SDL2 not found under SDL2_ROOT=${SDL2_ROOT}")
    endif()
    add_library(SDL2::SDL2 UNKNOWN IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES IMPORTED_LOCATION "${SDL2_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}")
    if(WIN32)
        find_library(SDL2MAIN_LIBRARY NAMES SDL2main PATHS "${SDL2_ROOT}/lib" "${SDL2_ROOT}/lib/x64" NO_DEFAULT_PATH)
        if(SDL2MAIN_LIBRARY)
            add_library(SDL2::SDL2main STATIC IMPORTED)
            set_target_properties(SDL2::SDL2main PROPERTIES IMPORTED_LOCATION "${SDL2MAIN_LIBRARY}")
        endif()
    endif()
else()
    find_package(SDL2 REQUIRED CONFIG)
endif()

# the translations (lang/*.po) are built into the program
file(GLOB OSF_LANG_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lang/*.po)
list(SORT OSF_LANG_FILES)
set(OSF_LANG_CPP ${CMAKE_CURRENT_BINARY_DIR}/lang_builtin.cpp)
add_custom_command(OUTPUT ${OSF_LANG_CPP}
    COMMAND ${CMAKE_COMMAND} -DOUT=${OSF_LANG_CPP} "-DFILES=${OSF_LANG_FILES}" -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/EmbedLang.cmake
    DEPENDS ${OSF_LANG_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/tools/EmbedLang.cmake VERBATIM)

# the engine's own songs (music/*.mid, made by tools/make_songs.py) are built into the program
file(GLOB OSF_MUSIC_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/music/*.mid)
list(SORT OSF_MUSIC_FILES)
set(OSF_MUSIC_CPP ${CMAKE_CURRENT_BINARY_DIR}/music_builtin.cpp)
add_custom_command(OUTPUT ${OSF_MUSIC_CPP}
    COMMAND ${CMAKE_COMMAND} -DOUT=${OSF_MUSIC_CPP} "-DFILES=${OSF_MUSIC_FILES}" -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/EmbedMusic.cmake
    DEPENDS ${OSF_MUSIC_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/tools/EmbedMusic.cmake VERBATIM)

add_executable(opensimfarm
    src/main.cpp
    src/core/Paths.cpp
    src/core/InstallDisc.cpp
    src/core/Lang.cpp
    ${OSF_LANG_CPP}
    ${OSF_MUSIC_CPP}
    src/assets/Megadata.cpp
    src/assets/Graphics.cpp
    src/assets/Picture.cpp
    src/assets/WindowsEdition.cpp
    src/assets/SaveGame.cpp
    src/audio/AudioFx.cpp
    src/audio/ModernSynth.cpp
    src/audio/Music.cpp
    src/audio/Sound.cpp
    src/game/Animals.cpp
    src/game/Traffic.cpp
    src/game/AutoPlayer.cpp
    src/game/Crops.cpp
    src/game/Disasters.cpp
    src/game/Editing.cpp
    src/game/MapEdit.cpp
    src/game/Flight.cpp
    src/game/GameState.cpp
    src/game/Irrigation.cpp
    src/game/Machines.cpp
    src/game/Shop.cpp
    src/game/Simulation.cpp
    src/game/WorldGen.cpp
    src/ui/GameUi.cpp
    src/ui/Gfx.cpp
    src/ui/PictureText.cpp
    src/ui/LangTest.cpp
    src/ui/MapView.cpp
    src/ui/PixelArt.cpp
    src/ui/Touch.cpp
    src/ui/PhoneUi.cpp
    src/ui/StartScreen.cpp
    src/ui/MapEditor.cpp
    src/ui/Windows.cpp
)
target_include_directories(opensimfarm PRIVATE src)
target_compile_definitions(opensimfarm PRIVATE OSF_VERSION="${PROJECT_VERSION}")
target_link_libraries(opensimfarm PRIVATE SDL2::SDL2)
if(MSVC)
    target_compile_options(opensimfarm PRIVATE /W4 /utf-8)
else()
    target_compile_options(opensimfarm PRIVATE -Wall -Wextra)
endif()
if(EMSCRIPTEN)
    # opensimfarm.js + opensimfarm.wasm for site/play/: the page puts the player's game files into the in-memory file
    # system and starts main() itself; settings and saves live in the browser's storage (IDBFS)
    target_link_options(opensimfarm PRIVATE -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=67108864 -sSTACK_SIZE=1048576 -sINVOKE_RUN=0 -sEXIT_RUNTIME=1 -sFORCE_FILESYSTEM=1 -lidbfs.js
        -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,ENV,callMain -sENVIRONMENT=web)
endif()
if(WIN32)
    set_target_properties(opensimfarm PROPERTIES WIN32_EXECUTABLE ON)
    target_link_libraries(opensimfarm PRIVATE shell32 ole32) # the folder dialog for finding SimFarm
    # the program icon: the resource is compiled by the Windows resource compiler (MSVC, MinGW windres); a
    # toolchain without one names a command that turns the .rc into a COFF object for the linker (Zig: "zig rc /:output-format coff")
    set(OSF_RC ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/opensimfarm.rc)
    if(OSF_RC_TO_RES)
        set(OSF_RES ${CMAKE_CURRENT_BINARY_DIR}/opensimfarm_res.obj)
        add_custom_command(OUTPUT ${OSF_RES}
            COMMAND ${OSF_RC_TO_RES} /fo ${OSF_RES} ${OSF_RC}
            DEPENDS ${OSF_RC} ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/opensimfarm.ico
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows VERBATIM)
        set_source_files_properties(${OSF_RES} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
        target_sources(opensimfarm PRIVATE ${OSF_RES})
    else()
        enable_language(RC)
        target_sources(opensimfarm PRIVATE ${OSF_RC})
    endif()
    if(TARGET SDL2::SDL2main)
        target_link_libraries(opensimfarm PRIVATE SDL2::SDL2main)
        if(MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            target_link_libraries(opensimfarm PRIVATE shell32)
            target_link_options(opensimfarm PRIVATE -Wl,--subsystem,windows)
        endif()
    endif()
endif()
