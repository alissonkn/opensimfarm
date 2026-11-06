# OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
# Licensed under the GNU General Public License version 3 (see LICENSE.txt).
# The Linux x64 build for the rpm and deb packages, with Zig: runs on glibc 2.35 or newer (Ubuntu 22.04+, Debian 12+,
# Fedora 36+), the C++ library is linked in, so the program needs only SDL2 and glibc from the system:
#   cmake -S . -B build-pkg -DCMAKE_TOOLCHAIN_FILE=packaging/toolchains/linux-glibc235.cmake -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_SKIP_RPATH=ON [-DSDL2_ROOT=<SDL2 headers>]
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig-linux-cc)
set(CMAKE_CXX_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig-linux-cxx)
set(CMAKE_AR ${CMAKE_CURRENT_LIST_DIR}/zig-ar)
set(CMAKE_RANLIB ${CMAKE_CURRENT_LIST_DIR}/zig-ranlib)
# Debian and Ubuntu keep libraries (and SDL2's CMake files) in /usr/lib/x86_64-linux-gnu and part of SDL2's headers
# in /usr/include/x86_64-linux-gnu; Zig knows neither folder. The header folder is searched last (-idirafter), so
# Zig's own glibc 2.35 headers still come first.
if(EXISTS /usr/lib/x86_64-linux-gnu)
    set(CMAKE_LIBRARY_ARCHITECTURE x86_64-linux-gnu)
endif()
if(EXISTS /usr/include/x86_64-linux-gnu)
    set(CMAKE_C_FLAGS_INIT "-idirafter /usr/include/x86_64-linux-gnu")
    set(CMAKE_CXX_FLAGS_INIT "-idirafter /usr/include/x86_64-linux-gnu")
endif()
