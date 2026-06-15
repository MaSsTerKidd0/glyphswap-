# CMakeLists.txt — builds GlyphSwap.dll and Injector.exe.
#
# Works with either toolchain:
#   * MSVC      (Visual Studio "Desktop development with C++")
#   * MinGW-w64 (GCC), e.g. WinLibs
#
# MinHook is compiled from source (third_party/minhook) so there is no
# runtime-library mismatch with the DLL.
#
# Example (MinGW + Ninja):
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# Example (Visual Studio 2022/2026, x64):
#   cmake -S . -B build -A x64
#   cmake --build build --config Release

cmake_minimum_required(VERSION 3.16)
project(GlyphSwap LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# This mod is 64-bit only (One Piece: World Seeker is an x64 process).
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Configure for x64 - OPWS.exe is a 64-bit process.")
endif()

# ---------------------------------------------------------------------------
# MinHook (static, from source)
# ---------------------------------------------------------------------------
set(MINHOOK_DIR "${CMAKE_SOURCE_DIR}/third_party/minhook")
add_library(minhook STATIC
  ${MINHOOK_DIR}/src/buffer.c
  ${MINHOOK_DIR}/src/hook.c
  ${MINHOOK_DIR}/src/trampoline.c
  ${MINHOOK_DIR}/src/hde/hde32.c
  ${MINHOOK_DIR}/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC ${MINHOOK_DIR}/include)

# ---------------------------------------------------------------------------
# The mod DLL
# ---------------------------------------------------------------------------
add_library(GlyphSwap SHARED src/dllmain.cpp)
target_link_libraries(GlyphSwap PRIVATE minhook d3d11 dxgi windowscodecs ole32 oleaut32 uuid)
set_target_properties(GlyphSwap PROPERTIES PREFIX "" OUTPUT_NAME "GlyphSwap")

# ---------------------------------------------------------------------------
# The injector
# ---------------------------------------------------------------------------
add_executable(Injector injector/main.cpp)
set_target_properties(Injector PROPERTIES OUTPUT_NAME "Injector")

# Statically link the GCC/C++ runtimes so the artifacts are self-contained
# (the game does not need any MinGW redistributable present).
if(MINGW)
  target_link_options(GlyphSwap PRIVATE -static -static-libgcc -static-libstdc++)
  target_link_options(Injector     PRIVATE -static -static-libgcc -static-libstdc++)
  # The injector uses wmain(); -municode selects wmainCRTStartup as the entry.
  target_compile_options(Injector PRIVATE -municode)
  target_link_options(Injector    PRIVATE -municode)
endif()

# Statically link the MSVC runtime for the same reason.
if(MSVC)
  set_property(TARGET GlyphSwap Injector PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
