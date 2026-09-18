#!/bin/bash

# Cross-compiles the GUI test client for Windows (64 bit) from Linux, producing a single
# self-contained gui/zoundream_gui.exe with no runtime dependencies (TLS uses the native
# Windows Schannel, so no certificate bundle or DLLs need to be shipped).
#
# Requirements:
# - the llvm-mingw toolchain (see below; no root needed, it is just a tarball)
# - a static Windows libcurl in gui/.win-deps - built once by ./build_windows_deps.sh

set -e
cd "$(dirname "$0")"

TOOLCHAIN="${LLVM_MINGW:-$HOME/babyt/toolchains/llvm-mingw}"
if [ ! -x "$TOOLCHAIN/bin/x86_64-w64-mingw32-gcc" ]; then
    echo "llvm-mingw toolchain not found at $TOOLCHAIN" >&2
    echo "Download a release from https://github.com/mstorsjo/llvm-mingw/releases" >&2
    echo "(the ucrt-ubuntu-*-x86_64 tarball), extract it there, or set LLVM_MINGW." >&2
    exit 1
fi
export PATH="$TOOLCHAIN/bin:$PATH"
CC=x86_64-w64-mingw32-gcc
CXX=x86_64-w64-mingw32-g++

DEPS=.win-deps
if [ ! -f $DEPS/lib/libcurl.a ]; then
    echo "Static Windows libcurl not found. Run ./build_windows_deps.sh first." >&2
    exit 1
fi

CORE=..
VENDOR=vendor
BUILD=build-windows
CFLAGS="-O2 -Wall"
mkdir -p $BUILD

# ---- GLFW (static, Win32 backend) ----
GLFW_SOURCES="context.c init.c input.c monitor.c platform.c vulkan.c window.c
              egl_context.c osmesa_context.c wgl_context.c
              win32_init.c win32_joystick.c win32_module.c win32_monitor.c
              win32_thread.c win32_time.c win32_window.c
              null_init.c null_monitor.c null_window.c null_joystick.c"

if [ ! -f $BUILD/libglfw3.a ]; then
    echo "Compiling GLFW (win32)..."
    for f in $GLFW_SOURCES; do
        $CC $CFLAGS -D_GLFW_WIN32 -I$VENDOR/glfw/include \
            -c $VENDOR/glfw/src/$f -o $BUILD/glfw_${f%.c}.o
    done
    x86_64-w64-mingw32-ar rcs $BUILD/libglfw3.a $BUILD/glfw_*.o
fi

# ---- Dear ImGui (static) ----
if [ ! -f $BUILD/libimgui.a ]; then
    echo "Compiling Dear ImGui (win32)..."
    for f in imgui imgui_draw imgui_tables imgui_widgets; do
        $CXX $CFLAGS -I$VENDOR/imgui -c $VENDOR/imgui/$f.cpp -o $BUILD/$f.o
    done
    $CXX $CFLAGS -DGLFW_INCLUDE_NONE -I$VENDOR/imgui -I$VENDOR/glfw/include \
        -c $VENDOR/imgui/backends/imgui_impl_glfw.cpp -o $BUILD/imgui_impl_glfw.o
    $CXX $CFLAGS -I$VENDOR/imgui \
        -c $VENDOR/imgui/backends/imgui_impl_opengl3.cpp -o $BUILD/imgui_impl_opengl3.o
    x86_64-w64-mingw32-ar rcs $BUILD/libimgui.a $BUILD/imgui*.o
fi

# ---- tinyfiledialogs (native file dialogs) ----
if [ ! -f $BUILD/tinyfiledialogs.o ]; then
    $CC $CFLAGS -c $VENDOR/tinyfiledialogs/tinyfiledialogs.c -o $BUILD/tinyfiledialogs.o
fi

# ---- Client core (from the repository root) ----
for f in api audio client_core zc_io zc_log third_party/cJSON; do
    $CC $CFLAGS -DCURL_STATICLIB -I$DEPS/include \
        -I$CORE -c $CORE/$f.c -o $BUILD/core_$(basename $f).o
done

# ---- The GUI itself ----
echo "Compiling and linking zoundream_gui.exe..."
$CXX $CFLAGS -DCURL_STATICLIB -I$CORE -I$VENDOR/imgui -I$VENDOR/imgui/backends -I$VENDOR/glfw/include -I$VENDOR/tinyfiledialogs \
    -c main.cpp -o $BUILD/main.o
$CXX -static -mwindows \
    $BUILD/main.o $BUILD/core_*.o $BUILD/tinyfiledialogs.o $BUILD/libimgui.a $BUILD/libglfw3.a \
    $DEPS/lib/libcurl.a \
    -lpthread -lws2_32 -lcrypt32 -lbcrypt -ladvapi32 \
    -lopengl32 -lgdi32 -luser32 -lshell32 -lole32 -lcomdlg32 \
    -o zoundream_gui.exe

echo "Built gui/zoundream_gui.exe"
