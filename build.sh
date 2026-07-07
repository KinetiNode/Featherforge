#!/usr/bin/env bash

set -e

echo "========================================="
echo "      FeatherForge Build Script"
echo "========================================="
echo

# ----------------------------------------------------
# Detect package manager
# ----------------------------------------------------

PKG_MANAGER=""

if command -v apt >/dev/null 2>&1; then
    PKG_MANAGER="apt"
elif command -v dnf >/dev/null 2>&1; then
    PKG_MANAGER="dnf"
elif command -v pacman >/dev/null 2>&1; then
    PKG_MANAGER="pacman"
fi

install_hint() {
    case "$PKG_MANAGER" in
        apt)
            echo "Install it with:"
            echo "sudo apt update"
            echo "sudo apt install $1"
            ;;
        dnf)
            echo "Install it with:"
            echo "sudo dnf install $2"
            ;;
        pacman)
            echo "Install it with:"
            echo "sudo pacman -S $3"
            ;;
        *)
            echo "Please install '$1' using your distribution's package manager."
            ;;
    esac
}

echo "Checking build dependencies..."
echo

# ----------------------------------------------------
# g++
# ----------------------------------------------------

if ! command -v g++ >/dev/null 2>&1; then
    echo "ERROR: g++ not found."
    install_hint "build-essential" "gcc-c++" "gcc"
    exit 1
fi

# ----------------------------------------------------
# GLFW
# ----------------------------------------------------

if ! pkg-config --exists glfw3; then
    echo "ERROR: GLFW development package not found."
    install_hint "libglfw3-dev" "glfw-devel" "glfw"
    exit 1
fi

# ----------------------------------------------------
# pkg-config
# ----------------------------------------------------

if ! command -v pkg-config >/dev/null 2>&1; then
    echo "ERROR: pkg-config not found."
    install_hint "pkg-config" "pkgconf-pkg-config" "pkgconf"
    exit 1
fi

echo "All dependencies found."
echo

echo "Building FeatherForge..."
echo

g++ \
-Ivendor \
-Ivendor/imgui \
-Ivendor/imgui/backends \
-Ivendor/TextEditor \
-std=c++17 \
-O2 \
src/main.cpp \
vendor/TextEditor/TextEditor.cpp \
vendor/imgui/imgui.cpp \
vendor/imgui/imgui_draw.cpp \
vendor/imgui/imgui_widgets.cpp \
vendor/imgui/imgui_tables.cpp \
vendor/imgui/backends/imgui_impl_glfw.cpp \
vendor/imgui/backends/imgui_impl_opengl3.cpp \
$(pkg-config --cflags --libs glfw3) \
-lGL \
-lX11 \
-lpthread \
-lXrandr \
-lXi \
-ldl \
-o FeatherForge

echo
echo "========================================="
echo "Build successful!"
echo "========================================="
echo
echo "Run with:"
echo "./FeatherForge"
