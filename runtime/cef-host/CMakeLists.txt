# The host's CEF-free core: asset resolution, the security policy, options, sandbox inspection,
# message routing.
add_library(pd_host_core STATIC
  src/core/assets.cpp
  src/core/exit_code.cpp
  src/core/interruptible_io.cpp
  src/core/options.cpp
  src/core/policy.cpp
  src/core/router.cpp
  src/core/sandbox.cpp
  src/core/sandbox_mac.cpp
  src/core/sandbox_windows.cpp
  src/core/services.cpp
  src/core/temp_dir.cpp)
target_include_directories(pd_host_core PUBLIC include)
target_link_libraries(pd_host_core PUBLIC pd_ipc)
if(APPLE)
  target_link_libraries(pd_host_core PRIVATE sandbox)  # sandbox_check (src/core/sandbox_mac.cpp)
endif()
pd_warnings(pd_host_core)

add_executable(pd_host_core_test tests/core_test.cpp)
target_link_libraries(pd_host_core_test PRIVATE pd_host_core)
pd_warnings(pd_host_core_test)
add_test(NAME host.core COMMAND pd_host_core_test)

# Stopping a thread blocked in pipe I/O, even when the stop comes between its calls (#16).
add_executable(pd_host_interruptible_io_test tests/interruptible_io_test.cpp)
target_link_libraries(pd_host_interruptible_io_test PRIVATE pd_host_core)
pd_warnings(pd_host_interruptible_io_test)
add_test(NAME host.interruptible-io COMMAND pd_host_interruptible_io_test)
set_tests_properties(host.interruptible-io PROPERTIES TIMEOUT 30)

# How Windows exit codes read in the log: NTSTATUS crashes in hex, the host's kill as "killed"
# (#19, #20). Runs everywhere (it's only formatting).
add_executable(pd_host_exit_code_test tests/exit_code_test.cpp)
target_link_libraries(pd_host_exit_code_test PRIVATE pd_host_core)
pd_warnings(pd_host_exit_code_test)
add_test(NAME host.exit-code COMMAND pd_host_exit_code_test)

# The self-test's throwaway profile directory: private and unguessable on POSIX (#27).
add_executable(pd_host_temp_dir_test tests/temp_dir_test.cpp)
target_link_libraries(pd_host_temp_dir_test PRIVATE pd_host_core)
pd_warnings(pd_host_temp_dir_test)
add_test(NAME host.temp-dir COMMAND pd_host_temp_dir_test)

add_executable(pd_host_router_test tests/router_test.cpp)
target_link_libraries(pd_host_router_test PRIVATE pd_host_core)
pd_warnings(pd_host_router_test)
add_test(NAME host.router COMMAND pd_host_router_test)

# tests/lib.sh, the helper the end-to-end scripts below source (skips, the display, `timeout`).
# Needs no CEF or display, so it runs in every preset. POSIX: it fakes uname, xvfb-run and pico
# with symlinks under `env -i`, which Git Bash on Windows can't stand in for.
if(NOT WIN32)
  add_test(NAME host.test-lib COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/lib_test.sh)
endif()

if(NOT PD_WITH_CEF)
  # Without CEF: a stub that only identifies itself (so packaging and CI have a binary to run).
  add_executable(pico-desktop-host src/stub_main.cpp)
  target_link_libraries(pico-desktop-host PRIVATE pd_ipc)
  target_compile_definitions(pico-desktop-host PRIVATE PD_VERSION="${PROJECT_VERSION}")
  pd_warnings(pico-desktop-host)
  add_test(NAME host.version COMMAND pico-desktop-host --version)
  set_tests_properties(host.version PROPERTIES PASS_REGULAR_EXPRESSION "^pico-desktop-host ${PROJECT_VERSION}")
  add_test(NAME host.unknown-option COMMAND pico-desktop-host --bogus)
  set_tests_properties(host.unknown-option PROPERTIES WILL_FAIL TRUE)
  return()
