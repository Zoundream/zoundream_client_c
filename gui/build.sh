#!/bin/bash

# Builds the GUI test client (zoundream_gui) for Linux.
#
# The vendored GLFW and Dear ImGui are compiled into static libraries in gui/build/
# the first time (delete gui/build/ to force a rebuild of those), then the client core
# from the repository root and the GUI itself are compiled and linked.
#
# Requirements beyond the command line client's (libcurl, json-c, sndfile dev packages):
# X11 headers - either system-wide (sudo apt install xorg-dev) or fetched locally
# without root via ./fetch_x11_headers.sh

set -e
cd "$(dirname "$0")"

CORE=..
VENDOR=vendor
BUILD=build
CFLAGS="-O2 -Wall"
mkdir -p $BUILD

# ---- X11 headers: system ones if present, otherwise the local sysroot ----
if [ -f /usr/include/X11/Xlib.h ]; then
    X11_INC=""
elif [ -f .deps-sysroot/usr/include/X11/Xlib.h ]; then
    X11_INC="-I.deps-sysroot/usr/include"
else
    echo "X11 headers not found. Run ./fetch_x11_headers.sh (no root needed)" >&2
    echo "or install them system wide with: sudo apt install xorg-dev" >&2
    exit 1
fi

# ---- GLFW (static, X11 backend; the X11 extension libraries are loaded at runtime) ----
GLFW_SOURCES="context.c init.c input.c monitor.c platform.c vulkan.c window.c
              egl_context.c osmesa_context.c glx_context.c
              x11_init.c x11_monitor.c x11_window.c xkb_unicode.c
              posix_module.c posix_poll.c posix_thread.c posix_time.c
              linux_joystick.c null_init.c null_monitor.c null_window.c null_joystick.c"

if [ ! -f $BUILD/libglfw3.a ]; then
    echo "Compiling GLFW..."
    for f in $GLFW_SOURCES; do
        gcc $CFLAGS -D_GLFW_X11 $X11_INC -I$VENDOR/glfw/include \
            -c $VENDOR/glfw/src/$f -o $BUILD/glfw_${f%.c}.o
    done
    ar rcs $BUILD/libglfw3.a $BUILD/glfw_*.o
fi

# ---- Dear ImGui (static) ----
if [ ! -f $BUILD/libimgui.a ]; then
    echo "Compiling Dear ImGui..."
    for f in imgui imgui_draw imgui_tables imgui_widgets; do
        g++ $CFLAGS -I$VENDOR/imgui -c $VENDOR/imgui/$f.cpp -o $BUILD/$f.o
    done
    g++ $CFLAGS -DGLFW_INCLUDE_NONE -I$VENDOR/imgui -I$VENDOR/glfw/include $X11_INC \
        -c $VENDOR/imgui/backends/imgui_impl_glfw.cpp -o $BUILD/imgui_impl_glfw.o
    g++ $CFLAGS -I$VENDOR/imgui \
        -c $VENDOR/imgui/backends/imgui_impl_opengl3.cpp -o $BUILD/imgui_impl_opengl3.o
    ar rcs $BUILD/libimgui.a $BUILD/imgui*.o
fi

# ---- Client core (from the repository root) ----
for f in api audio client_core zc_log third_party/cJSON; do
    gcc $CFLAGS `curl-config --cflags` \
        -I$CORE -c $CORE/$f.c -o $BUILD/core_$(basename $f).o
done

# ---- Link libraries that may lack a .so dev symlink on this machine ----
find_lib() { # find_lib <-lname> <soname>
    if /sbin/ldconfig -p | grep -q "$2 "; then
        /sbin/ldconfig -p | awk -v lib="$2" '$1 == lib {print $NF; exit}'
    else
        echo "$1"
    fi
}
X11_LIB=$([ -e /usr/lib/x86_64-linux-gnu/libX11.so ] && echo "-lX11" || find_lib -lX11 libX11.so.6)
GL_LIB=$([ -e /usr/lib/x86_64-linux-gnu/libGL.so ] && echo "-lGL" || find_lib -lGL libGL.so.1)

# ---- The GUI itself ----
echo "Compiling and linking zoundream_gui..."
g++ $CFLAGS -I$CORE -I$VENDOR/imgui -I$VENDOR/imgui/backends -I$VENDOR/glfw/include \
    main.cpp $BUILD/core_*.o $BUILD/libimgui.a $BUILD/libglfw3.a \
    `curl-config --libs` \
    $X11_LIB $GL_LIB -lm -lpthread -ldl \
    -o zoundream_gui

echo "Built gui/zoundream_gui"
