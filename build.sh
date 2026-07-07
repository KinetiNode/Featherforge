#!/bin/bash

set -e

echo "Building FeatherForge..."

g++ \
-Ivendor/imgui \
-Ivendor/imgui/backends \
-Ivendor/TextEditor \
-Ivendor \
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
-lglfw \
-lGL \
-lX11 \
-lpthread \
-lXrandr \
-lXi \
-ldl \
-o FeatherForge

echo ""
echo "Build complete!"
