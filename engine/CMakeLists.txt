cmake_minimum_required(VERSION 3.16)
project(OpenSimFarm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# SDL2: use an installed package (find_package) or point SDL2_ROOT at an unpacked SDL2 development tree
# (include/SDL2 + lib), e.g. -DSDL2_ROOT=$HOME/tools/sdl2/usr on Linux or the SDL2-devel-VC / mingw archive on Windows.
set(SDL2_ROOT "" CACHE PATH "Optional root of an SDL2 development tree")
if(SDL2_ROOT)
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

add_executable(opensimfarm
    src/main.cpp
    src/core/Paths.cpp
    src/assets/Megadata.cpp
    src/assets/Graphics.cpp
    src/assets/Picture.cpp
    src/assets/SaveGame.cpp
    src/audio/AudioFx.cpp
    src/audio/ModernSynth.cpp
    src/audio/Music.cpp
    src/audio/Sound.cpp
    src/game/Animals.cpp
    src/game/Traffic.cpp
    src/game/Crops.cpp
    src/game/Disasters.cpp
    src/game/Editing.cpp
    src/game/Flight.cpp
    src/game/GameState.cpp
    src/game/Irrigation.cpp
    src/game/Machines.cpp
    src/game/Shop.cpp
    src/game/Simulation.cpp
    src/game/WorldGen.cpp
    src/ui/GameUi.cpp
    src/ui/Gfx.cpp
    src/ui/MapView.cpp
    src/ui/PixelArt.cpp
    src/ui/StartScreen.cpp
    src/ui/Windows.cpp
)
target_include_directories(opensimfarm PRIVATE src)
target_link_libraries(opensimfarm PRIVATE SDL2::SDL2)
if(MSVC)
    target_compile_options(opensimfarm PRIVATE /W4 /utf-8)
else()
    target_compile_options(opensimfarm PRIVATE -Wall -Wextra)
endif()
if(WIN32)
    set_target_properties(opensimfarm PROPERTIES WIN32_EXECUTABLE ON)
    if(TARGET SDL2::SDL2main)
        target_link_libraries(opensimfarm PRIVATE SDL2::SDL2main)
        if(MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            target_link_libraries(opensimfarm PRIVATE shell32)
            target_link_options(opensimfarm PRIVATE -Wl,--subsystem,windows)
        endif()
    endif()
endif()
