cmake_minimum_required(VERSION 3.28)

project(pico_desktop_runtime
  VERSION 0.0.0
  DESCRIPTION "Pico Desktop native runtime: the CEF host and its IPC layer"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# CEF itself arrives in F1 (pinned in toolchain.json); until then the host builds without it.
option(PD_WITH_CEF "Build the host against CEF (F1)" OFF)

if(MSVC)
  add_compile_options(/W4 /WX)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()

add_subdirectory(runtime/ipc)
add_subdirectory(runtime/cef-host)