endif()

# The CEF Views host's sources, shared by the executables below.
set(PD_HOST_SOURCES
  src/cef/app.cpp
  src/cef/bridge.cpp
  src/cef/client.cpp
  src/cef/control.cpp
  src/cef/external.cpp
  src/cef/link.cpp
  src/cef/main.cpp
  src/cef/process_os.cpp
  src/cef/scheme.cpp
  src/cef/services.cpp
  src/cef/signals.cpp
  src/cef/window.cpp)
# The backend's clipboard: an X11 client on Linux; elsewhere not implemented yet (UNAVAILABLE).
if(OS_LINUX)
  list(APPEND PD_HOST_SOURCES src/cef/clipboard.cpp)
else()
  list(APPEND PD_HOST_SOURCES src/cef/clipboard_none.cpp)
endif()

if(OS_LINUX)
  # Linux: one executable is both the browser process and every Chromium subprocess (renderer,
  # GPU, utility, zygote).
  add_executable(pico-desktop-host ${PD_HOST_SOURCES})
  SET_EXECUTABLE_TARGET_PROPERTIES(pico-desktop-host)
  add_dependencies(pico-desktop-host libcef_dll_wrapper)
  target_link_libraries(pico-desktop-host PRIVATE libcef_dll_wrapper ${CEF_STANDARD_LIBS} pd_host_core pd_ipc xcb)
  target_compile_definitions(pico-desktop-host PRIVATE PD_VERSION="${PROJECT_VERSION}")
endif()

if(OS_LINUX)
  ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
  target_link_libraries(pico-desktop-host PRIVATE libcef_lib)
  # CEF's `-rpath,.` is relative to the working directory; find libcef.so next to the executable.
  set_target_properties(pico-desktop-host PROPERTIES BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
  # Development layout: CEF's binaries and resources are linked next to the executable (packaging
  # copies them, F6). The SUID chrome-sandbox isn't used: the sandbox runs on user namespaces.
  foreach(file IN LISTS CEF_BINARY_FILES)
    add_custom_command(TARGET pico-desktop-host POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E create_symlink "${CEF_BINARY_DIR_RELEASE}/${file}" "$<TARGET_FILE_DIR:pico-desktop-host>/${file}")
  endforeach()
  foreach(file IN LISTS CEF_RESOURCE_FILES)
    add_custom_command(TARGET pico-desktop-host POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E create_symlink "${CEF_RESOURCE_DIR}/${file}" "$<TARGET_FILE_DIR:pico-desktop-host>/${file}")
  endforeach()
  # `cmake --install … --component runtime` lays out the runtime: the host and CEF's files side by
  # side, with the notices (scripts/stage-runtime.mjs strips, hashes and archives it).
  install(TARGETS pico-desktop-host RUNTIME DESTINATION . COMPONENT runtime)
  foreach(file IN LISTS CEF_BINARY_FILES)
    install(PROGRAMS "${CEF_BINARY_DIR_RELEASE}/${file}" DESTINATION . COMPONENT runtime)
  endforeach()
  foreach(file IN LISTS CEF_RESOURCE_FILES)
    if(IS_DIRECTORY "${CEF_RESOURCE_DIR}/${file}")
      install(DIRECTORY "${CEF_RESOURCE_DIR}/${file}" DESTINATION . COMPONENT runtime)
    else()
      install(FILES "${CEF_RESOURCE_DIR}/${file}" DESTINATION . COMPONENT runtime)
    endif()
  endforeach()
  install(FILES "${CEF_ROOT}/LICENSE.txt" DESTINATION licenses RENAME CEF-LICENSE.txt COMPONENT runtime)
  install(FILES "${CEF_ROOT}/CREDITS.html" DESTINATION licenses RENAME CHROMIUM-CREDITS.html COMPONENT runtime)
  install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION licenses RENAME PICO-DESKTOP-LICENSE.txt
          COMPONENT runtime)
