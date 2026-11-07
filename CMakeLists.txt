cmake_minimum_required(VERSION 3.28)

project(pico_desktop_runtime
  VERSION 0.0.0
  DESCRIPTION "Pico Desktop native runtime: the CEF host and its IPC layer"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# With CEF (the `cef` preset): CEF_ROOT is the unpacked distribution pinned in toolchain.json
# (`pnpm cef:fetch` prints it). Without it only the CEF-free parts and a stub host build.
option(PD_WITH_CEF "Build the host against CEF" OFF)

# Our warnings, per target (CEF's own targets keep CEF's flags).
function(pd_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

# The fuzz tests' preset (`sanitize`): ASan and UBSan, failing on the first report.
option(PD_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(PD_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

enable_testing()

if(PD_WITH_CEF)
  if(NOT CEF_ROOT AND DEFINED ENV{PD_CEF_ROOT})
    set(CEF_ROOT "$ENV{PD_CEF_ROOT}")
  endif()
  if(NOT CEF_ROOT OR NOT EXISTS "${CEF_ROOT}/cmake/FindCEF.cmake")
    message(FATAL_ERROR "PD_WITH_CEF needs CEF_ROOT (or PD_CEF_ROOT): run `pnpm cef:fetch` and pass the path it prints")
  endif()
  set(CEF_ROOT "${CEF_ROOT}" CACHE PATH "The unpacked CEF distribution" FORCE)
  enable_language(C)  # CEF's CMake checks C compiler flags too
  # CEF's detection only knows the processor name `arm64` (macOS); Linux says `aarch64`.
  if(NOT DEFINED PROJECT_ARCH AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(PROJECT_ARCH "arm64")
  endif()
  list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
  find_package(CEF REQUIRED)
  if(MSVC)
    # CEF's libraries use the static C runtime (/MT); everything linked with them must too.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)
endif()

add_subdirectory(runtime/ipc)
add_subdirectory(runtime/cef-host)

# The test scripts' shared helpers (scripts/lib), POSIX (bash and perl).
if(NOT WIN32)
  add_test(NAME scripts.timeout COMMAND ${PROJECT_SOURCE_DIR}/scripts/tests/timeout.sh)
  set_tests_properties(scripts.timeout PROPERTIES TIMEOUT 60)
endif()
