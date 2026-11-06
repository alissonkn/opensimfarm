# OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
# Licensed under the GNU General Public License version 3 (see LICENSE.txt).
# Cross-compiles the Windows x64 build from Linux with Zig (no MinGW install, no Windows needed):
#   cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=packaging/toolchains/windows.cmake -DCMAKE_BUILD_TYPE=Release \
#         -DSDL2_ROOT=<SDL2-2.x>/x86_64-w64-mingw32       (from the official SDL2-devel-2.x-mingw.tar.gz)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig-win-cc)
set(CMAKE_CXX_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig-win-cxx)
set(CMAKE_AR ${CMAKE_CURRENT_LIST_DIR}/zig-ar)
set(CMAKE_RANLIB ${CMAKE_CURRENT_LIST_DIR}/zig-ranlib)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# the program icon resource: Zig's resource compiler writes a COFF object for the linker (CMakeLists: OSF_RC_TO_RES)
set(OSF_RC_TO_RES python3 -m ziglang rc /:output-format coff /:target x64)