elseif(OS_MAC)
  # macOS: an app bundle, pico-desktop-host.app, with the CEF framework and Chromium's helper apps
  # (one per process kind, as CEF requires) in Contents/Frameworks (CEF's cefsimple layout).
  add_library(pd_host_cef OBJECT ${PD_HOST_SOURCES})
  SET_LIBRARY_TARGET_PROPERTIES(pd_host_cef)
  add_dependencies(pd_host_cef libcef_dll_wrapper)
  target_link_libraries(pd_host_cef PUBLIC pd_host_core pd_ipc)
  target_compile_definitions(pd_host_cef PRIVATE PD_VERSION="${PROJECT_VERSION}")

  set(EXECUTABLE_NAME "pico-desktop-host")
  set(PRODUCT_NAME "pico-desktop-host")
  set(VERSION_SHORT "${PROJECT_VERSION}")
  set(PD_APP "$<TARGET_BUNDLE_DIR:pico-desktop-host>")
  add_executable(pico-desktop-host MACOSX_BUNDLE src/cef/main_mac.mm $<TARGET_OBJECTS:pd_host_cef>)
  SET_EXECUTABLE_TARGET_PROPERTIES(pico-desktop-host)
  add_dependencies(pico-desktop-host libcef_dll_wrapper)
  target_link_libraries(pico-desktop-host PRIVATE libcef_dll_wrapper ${CEF_STANDARD_LIBS} pd_host_core pd_ipc)
  set_target_properties(pico-desktop-host PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist.in)
  add_custom_command(TARGET pico-desktop-host POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CEF_BINARY_DIR}/Chromium Embedded Framework.framework"
            "${PD_APP}/Contents/Frameworks/Chromium Embedded Framework.framework"
    VERBATIM)

  foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
    string(REPLACE ":" ";" _suffix_list ${_suffix_list})
    list(GET _suffix_list 0 _name_suffix)
    list(GET _suffix_list 1 _target_suffix)
    list(GET _suffix_list 2 _plist_suffix)
    set(_helper_target "pico-desktop-host_helper${_target_suffix}")
    set(_helper_output_name "pico-desktop-host Helper${_name_suffix}")
    # One Info.plist per helper, written here (configure_file would share the variables).
    set(_helper_info_plist "${CMAKE_CURRENT_BINARY_DIR}/helper-Info${_target_suffix}.plist")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/mac/helper-Info.plist.in" _plist_contents)
    string(REPLACE "\${EXECUTABLE_NAME}" "${_helper_output_name}" _plist_contents ${_plist_contents})
    string(REPLACE "\${PRODUCT_NAME}" "${_helper_output_name}" _plist_contents ${_plist_contents})
    string(REPLACE "\${BUNDLE_ID_SUFFIX}" "${_plist_suffix}" _plist_contents ${_plist_contents})
    string(REPLACE "\${VERSION_SHORT}" "${PROJECT_VERSION}" _plist_contents ${_plist_contents})
    file(WRITE ${_helper_info_plist} ${_plist_contents})

    add_executable(${_helper_target} MACOSX_BUNDLE src/cef/helper_mac.cpp $<TARGET_OBJECTS:pd_host_cef>)
    SET_EXECUTABLE_TARGET_PROPERTIES(${_helper_target})
    add_dependencies(${_helper_target} libcef_dll_wrapper)
    target_link_libraries(${_helper_target} PRIVATE libcef_dll_wrapper ${CEF_STANDARD_LIBS} pd_host_core pd_ipc)
    set_target_properties(${_helper_target} PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST ${_helper_info_plist}
      OUTPUT_NAME ${_helper_output_name})
    add_dependencies(pico-desktop-host "${_helper_target}")
    add_custom_command(TARGET pico-desktop-host POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:${_helper_target}>"
              "${PD_APP}/Contents/Frameworks/${_helper_output_name}.app"
      VERBATIM)
  endforeach()
