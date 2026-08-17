# Fovelle

Fovelle is a lightweight image viewer designed specifically for macOS.
It is based on qView and keeps its focus on fast, minimal, and unobtrusive image viewing.

The build and test entry points are CMake (`CMakeLists.txt`) and, for upstream-compatible
packaging, qmake (`qView.pro`). The default CMake build is deterministic, macOS-only, and
does not perform an automatic network version check during tests.