elseif(OS_WINDOWS)
  # Windows: the host is pico-desktop-host.dll, run by CEF's console bootstrap (bootstrapc.exe,
  # copied as pico-desktop-host.exe), which sets up the sandbox for every process. CEF's DLLs and
  # resources go next to it.
  add_library(pico-desktop-host SHARED ${PD_HOST_SOURCES} src/cef/main_win.cpp)
  SET_LIBRARY_TARGET_PROPERTIES(pico-desktop-host)
  add_dependencies(pico-desktop-host libcef_dll_wrapper)
  ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
  target_link_libraries(pico-desktop-host PRIVATE libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS} pd_host_core pd_ipc)
  target_compile_definitions(pico-desktop-host PRIVATE PD_VERSION="${PROJECT_VERSION}")
  set(PD_HOST_DIR "$<TARGET_FILE_DIR:pico-desktop-host>")
  add_custom_command(TARGET pico-desktop-host POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CEF_BINARY_DIR}/bootstrapc.exe" "${PD_HOST_DIR}/pico-desktop-host.exe"
    VERBATIM)
  foreach(file IN LISTS CEF_BINARY_FILES)
    add_custom_command(TARGET pico-desktop-host POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CEF_BINARY_DIR}/${file}" "${PD_HOST_DIR}/${file}" VERBATIM)
  endforeach()
  foreach(file IN LISTS CEF_RESOURCE_FILES)
    if(IS_DIRECTORY "${CEF_RESOURCE_DIR}/${file}")
      add_custom_command(TARGET pico-desktop-host POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CEF_RESOURCE_DIR}/${file}" "${PD_HOST_DIR}/${file}" VERBATIM)
    else()
      add_custom_command(TARGET pico-desktop-host POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CEF_RESOURCE_DIR}/${file}" "${PD_HOST_DIR}/${file}" VERBATIM)
    endif()
  endforeach()
  set(PD_HOST "${PD_HOST_DIR}/pico-desktop-host.exe")
else()
  message(FATAL_ERROR "the CEF host is implemented for Linux, macOS and Windows")
endif()
if(NOT DEFINED PD_HOST)
  set(PD_HOST "$<TARGET_FILE:pico-desktop-host>")  # what the tests run
endif()
# The tests' scripts are bash; Windows runs them through it (Git Bash).
set(PD_SH "")
if(WIN32)
  set(PD_SH bash)
endif()

add_test(NAME host.version COMMAND ${PD_HOST} --version)
set_tests_properties(host.version PROPERTIES PASS_REGULAR_EXPRESSION "^pico-desktop-host ${PROJECT_VERSION} \\(CEF ")
add_test(NAME host.refuses-remote-debugging COMMAND ${PD_HOST} --app-dir=/nonexistent --remote-debugging-port=9222)
set_tests_properties(host.refuses-remote-debugging PROPERTIES WILL_FAIL TRUE)

# Self-tests in a real window: the fixture page tests itself under each trusted-origin scheme,
# and the host checks the sandbox and that no process outlives it. Needs a display and the built
# fixture (`pnpm --filter @pico-desktop/host-fixture build`); skipped otherwise.
set(PD_FIXTURE_DIR "${PROJECT_SOURCE_DIR}/tests/host-fixture/dist" CACHE PATH "The built host self-test fixture")
foreach(origin IN ITEMS app https)
  add_test(NAME host.self-test.${origin}
    COMMAND ${PD_SH} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-self-test.sh ${PD_HOST} ${PD_FIXTURE_DIR} ${origin})
  set_tests_properties(host.self-test.${origin} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE)
endforeach()
# A failed self-test keeps its profile and CEF log (tests/keep-log.sh with tests/failing-fixture,
# a page that fails one check). Needs a display; nothing to build.
add_test(NAME host.self-test.keep-log
  COMMAND ${PD_SH} ${CMAKE_CURRENT_SOURCE_DIR}/tests/keep-log.sh ${PD_HOST})
set_tests_properties(host.self-test.keep-log PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE)

# The backend's clipboard (tests/clipboard.sh): written by the backend and read by the page
# (Chromium's own X11 clipboard), written by the page and read by the backend, a large text, and
# NOT_ALLOWED reading without the capability. In its own Xvfb (it replaces the clipboard);
# skipped without pico, xvfb-run (or PD_TEST_REAL_CLIPBOARD=1), the fixture or the built CLI.
if(OS_LINUX)
  add_test(NAME host.clipboard
    COMMAND ${PD_SH} ${CMAKE_CURRENT_SOURCE_DIR}/tests/clipboard.sh ${PD_HOST} ${PD_FIXTURE_DIR})
  set_tests_properties(host.clipboard PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 RUN_SERIAL TRUE)
endif()

# Real keys and clicks (tests/user-input.sh, with the CLI's e2e driver, under --dev): F12 opens
# DevTools and another key doesn't; a clicked link to another site goes to xdg-open (a fake one)
# and the window stays. Needs a display, the fixture and the built CLI; skipped otherwise.
add_test(NAME host.user-input
  COMMAND ${PD_SH} ${CMAKE_CURRENT_SOURCE_DIR}/tests/user-input.sh ${PD_HOST} ${PD_FIXTURE_DIR})
set_tests_properties(host.user-input PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE)

# Single instance: a second launch focuses the first and exits 0; --multi-instance runs beside it.
if(TRUE)  # #23: on Windows too
  add_test(NAME host.single-instance
    COMMAND ${PD_SH} ${CMAKE_CURRENT_SOURCE_DIR}/tests/single-instance.sh ${PD_HOST} ${PD_FIXTURE_DIR})
  set_tests_properties(host.single-instance PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE)
endif()

# The orderly close: `closing` to the page and the backend while the window stays, then the close;
# a backend that doesn't finish is killed after the grace period. Needs hello-world built.
if(NOT WIN32)  # Windows: its stuck backend is a shell script
  add_test(NAME host.close
    COMMAND ${PD_SH} ${CMAKE_CURRENT_SOURCE_DIR}/tests/close.sh ${PD_HOST} ${PD_FIXTURE_DIR}
            ${PROJECT_SOURCE_DIR}/apps/hello-world)
  set_tests_properties(host.close PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE)
endif()

# No user namespaces: the host explains and refuses to start (simulated with unshare, no root).
if(OS_LINUX)  # Linux only for now
  add_test(NAME host.no-userns
    COMMAND ${PD_SH} ${CMAKE_CURRENT_SOURCE_DIR}/tests/no-userns.sh ${PD_HOST} ${PD_FIXTURE_DIR})
  set_tests_properties(host.no-userns PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
endif()

# A backend that exits leaving a child that holds its pipes: the exit is still noticed and the
# host doesn't wait on the pipes (#16, #17). Its backend is a program, so this runs everywhere.
add_executable(pd_stray_backend tests/stray-backend.cpp)
pd_warnings(pd_stray_backend)
add_test(NAME host.stray
  COMMAND ${PD_SH} ${CMAKE_CURRENT_SOURCE_DIR}/tests/stray.sh ${PD_HOST} ${PD_FIXTURE_DIR} $<TARGET_FILE:pd_stray_backend>)
set_tests_properties(host.stray PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE)

# Hangs (liveness pings) and crash loops (the restart budget). Needs pico for the hang backend.
if(NOT WIN32)  # Windows: its crashing and stray backends are shell scripts
  add_test(NAME host.hang
    COMMAND ${PD_SH} ${CMAKE_CURRENT_SOURCE_DIR}/tests/hang.sh ${PD_HOST} ${PD_FIXTURE_DIR})
  set_tests_properties(host.hang PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE)
endif()
if(NOT WIN32)  # Windows: notifications aren't implemented on Windows yet
  add_test(NAME host.services
    COMMAND ${PD_SH} ${CMAKE_CURRENT_SOURCE_DIR}/tests/services.sh ${PD_HOST} ${PD_FIXTURE_DIR})
  set_tests_properties(host.services PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE)
endif()
# The backend's data and cache directories (packages/communicator-pico/app_dirs.pi): created and written
# in the normal case; a blocked or read-only location is reported as an error and the backend carries on.
# Run through bash, so the script doesn't need an executable bit. Linux only for now.
if(OS_LINUX)
  add_test(NAME host.appdata
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/appdata.sh ${PD_HOST} ${PD_FIXTURE_DIR})
  set_tests_properties(host.appdata PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE)
endif()

# The F3 vertical slice end to end: the hello-world app's self-test build and its compiled Pico
# backend in a real window (`pnpm --filter @pico-desktop/hello-world build:self-test` and
# `backend`, which needs pico); skipped when they aren't built.
add_test(NAME hello-world.self-test
  COMMAND ${PD_SH} ${PROJECT_SOURCE_DIR}/apps/hello-world/scripts/self-test.sh ${PD_HOST})
set_tests_properties(hello-world.self-test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 150 RUN_SERIAL TRUE)

# The F4 developer loop on a freshly created app (tooling/cli/tests/dev-loop.sh): create,
# install, test, then `pnpm dev` with backend edits, a syntax error, frontend and contract edits,
# and Ctrl+C. Needs pico and the built CLI (`pnpm build`); skipped otherwise.
if(OS_LINUX)  # Linux only for now
  add_test(NAME cli.dev-loop
    COMMAND ${PD_SH} ${PROJECT_SOURCE_DIR}/tooling/cli/tests/dev-loop.sh ${PD_HOST})
  set_tests_properties(cli.dev-loop PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 RUN_SERIAL TRUE)
endif()

# The Vue template (tooling/cli/tests/vue-template.sh): create --template vue-ts, test, typecheck,
# and `pnpm e2e` in a real window with no CSP violations. Needs pico and the built CLI; skipped otherwise.
if(OS_LINUX)  # Linux only for now
  add_test(NAME cli.vue-template
    COMMAND bash ${PROJECT_SOURCE_DIR}/tooling/cli/tests/vue-template.sh ${PD_HOST})
  set_tests_properties(cli.vue-template PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 400 RUN_SERIAL TRUE)
endif()

# `pnpm e2e` on a freshly created app, finding the host the way an app does, on every system
# (tooling/cli/tests/e2e.sh). Needs pico and the built CLI; skipped otherwise.
add_test(NAME cli.e2e
  COMMAND ${PD_SH} ${PROJECT_SOURCE_DIR}/tooling/cli/tests/e2e.sh ${PD_HOST})
set_tests_properties(cli.e2e PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 RUN_SERIAL TRUE)

# The Pico debugger on the backend that the host starts (tooling/cli/tests/debug-attach.sh): a
# DAP client attaches through `pico dap` to the next backend, and `pnpm dev --debug` stops at a
# breakpoint in its handler, held past the hang timeout, then detaches. Needs pico, lldb-dap and
# ptrace_scope 0; skipped otherwise.
if(OS_LINUX)  # Linux only for now
  add_test(NAME cli.debug-attach
    COMMAND ${PD_SH} ${PROJECT_SOURCE_DIR}/tooling/cli/tests/debug-attach.sh ${PD_HOST})
  set_tests_properties(cli.debug-attach PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 RUN_SERIAL TRUE)
endif()

# The F6 bundle on a freshly created app (tooling/cli/tests/bundle.sh): build, run it with a bare
# environment, SIGTERM, package, verify, unpack and run again, and a tampered runtime refused.
# Needs pico, the built CLI and a staged runtime (`pnpm runtime:stage`); skipped otherwise.
add_test(NAME cli.bundle COMMAND ${PD_SH} ${PROJECT_SOURCE_DIR}/tooling/cli/tests/bundle.sh)
set_tests_properties(cli.bundle PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 RUN_SERIAL TRUE)
